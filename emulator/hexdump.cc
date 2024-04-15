#include "hexdump.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

namespace eight_bit {

std::string hexdump(const std::vector<uint8_t>& data) {
  std::string output;
  const int bytes_per_line = 16;

  std::vector<uint8_t> prev_line(bytes_per_line, 0);
  bool is_repeated = true;

  for (std::size_t i = 0; i < data.size(); i += bytes_per_line) {
    // Check if the current line is repeated
    if (i != 0 && i + bytes_per_line <= data.size() &&
        std::equal(data.begin() + i, data.begin() + i + bytes_per_line,
                   prev_line.begin())) {
      if (!is_repeated) {
        is_repeated = true;
        absl::StrAppend(&output, "*\n");
      }
      continue;
    }
    is_repeated = false;

    // Copy current line to prev_line
    std::copy(data.begin() + i, data.begin() + i + bytes_per_line,
              prev_line.begin());

    // Print offset
    absl::StrAppend(&output, absl::Hex(i, absl::kZeroPad8), "  ");

    // Print bytes in hexadecimal format
    for (size_t j = 0; j < bytes_per_line; ++j) {
      if (j == 8) {
        absl::StrAppend(&output, " ");
      }
      if (i + j < data.size()) {
        absl::StrAppend(&output, absl::Hex(data[i + j], absl::kZeroPad2), " ");
      } else {
        absl::StrAppend(&output, "   ");
      }
    }

    absl::StrAppend(&output, " |");

    // Print bytes in ASCII format
    for (size_t j = 0; j < bytes_per_line && i + j < data.size(); ++j) {
      char d = data[i + j];
      if (d >= 32 && d <= 126) {
        absl::string_view c(&d, 1);
        absl::StrAppend(&output, c);
      } else {
        absl::StrAppend(&output, ".");
      }
    }

    absl::StrAppend(&output, "|\n");
  }
  return output;
}

}  // namespace eight_bit