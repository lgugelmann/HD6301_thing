#include "disassembler.h"

#include <cstdint>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <stack>
#include <string>
#include <vector>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "instructions6301.h"

namespace eight_bit {

Disassembler::Disassembler()
    : memory_(0x10000, 0),
      annotations_(0x10000, std::nullopt),
      disassembly_(0x10000) {}

absl::Status Disassembler::set_data(uint16_t start_address,
                                    std::span<uint8_t> data) {
  if (start_address + data.size() - 1 > memory_.size()) {
    return absl::InvalidArgumentError("Data does not fit in memory");
  }
  for (size_t i = start_address; i < start_address + data.size(); ++i) {
    memory_[i] = data[i - start_address];
    annotations_[i] = Annotation{};
  }
  return absl::OkStatus();
}

void Disassembler::set_instruction_boundary_hint(uint16_t address) {
  if (annotations_[address]->instruction.has_value()) {
    return;
  }
  annotations_[address]->instruction = {.instruction = Instruction{}};
  decode_instruction(address);
  disassembly_[address] = print_annotation(address);
}

absl::Status Disassembler::disassemble() {
  auto status = decode_vectors();
  if (!status.ok()) {
    return status;
  }
  while (!worklist_.empty()) {
    uint16_t address = worklist_.top();
    worklist_.pop();
    if (annotations_[address]->decoded) {
      continue;
    }
    if (annotations_[address]->instruction.has_value()) {
      decode_instruction(address);
    }
    annotations_[address]->decoded = true;
    disassembly_[address] = print_annotation(address);
  }
  print_unannotated();
  return absl::OkStatus();
}

std::string Disassembler::print_line(uint16_t address) {
  return disassembly_[address];
}

std::string Disassembler::print() {
  std::string disassembly;
  bool previous_has_value = false;
  for (size_t i = 0; i < memory_.size(); ++i) {
    if (annotations_[i].has_value()) {
      if (!previous_has_value) {
        // This is an org boundary.
        absl::StrAppend(&disassembly, "        org  $",
                        absl::Hex(i, absl::kZeroPad4), "\n");
        previous_has_value = true;
      }
      absl::StrAppend(&disassembly, disassembly_[i]);
    }
  }
  return disassembly;
}

const std::vector<std::string>& Disassembler::disassembly() const {
  return disassembly_;
}

uint8_t Disassembler::get(uint16_t address) const { return memory_[address]; }

uint16_t Disassembler::get16(uint16_t address) {
  if (address > 0xfffe) {
    LOG(ERROR) << absl::StreamFormat("Invalid 16-bit read from address %04x",
                                     address);
    return 0;
  }
  return (uint16_t)get(address) << 8 | (uint16_t)get(address + 1);
}

bool Disassembler::check(uint16_t address) const {
  return annotations_[address].has_value();
}

bool Disassembler::check16(uint16_t address) const {
  if (address + 1u >= memory_.size()) {
    return false;
  }
  return annotations_[address].has_value() &&
         annotations_[address + 1].has_value();
}

void Disassembler::update_label(uint16_t address, std::string_view label) {
  // We shouldn't update labels for addresses that have them already until we
  // implement a way to go fix the references.
  if (!annotations_[address]->label.has_value()) {
    annotations_[address]->label = std::string(label);
    disassembly_[address] = print_annotation(address);
  }
}

absl::Status Disassembler::decode_vectors() {
  const auto vectors = std::vector<std::pair<std::string, uint16_t>>{
      {"start", 0xfffe},
      {"nmi", 0xfffc},
      {"swi", 0xfffa},
      {"irq", 0xfff8},
      {"timer_input_capture", 0xfff6},
      {"timer_output_compare", 0xfff4},
      {"timer_overflow", 0xfff2},
      {"sci", 0xfff0},
  };

  for (const auto& [label, address] : vectors) {
    if (!check16(address)) {
      continue;
    }
    if (annotations_[address]->code_address.has_value()) {
      // We already decoded at least one vector - likely this is a second
      // disassembly pass.
      continue;
    }
    uint16_t destination = get16(address);
    if (!check(destination)) {
      continue;
    }
    annotations_[address]->code_address = {.address = destination};
    if (address == 0xfff0) {
      annotations_[address]->label = "startup_vectors";
    }
    annotations_[address]->decoded = true;
    annotations_[address + 1]->skip = true;
    disassembly_[address] = print_annotation(address);

    annotations_[destination]->instruction = {.instruction = Instruction{}};
    annotations_[destination]->label = absl::StrCat("vec_", label);
    worklist_.push(destination);
  }
  return absl::OkStatus();
}

void Disassembler::decode_instruction(uint16_t address) {
  uint8_t opcode = get(address);
  const Instruction& instruction = kInstructions6301[opcode];
  if (instruction.mode == kILL) {
    LOG(ERROR) << absl::StreamFormat("Illegal instruction %02x at address %04x",
                                     opcode, address);
    annotations_[address]->instruction.reset();
    return;
  }
  if (address + instruction.bytes - 1u >= memory_.size()) {
    LOG(ERROR) << absl::StreamFormat(
        "Instruction %s at address %04x is too long", instruction.name,
        address);
    annotations_[address]->instruction.reset();
    return;
  }
  annotations_[address]->instruction = {.instruction = instruction};
  uint8_t length = instruction.bytes;
  for (uint8_t i = 1; i < length; ++i) {
    if (!check(address + i)) {
      LOG(ERROR) << absl::StreamFormat("Missing byte at %04x", address + i);
      return;
    }
  }
  std::vector<uint8_t> data(memory_.begin() + address,
                            memory_.begin() + address + length);
  annotations_[address]->data = data;
  annotations_[address]->decoded = true;
  for (uint8_t i = 1; i < length; ++i) {
    annotations_[address + i] = {.decoded = true, .skip = true};
  }

  // Handle instructions whose operands move the program counter - except
  // indexed as we can't compute the value of X.
  const std::set<std::string> dest_is_code = {"jmp", "jsr"};
  if (instruction.mode != AddressingMode::kIDX &&
      (instruction.mode == AddressingMode::kREL ||
       dest_is_code.contains(instruction.name))) {
    bool have_destination = true;
    uint16_t destination = 0;
    switch (instruction.mode) {
      case kREL:
        destination = address + 2 + (int8_t)data[1];
        break;
      case kDIR:
        destination = data[1];
        break;
      case kEXT:
        destination = (uint16_t)data[1] << 8 | data[2];
        break;
      default:
        have_destination = false;
        break;
    }
    if (have_destination && check(destination)) {
      if (!annotations_[destination]->instruction.has_value()) {
        annotations_[destination]->instruction = {.instruction = Instruction{}};
        worklist_.push(destination);
      }
      if (!annotations_[destination]->label.has_value()) {
        update_label(
            destination,
            absl::StrCat("loc_", absl::Hex(destination, absl::kZeroPad4)));
      }
      annotations_[address]->instruction->destination_address = destination;
    }
  }

  // The next bytes are also an instruction, except for always-taken branches,
  // jumps, and returns.
  const std::set<std::string> next_is_not_always_code = {"bra", "jmp", "rts",
                                                         "rti"};
  uint16_t next = address + length;
  if (!next_is_not_always_code.contains(instruction.name) && check(next)) {
    if (!annotations_[next]->instruction.has_value()) {
      annotations_[next]->instruction = {.instruction = Instruction{}};
      worklist_.push(next);
    }
  }
}

std::string Disassembler::print_annotation(uint16_t address) {
  if (!check(address) || annotations_[address]->skip) {
    return "";
  }
  std::string annotation;
  if (annotations_[address]->label.has_value()) {
    absl::StrAppend(
        &annotation,
        absl::StrFormat("%s:\n", annotations_[address]->label.value()));
  }
  if (annotations_[address]->instruction.has_value()) {
    absl::StrAppend(&annotation, print_instruction(address));
    return annotation;
  }
  if (annotations_[address]->code_address.has_value()) {
    absl::StrAppend(&annotation, print_code_address(address));
    return annotation;
  }
  if (annotations_[address]->data_address.has_value()) {
    absl::StrAppend(&annotation, print_data_address(address));
    return annotation;
  }
  absl::StrAppend(&annotation, print_data(address));
  return annotation;
}

std::string Disassembler::print_instruction(uint16_t address) {
  if (!annotations_[address]->instruction.has_value()) {
    LOG(ERROR) << absl::StreamFormat(
        "Unexpected call to print_instruction at %04x", address);
    return "";
  }
  const auto& instruction =
      annotations_[address]->instruction.value().instruction;
  const auto& data = annotations_[address]->data;
  const auto& destination =
      annotations_[address]->instruction.value().destination_address;
  std::string destination_label;
  if (destination.has_value()) {
    uint16_t dest = destination.value();
    if (annotations_[dest]->label.has_value()) {
      destination_label = annotations_[dest]->label.value();
    }
  }
  std::string byte_string =
      absl::StrJoin(data, " ", [](std::string* out, uint8_t byte) {
        absl::StrAppend(out, absl::Hex(byte, absl::kZeroPad2));
      });
  uint16_t operand = 0;
  if (data.size() == 2) {
    operand = data[1];
  } else if (data.size() == 3) {
    operand = data[1] << 8 | data[2];
  }
  std::string disassembly;
  switch (instruction.mode) {
    case kACA:
    case kACB:
    case kACD:
    case kIMP:
      disassembly =
          absl::StrFormat("        %-4s %-16s; %04x: %s\n", instruction.name,
                          " ", address, byte_string);
      break;
    case kIMM:
      disassembly =
          absl::StrFormat("        %-4s #$%02x            ; %04x: %s\n",
                          instruction.name, operand, address, byte_string);
      break;
    case kIM2:
      disassembly =
          absl::StrFormat("        %-4s #$%04x          ; %04x: %s\n",
                          instruction.name, operand, address, byte_string);
      break;
    case kREL:
    case kDIR:
      if (destination_label.empty()) {
        destination_label = absl::StrFormat("$%02x", operand);
      }
      disassembly =
          absl::StrFormat("        %-4s %-16s; %04x: %s\n", instruction.name,
                          destination_label, address, byte_string);
      break;
    case kEXT:
      if (destination_label.empty()) {
        destination_label = absl::StrFormat("$%04x", operand);
      }
      disassembly =
          absl::StrFormat("        %-4s %-16s; %04x: %s\n", instruction.name,
                          destination_label, address, byte_string);
      break;
    case kIDX:
      disassembly =
          absl::StrFormat("        %-4s $%02x,X           ; %04x: %s\n",
                          instruction.name, operand, address, byte_string);
      break;
    case kIDXBIT:
      disassembly = absl::StrFormat(
          "        %-4s #$%02x,$%02x,X      ; %04x: %s\n", instruction.name,
          data[1], data[2], address, byte_string);
      break;
    case kDIRBIT:
      disassembly = absl::StrFormat(
          "        %-4s #$%02x,$%02x        ; %04x: %s\n", instruction.name,
          data[1], data[2], address, byte_string);
      break;
    case kILL:
      disassembly = absl::StrFormat(
          "        byt  $%02x     ; Illegal instruction\n", data[0]);
      break;
  }
  return disassembly;
}

std::string Disassembler::print_code_address(uint16_t address) {
  if (!annotations_[address]->code_address.has_value()) {
    LOG(ERROR) << absl::StreamFormat(
        "Unexpected call to print_code_address at %04x", address);
    return "";
  }
  uint16_t destination = annotations_[address]->code_address->address;
  return absl::StrFormat("        adr $%04x            ; %04x: %02x %02x\n",
                         destination, address, destination >> 8,
                         destination % 256);
}

std::string Disassembler::print_data_address(uint16_t address) {
  if (!annotations_[address]->data_address.has_value()) {
    LOG(ERROR) << absl::StreamFormat(
        "Unexpected call to print_data_address at %04x", address);
    return "";
  }
  uint16_t destination = annotations_[address]->data_address->address;
  return absl::StrFormat("        adr $%04x            ; %04x: %02x %02x\n",
                         destination, address, destination >> 8,
                         destination % 256);
}

std::string Disassembler::print_data(uint16_t address) {
  const auto& data = annotations_[address]->data;
  std::string data_string;
  for (size_t i = 0; i < data.size(); i += 8) {
    std::ranges::subrange range{
        data.data() + i,
        data.data() + i + std::min<size_t>(8, data.size() - i)};

    absl::StrAppendFormat(
        &data_string, "        byt %-20s ; %04x\n",
        absl::StrJoin(range, ",",
                      [](std::string* out, uint8_t byte) {
                        absl::StrAppend(out, absl::Hex(byte, absl::kZeroPad2));
                      }),
        address + i);
  }
  return data_string;
}

// Go over bytes that are set but not annotated and print them as data.
void Disassembler::print_unannotated() {
  constexpr int kMaxBytesPerLine = 8;
  for (size_t i = 0; i < memory_.size(); ++i) {
    if (!annotations_[i].has_value() || annotations_[i]->skip ||
        annotations_[i]->decoded) {
      continue;
    }
    std::string data_string = "        byt  ";
    // Look ahead up to kMaxBytesPerLine bytes to see if they can be merged into
    // this line.
    size_t j = i;
    for (; j < i + kMaxBytesPerLine && j < memory_.size(); ++j) {
      if (annotations_[j].has_value() &&
          (annotations_[j]->skip || annotations_[j]->decoded)) {
        break;
      }
      absl::StrAppendFormat(&data_string, "%s$%02x", j == i ? "" : ",",
                            memory_[j]);
    }
    absl::StrAppendFormat(&data_string, " ; %04x\n", i);
    disassembly_[i] = data_string;
    i = j - 1;
  }
}

}  // namespace eight_bit
