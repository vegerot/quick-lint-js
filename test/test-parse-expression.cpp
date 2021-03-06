// quick-lint-js finds bugs in JavaScript programs.
// Copyright (C) 2020  Matthew Glazar
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <optional>
#include <quick-lint-js/char8.h>
#include <quick-lint-js/error-collector.h>
#include <quick-lint-js/error-matcher.h>
#include <quick-lint-js/error.h>
#include <quick-lint-js/location.h>
#include <quick-lint-js/narrow-cast.h>
#include <quick-lint-js/padded-string.h>
#include <quick-lint-js/parse.h>
#include <quick-lint-js/unreachable.h>
#include <quick-lint-js/warning.h>
#include <string>
#include <string_view>
#include <vector>

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::VariantWith;

namespace quick_lint_js {
namespace {
std::string summarize(const expression &);
std::string summarize(expression_ptr);
std::string summarize(std::optional<expression_ptr>);
std::string string8_to_string(string8_view);

class test_parser {
 public:
  explicit test_parser(const char8 *input)
      : code_(input),
        locator(&this->code_),
        parser_(&this->code_, &this->errors_) {}

  ~test_parser() { this->clean_up_expressions(); }

  expression_ptr parse_expression() {
    expression_ptr ast = this->parser_.parse_expression();
    this->expressions_needing_cleanup_.push_back(ast);
    return ast;
  }

  const std::vector<error_collector::error> &errors() const noexcept {
    return this->errors_.errors;
  }

  source_range range(expression_ptr ast) {
    return this->locator.range(ast->span());
  }

  quick_lint_js::lexer &lexer() noexcept { return this->parser_.lexer(); }

 private:
  void clean_up_expressions() {
    for (expression_ptr ast : this->expressions_needing_cleanup_) {
      this->clean_up_expression(ast);
    }
  }

  void clean_up_expression(expression_ptr ast) {
    auto visit_children = [&] {
      buffering_visitor v;
      // Deallocate the buffering_visitor stashed within 'ast'.
      ast->visit_children(v, this->parser_.expression_arena());
    };
    auto children = [&] {
      for (int i = 0; i < ast->child_count(); ++i) {
        this->clean_up_expression(ast->child(i));
      }
    };
    switch (ast->kind()) {
    case expression_kind::_invalid:
    case expression_kind::import:
    case expression_kind::literal:
    case expression_kind::new_target:
    case expression_kind::super:
    case expression_kind::variable:
      break;
    case expression_kind::_new:
    case expression_kind::_template:
    case expression_kind::array:
    case expression_kind::arrow_function_with_expression:
    case expression_kind::assignment:
    case expression_kind::binary_operator:
    case expression_kind::call:
    case expression_kind::compound_assignment:
    case expression_kind::index:
    case expression_kind::tagged_template_literal:
      children();
      break;
    case expression_kind::arrow_function_with_statements:
      children();
      visit_children();
      break;
    case expression_kind::_typeof:
    case expression_kind::await:
    case expression_kind::dot:
    case expression_kind::rw_unary_prefix:
    case expression_kind::rw_unary_suffix:
    case expression_kind::spread:
    case expression_kind::unary_operator:
      this->clean_up_expression(ast->child_0());
      break;
    case expression_kind::conditional:
      this->clean_up_expression(ast->child_0());
      this->clean_up_expression(ast->child_1());
      this->clean_up_expression(ast->child_2());
      break;
    case expression_kind::function:
    case expression_kind::named_function:
      visit_children();
      break;
    case expression_kind::object:
      for (int i = 0; i < ast->object_entry_count(); ++i) {
        auto entry = ast->object_entry(i);
        if (entry.property.has_value()) {
          this->clean_up_expression(*entry.property);
        }
        this->clean_up_expression(entry.value);
      }
      break;
    }
  }

  padded_string code_;

 public:
  quick_lint_js::locator locator;

 private:
  error_collector errors_;
  quick_lint_js::parser parser_;
  std::vector<expression_ptr> expressions_needing_cleanup_;
};

class test_parse_expression : public ::testing::Test {
 protected:
  expression_ptr parse_expression(const char8 *input) {
    test_parser &p = this->parsers_.emplace_back(input);

    expression_ptr ast = p.parse_expression();
    EXPECT_THAT(p.errors(), IsEmpty());
    return ast;
  }

 private:
  std::deque<test_parser> parsers_;
};

TEST_F(test_parse_expression, parse_single_token_expression) {
  {
    test_parser p(u8"x");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::variable);
    EXPECT_EQ(ast->variable_identifier().normalized_name(), u8"x");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 1);
  }

  {
    test_parser p(u8"42");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 2);
  }

  {
    test_parser p(u8"'hello'");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 7);
  }

  {
    test_parser p(u8"null");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 4);
  }

  {
    test_parser p(u8"true");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 4);
  }

  {
    test_parser p(u8"false");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 5);
  }

  {
    test_parser p(u8"this");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 4);
  }
}

TEST_F(test_parse_expression, parse_regular_expression) {
  {
    test_parser p(u8"/regexp/");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 8);
  }

  {
    test_parser p(u8"/=regexp/");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 9);
  }
}

TEST_F(test_parse_expression, parse_math_expression) {
  {
    test_parser p(u8"-x");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::unary_operator);
    EXPECT_EQ(ast->child_0()->kind(), expression_kind::variable);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 2);
  }

  {
    expression_ptr ast = this->parse_expression(u8"+x");
    EXPECT_EQ(summarize(ast), "unary(var x)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"~x");
    EXPECT_EQ(summarize(ast), "unary(var x)");
  }

  {
    test_parser p(u8"x+y");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(var x, var y)");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 3);
  }

  {
    expression_ptr ast = this->parse_expression(u8"x+y-z");
    EXPECT_EQ(summarize(ast), "binary(var x, var y, var z)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"2-4+1");
    EXPECT_EQ(summarize(ast), "binary(literal, literal, literal)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"-x+y");
    EXPECT_EQ(summarize(ast), "binary(unary(var x), var y)");
  }

  for (const char8 *input :
       {u8"2+2", u8"2-2", u8"2*2", u8"2/2", u8"2%2", u8"2**2", u8"2^2", u8"2&2",
        u8"2|2", u8"2<<2", u8"2>>2", u8"2>>>2"}) {
    SCOPED_TRACE(out_string8(u8"input = " + string8(input)));
    expression_ptr ast = this->parse_expression(input);
    EXPECT_EQ(summarize(ast), "binary(literal, literal)");
  }
}

TEST_F(test_parse_expression, parse_broken_math_expression) {
  {
    test_parser p(u8"2+");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, ?)");
    EXPECT_THAT(p.errors(), ElementsAre(ERROR_TYPE_FIELD(
                                error_missing_operand_for_operator, where,
                                offsets_matcher(p.locator, 1, 2))));
  }

  {
    test_parser p(u8"^2");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(?, literal)");
    EXPECT_THAT(p.errors(), ElementsAre(ERROR_TYPE_FIELD(
                                error_missing_operand_for_operator, where,
                                offsets_matcher(p.locator, 0, 1))));
  }

  {
    test_parser p(u8"2 * * 2");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, ?, literal)");
    EXPECT_THAT(p.errors(), ElementsAre(ERROR_TYPE_FIELD(
                                error_missing_operand_for_operator, where,
                                offsets_matcher(p.locator, 2, 3))));
  }

  {
    test_parser p(u8"2 & & & 2");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, ?, ?, literal)");

    EXPECT_THAT(
        p.errors(),
        ElementsAre(ERROR_TYPE_FIELD(error_missing_operand_for_operator, where,
                                     offsets_matcher(p.locator, 2, 3)),
                    ERROR_TYPE_FIELD(error_missing_operand_for_operator, where,
                                     offsets_matcher(p.locator, 4, 5))));
  }

  {
    test_parser p(u8"(2*)");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, ?)");
    EXPECT_THAT(p.errors(), ElementsAre(ERROR_TYPE_FIELD(
                                error_missing_operand_for_operator, where,
                                offsets_matcher(p.locator, 2, 3))));
  }

  {
    test_parser p(u8"2 * (3 + 4");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, binary(literal, literal))");
    EXPECT_THAT(p.errors(), ElementsAre(ERROR_TYPE_FIELD(
                                error_unmatched_parenthesis, where,
                                offsets_matcher(p.locator, 4, 5))));
  }

  {
    test_parser p(u8"2 * (3 + (4");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, binary(literal, literal))");

    EXPECT_THAT(
        p.errors(),
        ElementsAre(ERROR_TYPE_FIELD(error_unmatched_parenthesis, where,
                                     offsets_matcher(p.locator, 9, 10)),
                    ERROR_TYPE_FIELD(error_unmatched_parenthesis, where,
                                     offsets_matcher(p.locator, 4, 5))));
  }
}

TEST_F(test_parse_expression, parse_logical_expression) {
  for (const char8 *input : {u8"2==2", u8"2===2", u8"2!=2", u8"2!==2", u8"2>2",
                             u8"2<2", u8"2>=2", u8"2<=2", u8"2&&2", u8"2||2"}) {
    SCOPED_TRACE(out_string8(u8"input = " + string8(input)));
    test_parser p(input);
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "binary(literal, literal)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"!x");
    EXPECT_EQ(summarize(ast), "unary(var x)");
  }
}

TEST_F(test_parse_expression, parse_keyword_binary_operators) {
  {
    expression_ptr ast = this->parse_expression(u8"prop in object");
    EXPECT_EQ(summarize(ast), "binary(var prop, var object)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"object instanceof Class");
    EXPECT_EQ(summarize(ast), "binary(var object, var Class)");
  }
}

TEST_F(test_parse_expression, parse_typeof_unary_operator) {
  {
    expression_ptr ast = this->parse_expression(u8"typeof o");
    EXPECT_EQ(summarize(ast), "typeof(var o)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"typeof o === 'number'");
    EXPECT_EQ(summarize(ast), "binary(typeof(var o), literal)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"typeof o.p");
    EXPECT_EQ(summarize(ast), "typeof(dot(var o, p))");
  }
}

TEST_F(test_parse_expression, delete_unary_operator) {
  {
    expression_ptr ast = this->parse_expression(u8"delete variable");
    EXPECT_EQ(summarize(ast), "unary(var variable)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"delete variable.property");
    EXPECT_EQ(summarize(ast), "unary(dot(var variable, property))");
  }
}

TEST_F(test_parse_expression, void_unary_operator) {
  {
    expression_ptr ast = this->parse_expression(u8"void 0");
    EXPECT_EQ(summarize(ast), "unary(literal)");
  }
}

TEST_F(test_parse_expression, spread) {
  {
    test_parser p(u8"...args");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "spread(var args)");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 7);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, conditional_expression) {
  {
    test_parser p(u8"x?y:z");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::conditional);
    EXPECT_EQ(summarize(ast->child_0()), "var x");
    EXPECT_EQ(summarize(ast->child_1()), "var y");
    EXPECT_EQ(summarize(ast->child_2()), "var z");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 5);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"x+x?y+y:z+z");
    EXPECT_EQ(ast->kind(), expression_kind::conditional);
    EXPECT_EQ(summarize(ast->child_0()), "binary(var x, var x)");
    EXPECT_EQ(summarize(ast->child_1()), "binary(var y, var y)");
    EXPECT_EQ(summarize(ast->child_2()), "binary(var z, var z)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"a ? b : c ? d : e");
    EXPECT_EQ(summarize(ast), "cond(var a, var b, cond(var c, var d, var e))");
  }
}

TEST_F(test_parse_expression, parse_function_call) {
  {
    test_parser p(u8"f()");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::call);
    EXPECT_EQ(summarize(ast->child_0()), "var f");
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 3);
  }

  {
    expression_ptr ast = this->parse_expression(u8"f(x)");
    EXPECT_EQ(ast->kind(), expression_kind::call);
    EXPECT_EQ(summarize(ast->child_0()), "var f");
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(1)), "var x");
  }

  {
    expression_ptr ast = this->parse_expression(u8"f(x,y)");
    EXPECT_EQ(ast->kind(), expression_kind::call);
    EXPECT_EQ(summarize(ast->child_0()), "var f");
    EXPECT_EQ(ast->child_count(), 3);
    EXPECT_EQ(summarize(ast->child(1)), "var x");
    EXPECT_EQ(summarize(ast->child(2)), "var y");
  }
}

TEST_F(test_parse_expression, parse_dot_expressions) {
  {
    test_parser p(u8"x.prop");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::dot);
    EXPECT_EQ(summarize(ast->child_0()), "var x");
    EXPECT_EQ(ast->variable_identifier().normalized_name(), u8"prop");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 6);
  }

  {
    expression_ptr ast = this->parse_expression(u8"x.p1.p2");
    EXPECT_EQ(summarize(ast), "dot(dot(var x, p1), p2)");
  }

  for (string8 keyword :
       {u8"catch", u8"class", u8"default", u8"get", u8"try"}) {
    SCOPED_TRACE(out_string8(keyword));
    string8 code = u8"promise." + keyword;
    expression_ptr ast = this->parse_expression(code.c_str());
    EXPECT_EQ(summarize(ast),
              "dot(var promise, " + string8_to_string(keyword) + ")");
  }
}

TEST_F(test_parse_expression, parse_indexing_expression) {
  {
    test_parser p(u8"xs[i]");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::index);
    EXPECT_EQ(summarize(ast->child_0()), "var xs");
    EXPECT_EQ(summarize(ast->child_1()), "var i");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 5);
  }
}

TEST_F(test_parse_expression, parse_parenthesized_expression) {
  {
    test_parser p(u8"(x)");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "var x");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 1);
    EXPECT_EQ(p.range(ast).end_offset(), 2);
  }

  {
    expression_ptr ast = this->parse_expression(u8"x+(y)");
    EXPECT_EQ(summarize(ast), "binary(var x, var y)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"x+(y+z)");
    EXPECT_EQ(summarize(ast), "binary(var x, binary(var y, var z))");
  }

  {
    expression_ptr ast = this->parse_expression(u8"(x+y)+z");
    EXPECT_EQ(summarize(ast), "binary(binary(var x, var y), var z)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"x+(y+z)+w");
    EXPECT_EQ(summarize(ast), "binary(var x, binary(var y, var z), var w)");
  }
}

TEST_F(test_parse_expression, parse_await_expression) {
  {
    test_parser p(u8"await myPromise");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "await(var myPromise)");
    EXPECT_EQ(ast->kind(), expression_kind::await);
    EXPECT_EQ(summarize(ast->child_0()), "var myPromise");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 15);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, parse_new_expression) {
  {
    test_parser p(u8"new Date");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::_new);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child_0()), "var Date");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 8);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"new Date()");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::_new);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child_0()), "var Date");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 10);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"new Date(y,m,d)");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "new(var Date, var y, var m, var d)");
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, new_target) {
  {
    test_parser p(u8"new.target");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "newtarget");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 10);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"new.target()");
    EXPECT_EQ(summarize(ast), "call(newtarget)");
  }
}

TEST_F(test_parse_expression, super) {
  {
    test_parser p(u8"super()");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "call(super)");
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"super.method()");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "call(dot(super, method))");
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, import) {
  {
    test_parser p(u8"import(url)");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "call(import, var url)");
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"import.meta");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "dot(import, meta)");
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, parse_assignment) {
  {
    test_parser p(u8"x=y");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::assignment);
    EXPECT_EQ(summarize(ast->child_0()), "var x");
    EXPECT_EQ(summarize(ast->child_1()), "var y");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 3);
  }

  {
    expression_ptr ast = this->parse_expression(u8"x.p=z");
    EXPECT_EQ(ast->kind(), expression_kind::assignment);
    EXPECT_EQ(summarize(ast->child_0()), "dot(var x, p)");
    EXPECT_EQ(summarize(ast->child_1()), "var z");
  }

  {
    expression_ptr ast = this->parse_expression(u8"f().p=x");
    EXPECT_EQ(summarize(ast), "assign(dot(call(var f), p), var x)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"x=y=z");
    EXPECT_EQ(summarize(ast), "assign(var x, assign(var y, var z))");
  }

  {
    expression_ptr ast = this->parse_expression(u8"x,y=z,w");
    EXPECT_EQ(summarize(ast), "binary(var x, assign(var y, var z), var w)");
  }
}

TEST_F(test_parse_expression, parse_compound_assignment) {
  for (string8 op : {u8"*=", u8"/=", u8"%=", u8"+=", u8"-=", u8"<<=", u8">>=",
                     u8">>>=", u8"&=", u8"^=", u8"|=", u8"**="}) {
    SCOPED_TRACE(out_string8(op));
    string8 code = u8"x " + op + u8" y";
    test_parser p(code.c_str());
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::compound_assignment);
    EXPECT_EQ(summarize(ast->child_0()), "var x");
    EXPECT_EQ(summarize(ast->child_1()), "var y");
    EXPECT_THAT(p.errors(), IsEmpty());
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), code.size());
  }
}

TEST_F(test_parse_expression, parse_invalid_assignment) {
  {
    test_parser p(u8"x+y=z");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "assign(binary(var x, var y), var z)");

    EXPECT_THAT(p.errors(), ElementsAre(ERROR_TYPE_FIELD(
                                error_invalid_expression_left_of_assignment,
                                where, offsets_matcher(p.locator, 0, 3))));
  }

  for (const char8 *code : {
           u8"f()=x",
           u8"-x=y",
           u8"42=y",
           u8"(x=y)=z",
       }) {
    SCOPED_TRACE(out_string8(code));
    test_parser p(code);
    p.parse_expression();

    EXPECT_THAT(
        p.errors(),
        ElementsAre(
            VariantWith<error_invalid_expression_left_of_assignment>(_)));
  }
}

TEST_F(test_parse_expression, parse_prefix_plusplus_minusminus) {
  {
    test_parser p(u8"++x");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::rw_unary_prefix);
    EXPECT_EQ(summarize(ast->child_0()), "var x");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 3);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"--y");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::rw_unary_prefix);
    EXPECT_EQ(summarize(ast->child_0()), "var y");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 3);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, parse_suffix_plusplus_minusminus) {
  {
    test_parser p(u8"x++");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::rw_unary_suffix);
    EXPECT_EQ(summarize(ast->child_0()), "var x");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 3);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, suffix_plusplus_minusminus_disallows_line_break) {
  {
    test_parser p(u8"x\n++\ny");

    expression_ptr ast_1 = p.parse_expression();
    EXPECT_EQ(summarize(ast_1), "var x");

    ASSERT_EQ(p.lexer().peek().type, token_type::semicolon)
        << "Semicolon should be inserted (ASI)";
    p.lexer().skip();

    expression_ptr ast_2 = p.parse_expression();
    EXPECT_EQ(summarize(ast_2), "rwunary(var y)");

    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, parse_template) {
  {
    test_parser p(u8"`hello`");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::literal);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 7);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"`hello${world}`");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::_template);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child(0)), "var world");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 15);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"`${one}${two}${three}`");
    EXPECT_EQ(summarize(ast), "template(var one, var two, var three)");
  }
}

TEST_F(test_parse_expression, tagged_template_literal) {
  {
    test_parser p(u8"hello`world`");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::tagged_template_literal);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child(0)), "var hello");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 12);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"hello`template ${literal} thingy`");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::tagged_template_literal);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)), "var hello");
    EXPECT_EQ(summarize(ast->child(1)), "var literal");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 33);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"a.b()`c`");
    EXPECT_EQ(summarize(ast), "taggedtemplate(call(dot(var a, b)))");
  }

  {
    expression_ptr ast = this->parse_expression(u8"tag`template`.property");
    EXPECT_EQ(summarize(ast), "dot(taggedtemplate(var tag), property)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"x + tag`template`");
    EXPECT_EQ(summarize(ast), "binary(var x, taggedtemplate(var tag))");
  }
}

TEST_F(test_parse_expression, array_literal) {
  {
    test_parser p(u8"[]");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::array);
    EXPECT_EQ(ast->child_count(), 0);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 2);
  }

  {
    expression_ptr ast = this->parse_expression(u8"[x]");
    EXPECT_EQ(ast->kind(), expression_kind::array);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child(0)), "var x");
  }

  {
    expression_ptr ast = this->parse_expression(u8"[x, y]");
    EXPECT_EQ(ast->kind(), expression_kind::array);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)), "var x");
    EXPECT_EQ(summarize(ast->child(1)), "var y");
  }

  {
    expression_ptr ast = this->parse_expression(u8"[,,x,,y,,]");
    EXPECT_EQ(summarize(ast), "array(var x, var y)");
  }
}

TEST_F(test_parse_expression, object_literal) {
  {
    test_parser p(u8"{}");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 0);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 2);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"{key: value}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "var value");
  }

  {
    expression_ptr ast =
        this->parse_expression(u8"{key1: value1, key2: value2}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 2);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "var value1");
    EXPECT_EQ(summarize(ast->object_entry(1).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(1).value), "var value2");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{'key': value}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "var value");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{[key]: value}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "var key");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "var value");
  }

  {
    test_parser p(u8"{thing}");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    auto entry = ast->object_entry(0);
    EXPECT_EQ(summarize(entry.property), "literal");
    EXPECT_EQ(p.range(entry.property.value()).begin_offset(), 1);
    EXPECT_EQ(p.range(entry.property.value()).end_offset(), 6);
    EXPECT_EQ(summarize(entry.value), "var thing");
    EXPECT_EQ(p.range(entry.value).begin_offset(), 1);
    EXPECT_EQ(p.range(entry.value).end_offset(), 6);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast =
        this->parse_expression(u8"{key1: value1, thing2, key3: value3}");
    EXPECT_EQ(summarize(ast),
              "object(literal, var value1, literal, var thing2, literal, var "
              "value3)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{key: variable = value}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value),
              "assign(var variable, var value)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{key = value}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value),
              "assign(var key, var value)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{...other, k: v}");
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 2);
    EXPECT_FALSE(ast->object_entry(0).property.has_value());
    EXPECT_EQ(summarize(ast->object_entry(0).value), "spread(var other)");
    EXPECT_EQ(summarize(ast->object_entry(1).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(1).value), "var v");
  }
}

TEST_F(test_parse_expression, object_literal_with_method_key) {
  {
    test_parser p(u8"{ func(a, b) { } }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "function");
    EXPECT_EQ(p.range(ast->object_entry(0).value).begin_offset(), 2);
    EXPECT_EQ(p.range(ast->object_entry(0).value).end_offset(), 16);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"{ 'func'(a, b) { } }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "function");
    EXPECT_EQ(p.range(ast->object_entry(0).value).begin_offset(), 2);
    EXPECT_EQ(p.range(ast->object_entry(0).value).end_offset(), 18);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"{ [func](a, b) { } }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "var func");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "function");
    EXPECT_EQ(p.range(ast->object_entry(0).value).begin_offset(), 2);
    EXPECT_EQ(p.range(ast->object_entry(0).value).end_offset(), 18);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, object_literal_with_getter_setter_key) {
  {
    test_parser p(u8"{ get prop() { } }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "function");
    // TODO(strager): Should the span start at 'get' instead?
    EXPECT_EQ(p.range(ast->object_entry(0).value).begin_offset(), 6);
    EXPECT_EQ(p.range(ast->object_entry(0).value).end_offset(), 16);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"{ set prop(v) { } }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::object);
    EXPECT_EQ(ast->object_entry_count(), 1);
    EXPECT_EQ(summarize(ast->object_entry(0).property), "literal");
    EXPECT_EQ(summarize(ast->object_entry(0).value), "function");
    // TODO(strager): Should the span start at 'set' instead?
    EXPECT_EQ(p.range(ast->object_entry(0).value).begin_offset(), 6);
    EXPECT_EQ(p.range(ast->object_entry(0).value).end_offset(), 17);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"{get 1234() { }}");
    EXPECT_EQ(summarize(ast), "object(literal, function)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{get 'string key'() { }}");
    EXPECT_EQ(summarize(ast), "object(literal, function)");
  }

  {
    expression_ptr ast =
        this->parse_expression(u8"{get [expression + key]() { }}");
    EXPECT_EQ(summarize(ast),
              "object(binary(var expression, var key), function)");
  }
}

TEST_F(test_parse_expression, object_literal_with_keyword_key) {
  for (string8 keyword :
       {u8"catch", u8"class", u8"default", u8"get", u8"set", u8"try"}) {
    SCOPED_TRACE(out_string8(keyword));

    {
      string8 code = u8"{" + keyword + u8": null}";
      expression_ptr ast = this->parse_expression(code.c_str());
      EXPECT_EQ(summarize(ast), "object(literal, literal)");
    }

    {
      string8 code = u8"{get " + keyword + u8"() {}}";
      expression_ptr ast = this->parse_expression(code.c_str());
      EXPECT_EQ(summarize(ast), "object(literal, function)");
    }

    {
      string8 code = u8"{" + keyword + u8"() {}}";
      expression_ptr ast = this->parse_expression(code.c_str());
      EXPECT_EQ(summarize(ast), "object(literal, function)");
    }
  }
}

TEST_F(test_parse_expression, object_literal_with_number_key) {
  expression_ptr ast = this->parse_expression(u8"{1234: null}");
  EXPECT_EQ(summarize(ast), "object(literal, literal)");
}

TEST_F(test_parse_expression, malformed_object_literal) {
  {
    test_parser p(u8"{p1: v1 p2}");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(summarize(ast), "object(literal, var v1, literal, var p2)");
    EXPECT_THAT(p.errors(),
                ElementsAre(ERROR_TYPE_FIELD(
                    error_missing_comma_between_object_literal_entries, where,
                    offsets_matcher(p.locator, 7, 7))));
  }
}

TEST_F(test_parse_expression, parse_comma_expression) {
  {
    test_parser p(u8"x,y,z");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::binary_operator);
    EXPECT_EQ(summarize(ast->child(0)), "var x");
    EXPECT_EQ(summarize(ast->child(1)), "var y");
    EXPECT_EQ(summarize(ast->child(2)), "var z");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 5);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"(x+(y,z)+w)");
    EXPECT_EQ(summarize(ast), "binary(var x, binary(var y, var z), var w)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"`${2+2, four}`");
    EXPECT_EQ(summarize(ast), "template(binary(literal, literal, var four))");
  }

  {
    expression_ptr ast = this->parse_expression(u8"i = 0, j = 0");
    EXPECT_EQ(summarize(ast),
              "binary(assign(var i, literal), assign(var j, literal))");
  }
}

TEST_F(test_parse_expression, parse_function_expression) {
  {
    test_parser p(u8"function(){} /* */");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::function);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 12);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"function(x, y){}");
    EXPECT_EQ(ast->kind(), expression_kind::function);
  }

  {
    expression_ptr ast = this->parse_expression(u8"function(){}()");
    EXPECT_EQ(ast->kind(), expression_kind::call);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(ast->child_0()->kind(), expression_kind::function);
  }

  {
    expression_ptr ast = this->parse_expression(u8"function f(){}");
    EXPECT_EQ(ast->kind(), expression_kind::named_function);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->variable_identifier().normalized_name(), u8"f");
  }
}

TEST_F(test_parse_expression, function_with_destructuring_parameters) {
  {
    expression_ptr ast = this->parse_expression(u8"function({a, b}) { c }");
    EXPECT_EQ(summarize(ast), "function");
  }

  {
    expression_ptr ast = this->parse_expression(u8"function([a, b]) { c }");
    EXPECT_EQ(summarize(ast), "function");
  }
}

TEST_F(test_parse_expression, async_function_expression) {
  {
    test_parser p(u8"async function(){}");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::function);
    EXPECT_EQ(ast->attributes(), function_attributes::async);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 18);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"async function f(){}");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::named_function);
    EXPECT_EQ(ast->attributes(), function_attributes::async);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 20);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, arrow_function_with_expression) {
  {
    test_parser p(u8"() => a");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child_0()), "var a");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 7);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"a => b");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)), "var a");
    EXPECT_EQ(summarize(ast->child(1)), "var b");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 6);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"(a) => b");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)), "var a");
    EXPECT_EQ(summarize(ast->child(1)), "var b");
    // TODO(strager): Implement begin_offset.
    EXPECT_EQ(p.range(ast).end_offset(), 8);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"(a, b) => c");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 3);
    EXPECT_EQ(summarize(ast->child(0)), "var a");
    EXPECT_EQ(summarize(ast->child(1)), "var b");
    EXPECT_EQ(summarize(ast->child(2)), "var c");
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"() => a, b");
    EXPECT_EQ(summarize(ast), "binary(arrowexpr(var a), var b)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"a => b, c");
    EXPECT_EQ(summarize(ast), "binary(arrowexpr(var a, var b), var c)");
  }
}

TEST_F(test_parse_expression, arrow_function_with_statements) {
  {
    test_parser p(u8"() => { a; }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_statements);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 0);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 12);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"a => { b; } /* */");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_statements);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child(0)), "var a");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 11);
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, arrow_function_with_destructuring_parameters) {
  {
    test_parser p(u8"({a, b}) => c");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)),
              "object(literal, var a, literal, var b)");
    EXPECT_EQ(summarize(ast->child(1)), "var c");
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"([a, b]) => c");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::normal);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)), "array(var a, var b)");
    EXPECT_EQ(summarize(ast->child(1)), "var c");
    EXPECT_THAT(p.errors(), IsEmpty());
  }
}

TEST_F(test_parse_expression, async_arrow_function) {
  {
    test_parser p(u8"async () => { a; }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_statements);
    EXPECT_EQ(ast->attributes(), function_attributes::async);
    EXPECT_EQ(ast->child_count(), 0);
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 18);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"async x => { y; }");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_statements);
    EXPECT_EQ(ast->attributes(), function_attributes::async);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child(0)), "var x");
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"async (x, y, z) => { w; }");
    EXPECT_EQ(summarize(ast), "asyncarrowblock(var x, var y, var z)");
  }

  {
    test_parser p(u8"async () => a");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::async);
    EXPECT_EQ(ast->child_count(), 1);
    EXPECT_EQ(summarize(ast->child(0)), "var a");
    EXPECT_EQ(p.range(ast).begin_offset(), 0);
    EXPECT_EQ(p.range(ast).end_offset(), 13);
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    test_parser p(u8"async x => y");
    expression_ptr ast = p.parse_expression();
    EXPECT_EQ(ast->kind(), expression_kind::arrow_function_with_expression);
    EXPECT_EQ(ast->attributes(), function_attributes::async);
    EXPECT_EQ(ast->child_count(), 2);
    EXPECT_EQ(summarize(ast->child(0)), "var x");
    EXPECT_EQ(summarize(ast->child(1)), "var y");
    EXPECT_THAT(p.errors(), IsEmpty());
  }

  {
    expression_ptr ast = this->parse_expression(u8"async (x, y, z) => w");
    EXPECT_EQ(summarize(ast), "asyncarrowexpr(var x, var y, var z, var w)");
  }
}

TEST_F(test_parse_expression, parse_mixed_expression) {
  {
    expression_ptr ast = this->parse_expression(u8"a+f()");
    EXPECT_EQ(summarize(ast), "binary(var a, call(var f))");
  }

  {
    expression_ptr ast = this->parse_expression(u8"a+f(x+y,-z-w)+b");
    EXPECT_EQ(summarize(ast),
              "binary(var a, call(var f, binary(var x, var y), "
              "binary(unary(var z), var w)), var b)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"(x+y).z");
    EXPECT_EQ(summarize(ast), "dot(binary(var x, var y), z)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"/hello/.test(string)");
    EXPECT_EQ(summarize(ast), "call(dot(literal, test), var string)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"!/hello/.test(string)");
    EXPECT_EQ(summarize(ast), "unary(call(dot(literal, test), var string))");
  }

  {
    expression_ptr ast = this->parse_expression(u8"{a: new A(), b: new B()}");
    EXPECT_EQ(summarize(ast),
              "object(literal, new(var A), literal, new(var B))");
  }

  {
    expression_ptr ast =
        this->parse_expression(u8"o && typeof o === 'object' ? o[k] : null");
    if (false) {  // TODO(strager): Check AST.
      EXPECT_EQ(summarize(ast),
                "cond(binary(var o, binary(typeof(var o), literal)), "
                "index(var o, var k), literal)");
    }
  }

  {
    expression_ptr ast = this->parse_expression(u8"!!o && k in o");
    EXPECT_EQ(summarize(ast), "binary(unary(unary(var o)), var k, var o)");
  }

  {
    expression_ptr ast = this->parse_expression(u8"x --> 0");
    EXPECT_EQ(summarize(ast), "binary(rwunarysuffix(var x), literal)");
  }
}

std::string summarize(const expression &expression) {
  auto children = [&] {
    std::string result;
    bool need_comma = false;
    for (int i = 0; i < expression.child_count(); ++i) {
      if (need_comma) {
        result += ", ";
      }
      result += summarize(expression.child(i));
      need_comma = true;
    }
    return result;
  };
  auto function_attributes = [&]() -> std::string {
    switch (expression.attributes()) {
    case function_attributes::normal:
      return "";
    case function_attributes::async:
      return "async";
    }
    QLJS_UNREACHABLE();
  };
  switch (expression.kind()) {
  case expression_kind::_invalid:
    return "?";
  case expression_kind::_new:
    return "new(" + children() + ")";
  case expression_kind::_template:
    return "template(" + children() + ")";
  case expression_kind::_typeof:
    return "typeof(" + summarize(expression.child_0()) + ")";
  case expression_kind::array:
    return "array(" + children() + ")";
  case expression_kind::arrow_function_with_expression:
    return function_attributes() + "arrowexpr(" + children() + ")";
  case expression_kind::arrow_function_with_statements:
    return function_attributes() + "arrowblock(" + children() + ")";
  case expression_kind::assignment:
    return "assign(" + children() + ")";
  case expression_kind::await:
    return "await(" + summarize(expression.child_0()) + ")";
  case expression_kind::call:
    return "call(" + children() + ")";
  case expression_kind::conditional:
    return "cond(" + summarize(expression.child_0()) + ", " +
           summarize(expression.child_1()) + ", " +
           summarize(expression.child_2()) + ")";
  case expression_kind::dot:
    return "dot(" + summarize(expression.child_0()) + ", " +
           string8_to_string(
               expression.variable_identifier().normalized_name()) +
           ")";
  case expression_kind::function:
    return "function";
  case expression_kind::import:
    return "import";
  case expression_kind::index:
    return "index(" + children() + ")";
  case expression_kind::literal:
    return "literal";
  case expression_kind::named_function:
    return "function " +
           string8_to_string(
               expression.variable_identifier().normalized_name());
  case expression_kind::new_target:
    return "newtarget";
  case expression_kind::object: {
    std::string result = "object(";
    bool need_comma = false;
    for (int i = 0; i < expression.object_entry_count(); ++i) {
      if (need_comma) {
        result += ", ";
      }
      auto entry = expression.object_entry(i);
      result += summarize(entry.property);
      result += ", ";
      result += summarize(entry.value);
      need_comma = true;
    }
    result += ")";
    return result;
  }
  case expression_kind::rw_unary_prefix:
    return "rwunary(" + summarize(expression.child_0()) + ")";
  case expression_kind::rw_unary_suffix:
    return "rwunarysuffix(" + summarize(expression.child_0()) + ")";
  case expression_kind::spread:
    return "spread(" + summarize(expression.child_0()) + ")";
  case expression_kind::super:
    return "super";
  case expression_kind::tagged_template_literal:
    return "taggedtemplate(" + children() + ")";
  case expression_kind::unary_operator:
    return "unary(" + summarize(expression.child_0()) + ")";
  case expression_kind::compound_assignment:
    return "upassign(" + children() + ")";
  case expression_kind::variable:
    return "var " + string8_to_string(
                        expression.variable_identifier().normalized_name());
  case expression_kind::binary_operator:
    return "binary(" + children() + ")";
  }
  QLJS_UNREACHABLE();
}

std::string summarize(expression_ptr expression) {
  return summarize(*expression);
}

std::string summarize(std::optional<expression_ptr> expression) {
  if (expression.has_value()) {
    return summarize(*expression);
  } else {
    return "(null)";
  }
}

QLJS_WARNING_PUSH
QLJS_WARNING_IGNORE_GCC("-Wuseless-cast")
std::string string8_to_string(string8_view sv) {
  return std::string(
      std::string_view(reinterpret_cast<const char *>(sv.data()), sv.size()));
}
QLJS_WARNING_POP
}
}
