#include "hexdump.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"

namespace eight_bit {

std::string hexdump(std::span<const uint8_t> data, unsigned int base_address) {
  std::string output;
  const int bytes_per_line = 16;

  std::vector<uint8_t> prev_line(bytes_per_line, 0);
  bool is_repeated = true;

  // We want to have everything aligned at bytes_per_line boundaries. We may
  // need to offset the data at the beginning of the first line due to
  // non-aligned base_address.
  int bytes_to_offset = base_address % bytes_per_line;

  for (int i = 0; i < data.size(); i += bytes_per_line) {
    // Check if the current line is repeated. The first condition makes sure
    // that the first line is not marked as repeated, or the first two if we
    // have an offset.
    if (i >= bytes_per_line && i + bytes_per_line <= data.size() &&
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
    if (i + bytes_per_line < data.size()) {
      std::copy(data.begin() + i, data.begin() + i + bytes_per_line,
                prev_line.begin());
    }

    // The index into the data buffer and the 'logical' address that determines
    // where we are in the hexdump can differ if the base_address is not 0.
    int line_address = i + base_address - (i + base_address) % bytes_per_line;
    absl::StrAppend(&output, absl::Hex(line_address, absl::kZeroPad8), "  ");

    for (size_t j = 0; j < bytes_per_line; ++j) {
      if (j == 8) {
        absl::StrAppend(&output, " ");
      }
      if (j >= bytes_to_offset && i + j - bytes_to_offset < data.size()) {
        absl::StrAppend(
            &output, absl::Hex(data[i + j - bytes_to_offset], absl::kZeroPad2),
            " ");
      } else {
        absl::StrAppend(&output, "   ");
      }
    }

    absl::StrAppend(&output, " |");

    // Print bytes in ASCII format
    for (size_t j = 0; j < bytes_per_line; ++j) {
      if (j < bytes_to_offset) {
        absl::StrAppend(&output, " ");
        continue;
      }
      if (i + j - bytes_to_offset >= data.size()) {
        break;
      }
      char d = data[i + j - bytes_to_offset];
      if (d >= 32 && d <= 126) {
        absl::string_view c(&d, 1);
        absl::StrAppend(&output, c);
      } else {
        absl::StrAppend(&output, ".");
      }
    }
    absl::StrAppend(&output, "|\n");

    if (bytes_to_offset > 0) {
      // We didn't print bytes_per_line many bytes, we need to skip fewer bytes
      // for the next iteration.
      i -= bytes_to_offset;
      bytes_to_offset = 0;
    }
  }
  return output;
}

}  // namespace eight_bit