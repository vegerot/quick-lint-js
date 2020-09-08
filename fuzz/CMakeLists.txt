# quick-lint-js finds bugs in JavaScript programs.
# Copyright (C) 2020  Matthew Glazar
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

cmake_minimum_required(VERSION 3.10)
include(QuickLintJSTarget)

quick_lint_js_add_executable(
  quick-lint-js-fuzz-options
  fuzz-options.cpp
)
target_link_libraries(quick-lint-js-fuzz-options PRIVATE quick-lint-js-lib)

# @@@
target_compile_options(quick-lint-js-fuzz-options PRIVATE -fsanitize=fuzzer)
target_link_libraries(quick-lint-js-fuzz-options PRIVATE -fsanitize=fuzzer)

quick_lint_js_add_executable(
  quick-lint-js-fuzz-parse
  fuzz-parse.cpp
)
target_link_libraries(quick-lint-js-fuzz-parse PRIVATE quick-lint-js-lib)

# @@@
target_compile_options(quick-lint-js-fuzz-parse PRIVATE -fsanitize=fuzzer)
target_link_libraries(quick-lint-js-fuzz-parse PRIVATE -fsanitize=fuzzer)
