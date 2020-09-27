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

#ifndef QUICK_LINT_JS_ERROR_COLLECTOR_H
#define QUICK_LINT_JS_ERROR_COLLECTOR_H

#include <iosfwd>
#include <quick-lint-js/error.h>
#include <quick-lint-js/language.h>
#include <quick-lint-js/lex.h>
#include <quick-lint-js/location.h>
#include <quick-lint-js/warning.h>
#include <type_traits>
#include <vector>

QLJS_WARNING_PUSH
QLJS_WARNING_IGNORE_MSVC(26495)  // Variable is uninitialized.
QLJS_WARNING_IGNORE_MSVC(26812)  // Prefer 'enum class' over 'enum'.

namespace quick_lint_js {
struct error_collector : public error_reporter {
  void report_error_assignment_before_variable_declaration(
      identifier assignment, identifier declaration) override {
    this->errors.emplace_back(error_assignment_before_variable_declaration,
                              assignment.span(), declaration.span());
  }

  void report_error_assignment_to_const_global_variable(
      identifier assignment) override {
    this->errors.emplace_back(error_assignment_to_const_global_variable,
                              assignment.span());
  }

  void report_error_assignment_to_const_variable(
      identifier declaration, identifier assignment,
      variable_kind var_kind) override {
    this->errors.emplace_back(error{error_assignment_to_const_variable,
                                    assignment.span(), declaration.span(),
                                    var_kind});
  }

  void report_error_assignment_to_undeclared_variable(
      identifier assignment) override {
    this->errors.emplace_back(error_assignment_to_undeclared_variable,
                              assignment.span());
  }

  void report_error_big_int_literal_contains_decimal_point(
      source_code_span where) override {
    this->errors.emplace_back(error_big_int_literal_contains_decimal_point,
                              where);
  }

  void report_error_big_int_literal_contains_exponent(
      source_code_span where) override {
    this->errors.emplace_back(error_big_int_literal_contains_exponent, where);
  }

  void report_error_big_int_literal_contains_leading_zero(
      source_code_span where) override {
    this->errors.emplace_back(error_big_int_literal_contains_leading_zero,
                              where);
  }

  void report_error_invalid_binding_in_let_statement(
      source_code_span where) override {
    this->errors.emplace_back(
        error{error_invalid_binding_in_let_statement, where});
  }

  void report_error_invalid_expression_left_of_assignment(
      source_code_span where) override {
    this->errors.emplace_back(
        error{error_invalid_expression_left_of_assignment, where});
  }

  void report_error_let_with_no_bindings(source_code_span where) override {
    this->errors.emplace_back(error{error_let_with_no_bindings, where});
  }

  void report_error_missing_comma_between_object_literal_entries(
      source_code_span where) override {
    this->errors.emplace_back(
        error(error_missing_comma_between_object_literal_entries, where));
  }

  void report_error_missing_operand_for_operator(
      source_code_span where) override {
    this->errors.emplace_back(error{error_missing_operand_for_operator, where});
  }

  void report_error_missing_semicolon_after_expression(
      source_code_span where) override {
    this->errors.emplace_back(
        error{error_missing_semicolon_after_expression, where});
  }

  void report_error_redeclaration_of_global_variable(
      identifier redeclaration) override {
    this->errors.emplace_back(error_redeclaration_of_global_variable,
                              redeclaration.span());
  }

  void report_error_redeclaration_of_variable(
      identifier redeclaration, identifier original_declaration) override {
    this->errors.emplace_back(error(error_redeclaration_of_variable,
                                    redeclaration.span(),
                                    original_declaration.span()));
  }

  void report_error_stray_comma_in_let_statement(
      source_code_span where) override {
    this->errors.emplace_back(error{error_stray_comma_in_let_statement, where});
  }

  void report_error_unclosed_block_comment(
      source_code_span comment_open) override {
    this->errors.emplace_back(
        error{error_unclosed_block_comment, comment_open});
  }

  void report_error_unclosed_regexp_literal(
      source_code_span regexp_literal) override {
    this->errors.emplace_back(
        error{error_unclosed_regexp_literal, regexp_literal});
  }

  void report_error_unclosed_string_literal(
      source_code_span string_literal) override {
    this->errors.emplace_back(
        error{error_unclosed_string_literal, string_literal});
  }

  void report_error_unclosed_template(
      source_code_span incomplete_template) override {
    this->errors.emplace_back(
        error{error_unclosed_template, incomplete_template});
  }

  void report_error_unexpected_characters_in_number(
      source_code_span characters) override {
    this->errors.emplace_back(error_unexpected_characters_in_number,
                              characters);
  }

  void report_error_unexpected_characters_in_octal_number(
      source_code_span characters) override {
    this->errors.emplace_back(error_unexpected_characters_in_octal_number,
                              characters);
  }

  void report_error_unexpected_hash_character(source_code_span where) override {
    this->errors.emplace_back(error_unexpected_hash_character, where);
  }

  void report_error_unexpected_identifier(source_code_span where) override {
    this->errors.emplace_back(error{error_unexpected_identifier, where});
  }

  void report_error_unmatched_parenthesis(source_code_span where) override {
    this->errors.emplace_back(error{error_unmatched_parenthesis, where});
  }

  void report_error_use_of_undeclared_variable(identifier name) override {
    this->errors.emplace_back(
        error{error_use_of_undeclared_variable, name.span()});
  }

  void report_error_variable_used_before_declaration(
      identifier use, identifier declaration) override {
    this->errors.emplace_back(error(error_variable_used_before_declaration,
                                    use.span(), declaration.span()));
  }

  void report_fatal_error_unimplemented_character(
      const char *qljs_file_name, int qljs_line, const char *qljs_function_name,
      const char8 *character) override;
  void report_fatal_error_unimplemented_token(
      const char *qljs_file_name, int qljs_line, const char *qljs_function_name,
      token_type, const char8 *token_begin) override;

  enum error_kind {
    error_assignment_before_variable_declaration,
    error_assignment_to_const_global_variable,
    error_assignment_to_const_variable,
    error_assignment_to_undeclared_variable,
    error_big_int_literal_contains_decimal_point,
    error_big_int_literal_contains_exponent,
    // q(🤷🏽‍♀️)either rename to `big_int_literal_for_octal` or just
    // remove
    error_big_int_literal_contains_leading_zero,
    error_invalid_binding_in_let_statement,
    error_invalid_expression_left_of_assignment,
    error_let_with_no_bindings,
    error_missing_comma_between_object_literal_entries,
    error_missing_operand_for_operator,
    error_missing_semicolon_after_expression,
    error_redeclaration_of_global_variable,
    error_redeclaration_of_variable,
    error_stray_comma_in_let_statement,
    error_unclosed_block_comment,
    error_unclosed_regexp_literal,
    error_unclosed_string_literal,
    error_unclosed_template,
    error_unexpected_characters_in_number,
    error_unexpected_characters_in_octal_number,
    error_unexpected_hash_character,
    error_unexpected_identifier,
    error_unmatched_parenthesis,
    error_use_of_undeclared_variable,
    error_variable_used_before_declaration,
  };
  struct error {
    explicit error(error_kind kind, source_code_span where) noexcept
        : kind(kind), where(where) {}

    explicit error(error_kind kind, source_code_span where,
                   source_code_span other_where) noexcept
        : kind(kind), where(where), other_where(other_where) {}

    explicit error(error_kind kind, source_code_span where,
                   source_code_span other_where,
                   variable_kind var_kind) noexcept
        : kind(kind),
          where(where),
          other_where(other_where),
          var_kind(var_kind) {}

    error_kind kind;
    source_code_span where;
    union {
      source_code_span other_where;
      static_assert(std::is_trivially_destructible_v<source_code_span>);
    };
    variable_kind var_kind;
  };
  std::vector<error> errors;
};

void PrintTo(const error_collector::error &, std::ostream *);
}

QLJS_WARNING_POP

#endif
