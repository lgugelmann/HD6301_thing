#ifndef EIGHT_BIT_INSTRUCTION_TEST_PARSER_H
#define EIGHT_BIT_INSTRUCTION_TEST_PARSER_H

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace eight_bit {

struct InstructionTestCase {
  // Source info.
  std::string file;
  std::string name;

  // Instruction bytes placed at PC.
  std::vector<uint8_t> memory;

  // Initial CPU state (with defaults).
  uint8_t a = 0x00;
  uint8_t b = 0x00;
  uint16_t x = 0x0000;
  uint16_t sp = 0xF000;
  uint16_t pc = 0x0020;
  uint8_t sr = 0xC0;

  // Pre-loaded memory at specific addresses.
  std::map<uint16_t, std::vector<uint8_t>> memory_at;

  // Expected post-execution state (only specified values are checked).
  std::optional<uint8_t> expect_a, expect_b, expect_sr;
  std::optional<uint16_t> expect_x, expect_sp, expect_pc;
  std::optional<bool> expect_H, expect_I, expect_N, expect_Z, expect_V,
      expect_C;

  // Expected memory values at specific addresses.
  std::map<uint16_t, uint8_t> expect_at;

  // Expected cycle count.
  std::optional<int> cycles;
};

// Parses all test cases from a .test file. Returns an error string on failure.
struct ParseResult {
  std::vector<InstructionTestCase> cases;
  std::string error;
};
ParseResult ParseTestFile(const std::filesystem::path& path);

// Discovers and parses all .test files in the given directory.
ParseResult ParseAllTestFiles(const std::filesystem::path& directory);

// Compact printer so GoogleTest/CTest don't dump raw struct bytes.
void PrintTo(const InstructionTestCase& tc, std::ostream* os);

}  // namespace eight_bit

#endif  // EIGHT_BIT_INSTRUCTION_TEST_PARSER_H
