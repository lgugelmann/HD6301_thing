#include "address_space.h"

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace eight_bit {

uint8_t AddressSpace::get(uint16_t address) { return data_[address]; }

uint16_t AddressSpace::get16(uint16_t address) {
  if (address <= 0xfffe) {
    return (uint16_t)data_[address] << 8 | (uint16_t)data_[address + 1];
  }
  return 0;
}

void AddressSpace::set(uint16_t address, uint8_t data) {
  data_[address] = data;
}

void AddressSpace::set16(uint16_t address, uint16_t data) {
  data_[address] = data >> 8;
  data_[address + 1] = data;
}

void AddressSpace::load(uint16_t address, std::span<uint8_t> data) {
  if (address + data.size() > data_.size()) {
    return;
  }
  for (const auto d : data) {
    data_[address++] = d;
  }
}

void AddressSpace::hexdump() {
  const int bytes_per_line = 16;

  std::vector<uint8_t> prev_line(bytes_per_line, 0);
  bool is_repeated = true;

  for (size_t i = 0; i < data_.size(); i += bytes_per_line) {
    // Check if the current line is repeated
    if (i != 0 && i + bytes_per_line <= data_.size() &&
        std::equal(data_.begin() + i, data_.begin() + i + bytes_per_line,
                   prev_line.begin())) {
      if (!is_repeated) {
        is_repeated = true;
        printf("*\n");
      }
      continue;
    } else {
      is_repeated = false;
    }

    // Copy current line to prev_line
    std::copy(data_.begin() + i, data_.begin() + i + bytes_per_line,
              prev_line.begin());

    // Print offset
    printf("%08zx  ", i);

    // Print bytes in hexadecimal format
    for (size_t j = 0; j < bytes_per_line; ++j) {
      if (j == 8) {
        printf(" ");
      }
      if (i + j < data_.size()) {
        printf("%02x ", static_cast<int>(data_[i + j]));
      } else {
        printf("   ");
      }
    }

    printf(" |");

    // Print bytes in ASCII format
    for (size_t j = 0; j < bytes_per_line && i + j < data_.size(); ++j) {
      char c = static_cast<char>(data_[i + j]);
      if (c >= 32 && c <= 126) {
        printf("%c", c);
      } else {
        printf(".");
      }
    }

    printf("|\n");
  }
}

}  // namespace eight_bit
