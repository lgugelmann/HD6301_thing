#ifndef EIGHT_BIT_HEXDUMP_H
#define EIGHT_BIT_HEXDUMP_H

#include <cstdint>
#include <span>
#include <string>

namespace eight_bit {

// Returns a hexdump of the given data in the style of 'hexdump -C'. If
// 'base_address' is given, the data is assumed to start at that address.
std::string hexdump(std::span<const uint8_t> data,
                    unsigned int base_address = 0);

}  // namespace eight_bit

#endif  // EIGHT_BIT_HEXDUMP_H