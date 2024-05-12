#include "disassembler.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "absl/cleanup/cleanup.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace eight_bit {
namespace {

struct CompileResult {
  uint16_t start_address;
  std::vector<uint8_t> data;
};

absl::StatusOr<CompileResult> Compile(std::string_view code) {
  // Create some temp files
  // TODO: find a better way to do this. Right now this doesn't allow running
  //       tests in parallel for example.
  std::filesystem::path input_file_path =
      std::filesystem::temp_directory_path() / "disassembler_test_input.s";
  std::filesystem::path output_file_path =
      std::filesystem::temp_directory_path() / "disassembler_test_output.p";
  std::filesystem::path bin_file_path =
      std::filesystem::temp_directory_path() / "disassembler_test_output.bin";

  absl::Cleanup cleanup([&input_file_path, &output_file_path, &bin_file_path] {
    std::filesystem::remove(input_file_path);
    std::filesystem::remove(output_file_path);
    std::filesystem::remove(bin_file_path);
  });

  std::ofstream input_file(input_file_path);
  input_file << code;
  input_file.close();

  // Run the asl command
  auto command = absl::StrCat("asl -q -cpu 6301 ", input_file_path.string(),
                              " -o ", output_file_path.string());
  int ret = std::system(command.c_str());
  if (ret != 0) {
    return absl::InternalError("Failed to compile the code");
  }
  // Convert the output to binary using p2bin
  command = absl::StrCat("p2bin -q ", output_file_path.string(), " ",
                         bin_file_path.string());
  ret = std::system(command.c_str());
  if (ret != 0) {
    return absl::InternalError("Failed to convert the output to binary");
  }

  std::ifstream bin_file(bin_file_path, std::ios::binary);
  if (!bin_file) {
    return absl::InternalError("Failed to open the binary file");
  }
  CompileResult result{
      .start_address = 0,
      .data = std::vector<uint8_t>(std::istreambuf_iterator<char>(bin_file),
                                   std::istreambuf_iterator<char>()),
  };
  bin_file.close();

  return result;
}

absl::Status CompileAndDisassemble(std::string_view code,
                                   Disassembler& disassembler) {
  auto compiled = Compile(code);
  if (!compiled.ok()) {
    return compiled.status();
  }

  auto status = disassembler.set_data(compiled->start_address, compiled->data);
  if (!status.ok()) {
    return status;
  }

  disassembler.set_instruction_boundary_hint(compiled->start_address);
  status = disassembler.disassemble();

  std::cout << "-------Full disassembly-------------\n";
  disassembler.print();
  std::cout << "------------------------------------\n";

  return status;
}

TEST(DisassemblerTest, BraDestinationHasLabelAndBraUsesIt) {
  auto code = R"(
loop:
    lda #0
    bra loop
  )";

  Disassembler disassembler;
  ASSERT_TRUE(CompileAndDisassemble(code, disassembler).ok());

  EXPECT_THAT(disassembler.print(0), testing::HasSubstr("loc_0000:"));
  EXPECT_THAT(disassembler.print(2), testing::HasSubstr("bra  loc_0000"));
}

class DisassemblerTest
    : public ::testing::TestWithParam<std::filesystem::path> {};

TEST_P(DisassemblerTest, RoundTripTest) {
  std::ifstream test_file(GetParam());
  std::string code((std::istreambuf_iterator<char>(test_file)),
                   std::istreambuf_iterator<char>());

  // Compile the test code
  auto compiled = Compile(code);
  ASSERT_TRUE(compiled.ok()) << compiled.status();

  // Disassemble the compiled code
  Disassembler disassembler;
  auto status = disassembler.set_data(compiled->start_address, compiled->data);
  ASSERT_TRUE(status.ok()) << status;

  disassembler.set_instruction_boundary_hint(compiled->start_address);
  status = disassembler.disassemble();
  ASSERT_TRUE(status.ok()) << status;
  auto disassembly = absl::StrJoin(disassembler.disassembly(), "\n");

  // Recompile the disassembled code
  auto recompiled = Compile(disassembly);
  ASSERT_TRUE(recompiled.ok()) << recompiled.status();

  // Compare whether the two compiled versions are the same
  EXPECT_EQ(compiled->data, recompiled->data);
}

INSTANTIATE_TEST_SUITE_P(
    TestWithFiles, DisassemblerTest, ::testing::ValuesIn([]() {
      std::vector<std::filesystem::path> paths;
      std::cout << "Current path: " << std::filesystem::current_path();
      for (const auto& entry :
           std::filesystem::directory_iterator(TESTDATA_DIR)) {
        if (entry.path().extension() == ".s") {
          paths.push_back(entry.path());
        }
      }
      return paths;
    }()));

}  // namespace
}  // namespace eight_bit