#include <cstdint>
#include <fstream>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "disassembler.h"
#include "instructions6301.h"

ABSL_FLAG(std::string, rom_file, "", "Path to the ROM file to disassemble");

int main(int argc, char* argv[]) {
  absl::ParseCommandLine(argc, argv);
  absl::InitializeLog();

  QCHECK(!absl::GetFlag(FLAGS_rom_file).empty()) << "No ROM file specified.";
  const std::string rom_file_name = absl::GetFlag(FLAGS_rom_file);
  std::ifstream rom_file(rom_file_name, std::ios::binary);
  QCHECK(rom_file.is_open()) << "Failed to open file: " << rom_file_name;
  std::vector<uint8_t> rom_data(std::istreambuf_iterator<char>(rom_file), {});

  eight_bit::Disassembler disassembler;
  QCHECK_OK(disassembler.set_data(0x10000 - rom_data.size(), rom_data));
  QCHECK_OK(disassembler.disassemble());
  disassembler.print();

  return 0;
}