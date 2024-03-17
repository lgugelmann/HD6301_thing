#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

#include "address_space.h"
#include "cpu6301.h"

using eight_bit::AddressSpace;
using eight_bit::Cpu6301;

int main(int argc, char* argv[]) {
  AddressSpace address_space;

  std::ifstream monitor_file("../asm/monitor.bin", std::ios::binary);
  if (!monitor_file.is_open()) {
    fprintf(stderr, "Failed to open monitor file.\n");
    return -1;
  }
  std::vector<uint8_t> monitor(std::istreambuf_iterator<char>(monitor_file),
                               {});
  address_space.load(0x10000 - monitor.size(), monitor);

  Cpu6301 cpu(&address_space);
  cpu.reset();
  while (cpu.tick() == 0) {
  };
  cpu.print_state();
  return 0;
}
