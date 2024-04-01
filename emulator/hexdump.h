#ifndef EIGHT_BIT_HEXDUMP_H
#define EIGHT_BIT_HEXDUMP_H

#include <cstdint>
#include <string>
#include <vector>

namespace eight_bit {

std::string hexdump(const std::vector<uint8_t>& data);

}  // namespace eight_bit

#endif  // EIGHT_BIT_HEXDUMP_H