#ifndef EIGHT_BIT_DISASSEMBLER_H
#define EIGHT_BIT_DISASSEMBLER_H

#include <cstdint>
#include <optional>
#include <span>
#include <stack>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "instructions6301.h"

namespace eight_bit {

class Disassembler {
 public:
  Disassembler();
  ~Disassembler() = default;

  // Add data to disassemble
  absl::Status set_data(uint16_t start_address, std::vector<uint8_t> data);

  // Tell the disassembler that 'address' is the start of an instruction.
  // Disassembles that instruction during the call, but if more needs to be
  // disassembled, call 'disassemble'.
  void set_instruction_boundary_hint(uint16_t address);

  // Begin (or continue) disassembly after calls to set_data or
  // set_instruction_boundary_hint. Returns an error if nothing could be
  // disassembled.
  absl::Status disassemble();

  // Print the disassembled data at 'address'.
  std::string print(uint16_t address);

  void print();

  const std::vector<std::string>& disassembly() const;

 private:
  struct Annotation {
    // Bytes representing an address, annotated on the first byte
    struct CodeAddress {
      uint16_t address;
    };
    struct DataAddress {
      uint16_t address;
    };
    struct InstructionAnnotation {
      Instruction instruction;
      std::optional<uint16_t> destination_address = std::nullopt;
    };

    std::optional<CodeAddress> code_address = std::nullopt;
    std::optional<DataAddress> data_address = std::nullopt;
    std::optional<InstructionAnnotation> instruction = std::nullopt;
    std::optional<std::string> label = std::nullopt;

    // Whether the data at this address has been fully decoded
    bool decoded = false;
    // Whether this address should be skipped when printing
    bool skip = false;
    // The bytes this decoding is made of
    std::vector<uint8_t> data = {};
  };

  uint8_t get(uint16_t address) const;
  uint16_t get16(uint16_t address);

  // Check if the address has valid data (i.e. an annotation is set)
  bool check(uint16_t address) const;
  // Like check, but checks for two bytes
  bool check16(uint16_t address) const;

  // Set the label for the address.
  void update_label(uint16_t address, std::string_view label);

  // Use known 6301 vectors to identify instruction boundaries. Returns an error
  // if the vectors are unset.
  absl::Status decode_vectors();

  void decode_instruction(uint16_t address);

  std::string print_annotation(uint16_t address);
  std::string print_code_address(uint16_t address);
  std::string print_data_address(uint16_t address);
  std::string print_instruction(uint16_t address);
  std::string print_data(uint16_t address);

  // Memory holds the data to disassemble
  std::vector<uint8_t> memory_;
  // annotations_ holds the annotations for each byte in memory_. Unset bytes
  // were not set in memory_.
  std::vector<std::optional<Annotation>> annotations_;
  std::vector<std::string> disassembly_;

  std::stack<uint16_t> worklist_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_DISASSEMBLER_H