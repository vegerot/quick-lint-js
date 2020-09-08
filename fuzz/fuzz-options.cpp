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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <memory>
#include <quick-lint-js/narrow-cast.h>
#include <quick-lint-js/options.h>
#include <type_traits>
#include <vector>

namespace quick_lint_js {
namespace {
class reader {
 public:
  explicit reader(const std::uint8_t *data, std::size_t data_size) noexcept
      : data_(data), data_remaining_(data_size) {}

  bool done() const noexcept { return this->data_remaining_ == 0; }

  template <class T>
  T read() noexcept {
    static_assert(std::is_trivially_constructible_v<T>);

    std::array<std::uint8_t, sizeof(int)> result_bytes = {};

    std::size_t bytes_to_read =
        std::min(result_bytes.size(), this->data_remaining_);
    std::memcpy(result_bytes.data(), this->data_, bytes_to_read);
    this->data_ += bytes_to_read;
    this->data_remaining_ -= bytes_to_read;

    int result;
    std::memcpy(&result, result_bytes.data(), sizeof(result));
    return result;
  };

  void read_chars(char *out, std::size_t count) noexcept {
    std::size_t bytes_to_read = std::min(count, this->data_remaining_);
    std::memcpy(out, this->data_, bytes_to_read);
    this->data_ += bytes_to_read;
    this->data_remaining_ -= bytes_to_read;
  }

 private:
  const std::uint8_t *data_;
  std::size_t data_remaining_;
};
}
}

extern "C" {
int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size) {
  quick_lint_js::reader r(data, size);

  std::vector<std::unique_ptr<char[]>> all_args;
  std::vector<char *> argv;

  while (!r.done() && argv.size() < 1024) {
    std::size_t arg_length = std::min(r.read<std::size_t>(), std::size_t(1024));
    std::unique_ptr<char[]> arg = std::make_unique<char[]>(arg_length + 1);
    r.read_chars(arg.get(), arg_length);
    arg[arg_length] = '\0';
    argv.push_back(arg.get());
    all_args.emplace_back(std::move(arg));
  }

  quick_lint_js::options o = quick_lint_js::parse_options(
      quick_lint_js::narrow_cast<int>(argv.size()), argv.data());
  static_cast<void>(o);

  return 0;
}
}
