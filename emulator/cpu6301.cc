#include "cpu6301.h"

#include <cstdio>
#include <functional>
#include <string>

#include "absl/log/log.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "address_space.h"
#include "hd6301_serial.h"
#include "interrupt.h"
#include "ioport.h"

namespace eight_bit {

absl::StatusOr<std::unique_ptr<Cpu6301>> Cpu6301::create(AddressSpace* memory) {
  std::unique_ptr<Cpu6301> cpu(new Cpu6301(memory));
  auto status = cpu->initialize();
  if (!status.ok()) {
    return status;
  }
  return cpu;
}

void Cpu6301::reset() {
  pc = memory_->get16(0xfffe);
  LOG(INFO) << "Reset to start at " << absl::Hex(pc, absl::kZeroPad4);
}

Cpu6301::TickResult Cpu6301::tick(int cycles_to_run, bool ignore_breakpoint) {
  int cycles_run = 0;
  while (cycles_run < cycles_to_run) {
    // We are always at instruction boundaries here, so we can check for
    // interrupts.
    if (interrupt_.has_interrupt() & !sr.I) {
      // Moves the PC to the relevant interrupt routine and masks interrupts.
      cycles_run += enter_interrupt(0xfff8);
    }
    if (timer_interrupt_.has_interrupt() & !sr.I) {
      cycles_run += enter_interrupt(0xfff2);
    }
    if (serial_interrupt_.has_interrupt() & !sr.I) {
      cycles_run += enter_interrupt(0xfff0);
    }
    if (!ignore_breakpoint && breakpoint_ && pc == breakpoint_) {
      return {.cycles_run = cycles_run, .breakpoint_hit = true};
    }
    uint8_t opcode = fetch();
    const auto& instruction = instructions_[opcode];
    if (instruction.mode == kILL) {
      LOG(ERROR) << "Invalid instruction: " << absl::Hex(opcode) << " at "
                 << absl::Hex(pc, absl::kZeroPad4);
      reset();
      cycles_run += 1;
      continue;
    }
    for (int i = 0; i < instruction.cycles; ++i) {
      timer_.tick();
      serial_->tick();
      for (const auto& callback : tick_callbacks_) {
        callback();
      }
      cycles_run += 1;
    }
    execute(instruction);
  }
  return {.cycles_run = cycles_run, .breakpoint_hit = false};
}

void Cpu6301::register_tick_callback(std::function<void()> callback) {
  tick_callbacks_.push_back(std::move(callback));
}

void Cpu6301::set_breakpoint(uint16_t address) { breakpoint_ = address; }

void Cpu6301::clear_breakpoint() { breakpoint_.reset(); }

void Cpu6301::print_state() const {
  std::println(std::cout,
               "A: {:02x} B: {:02x} X: {:04x} SP: {:04x} PC: {:04x} CC: "
               "11{:d}{:d}{:d}{:d}{:d}{:d}",
               a, b, x, sp, pc, sr.H, sr.I, sr.N, sr.Z, sr.V, sr.C);
}

Cpu6301::CpuState Cpu6301::get_state() const {
  return CpuState{.a = a,
                  .b = b,
                  .x = x,
                  .sp = sp,
                  .pc = pc,
                  .sr = sr.as_integer(),
                  .breakpoint = breakpoint_};
}

IOPort* Cpu6301::get_port1() { return &port1_; }

IOPort* Cpu6301::get_port2() { return &port2_; }

Interrupt* Cpu6301::get_irq() { return &interrupt_; }

HD6301Serial* Cpu6301::get_serial() { return serial_.get(); }

uint8_t Cpu6301::fetch() {
  uint8_t ret = get(pc);
  ++pc;
  return ret;
}

uint16_t Cpu6301::fetch16() {
  uint16_t ret = get16(pc);
  pc += 2;
  return ret;
}
uint8_t Cpu6301::get(uint16_t address) { return memory_->get(address); }

uint16_t Cpu6301::get16(uint16_t address) { return memory_->get16(address); }

void Cpu6301::set(uint16_t address, uint8_t data) {
  memory_->set(address, data);
}

void Cpu6301::set16(uint16_t address, uint16_t data) {
  memory_->set16(address, data);
}

uint16_t Cpu6301::get_d() const { return (uint16_t)a << 8 | (uint16_t)b; }

uint16_t Cpu6301::set_d(uint16_t d) {
  a = d >> 8;
  b = d;
  return d;
}

//
// Instruction implementations
//
void Cpu6301::mem_op(uint16_t address, void (Cpu6301::*op)(uint8_t&),
                     bool do_set = true) {
  uint8_t data = get(address);
  (this->*op)(data);
  if (do_set) {
    set(address, data);
  }
}

void Cpu6301::set_op(uint16_t address, void (Cpu6301::*op)(uint8_t&)) {
  uint8_t data;
  (this->*op)(data);
  set(address, data);
}

void Cpu6301::tap() { sr = StatusRegister(a); }

void Cpu6301::tpa() { a = sr.as_integer(); }

void Cpu6301::add_z(uint16_t& r, uint16_t data) {
  r += data;
  sr.Z = (r == 0);
}

void Cpu6301::cmp(uint8_t dest, uint8_t mem) {
  uint8_t r = dest - mem;
  bool d7 = dest >> 7;
  bool m7 = mem >> 7;
  bool r7 = r >> 7;
  sr.N = r7;
  sr.Z = (r == 0);
  sr.V = (d7 & !m7 & !r7) | (!d7 & m7 & r7);
  sr.C = (!d7 & m7) | (m7 & r7) | (r7 & !d7);
}

void Cpu6301::cmp16(uint16_t dest, uint16_t mem) {
  uint16_t r = dest - mem;
  bool d15 = dest >> 15;
  bool m15 = mem >> 15;
  bool r15 = r >> 15;
  sr.N = r15;
  sr.Z = (r == 0);
  sr.V = (d15 & !m15 & !r15) | (!d15 & m15 & r15);
  sr.C = (!d15 & m15) | (m15 & r15) | (r15 & !d15);
}

void Cpu6301::sub(uint8_t& dest, uint8_t mem) {
  cmp(dest, mem);  // sets all status flags
  dest -= mem;
}

void Cpu6301::sbc(uint8_t& dest, uint8_t mem) {
  uint8_t carry = sr.C;
  cmp(dest, mem + carry);  // sets all status flags
  dest = dest - mem - carry;
}

void Cpu6301::subd(uint16_t data) {
  uint16_t d = get_d();
  // cmp16() sets all flags correctly
  cmp16(d, data);
  set_d(d - data);
}

void Cpu6301::add(uint8_t& dest, bool carry, uint8_t mem) {
  uint8_t r = dest + mem + carry;
  bool d3 = (dest >> 3) & 1;
  bool m3 = (mem >> 3) & 1;
  bool r3 = (r >> 3) & 1;
  bool d7 = dest >> 7;
  bool m7 = mem >> 7;
  bool r7 = r >> 7;
  dest = r;
  sr.H = (d3 & m3) | (m3 & !r3) | (!r3 & d3);
  sr.N = r7;
  sr.Z = r == 0;
  sr.V = (d7 & m7 & !r7) | (!d7 & !m7 & r7);
  sr.C = (d7 & m7) | (m7 & !r7) | (!r7 & d7);
}

void Cpu6301::addd(uint16_t data) {
  uint16_t d = get_d();
  uint16_t r = d + data;
  bool d15 = d >> 15;
  bool m15 = data >> 15;
  bool r15 = r >> 15;
  set_d(r);
  sr.N = r15;
  sr.Z = r == 0;
  sr.V = (d15 & m15 & !r15) | (!d15 & !m15 & r15);
  sr.C = (d15 & m15) | (m15 & !r15) | (!r15 & d15);
}

void Cpu6301::xgdx() {
  uint16_t d = get_d();
  set_d(x);
  x = d;
}

void Cpu6301::brx(bool branch, uint16_t dest) {
  if (branch) {
    // Note the signed conversion!
    pc = (int16_t)pc + (int8_t)dest;
  }
}

uint8_t Cpu6301::pul8() {
  sp += 1;
  return get(sp);
}

uint16_t Cpu6301::pul16() {
  sp += 2;
  return get16(sp - 1);
}

void Cpu6301::psh8(uint8_t data) {
  set(sp, data);
  sp -= 1;
}

void Cpu6301::psh16(uint16_t data) {
  set(sp, data);
  set(sp - 1, data >> 8);
  sp -= 2;
}

void Cpu6301::rti() {
  uint8_t new_sr = pul8();
  sr = StatusRegister(new_sr);
  b = pul8();
  a = pul8();
  x = pul16();
  pc = pul16();
}

void Cpu6301::mul() {
  uint16_t r = (uint16_t)a * (uint16_t)b;
  a = r >> 8;
  b = r;
  sr.C = b >> 7;  // b7 of B
}

void Cpu6301::neg(uint8_t& dest) {
  dest = -dest;
  sr.N = (dest & 0x80);
  sr.Z = dest == 0;
  sr.V = dest == 0x80;
  sr.C = !sr.Z;
}

void Cpu6301::nzv_sr(uint8_t result) {
  sr.N = (result & 0x80);
  sr.Z = result == 0;
  sr.V = 0;
}

void Cpu6301::nzv_sr16(uint16_t result) {
  sr.N = (result & 0x8000);
  sr.Z = result == 0;
  sr.V = 0;
}

void Cpu6301::logic(uint8_t data,
                    const std::function<uint8_t(uint8_t, uint8_t)>& op,
                    uint8_t& dest) {
  dest = op(dest, data);
  nzv_sr(dest);
}

void Cpu6301::logic_m(uint16_t address, uint8_t data,
                      const std::function<uint8_t(uint8_t, uint8_t)>& op,
                      bool do_set = true) {
  uint8_t dest = get(address);
  logic(data, op, dest);
  if (do_set) {
    set(address, dest);
  }
}

void Cpu6301::com(uint8_t& dest) {
  dest = ~dest;
  sr.N = (dest & 0x80);
  sr.Z = dest == 0;
  sr.V = 0;
  sr.C = 1;
}

void Cpu6301::rot_flags(uint8_t result, bool carry) {
  sr.N = result & 0x80;
  sr.Z = result == 0;
  sr.V = sr.N ^ carry;
  sr.C = carry;
}

void Cpu6301::rot_flags16(uint16_t result, bool carry) {
  sr.N = result & 0x8000;
  sr.Z = result == 0;
  sr.V = sr.N ^ carry;
  sr.C = carry;
}

void Cpu6301::lsr(uint8_t& dest) {
  bool carry = dest & 1;
  dest = dest >> 1;
  rot_flags(dest, carry);
}

void Cpu6301::lsrd(uint16_t dest) {
  bool carry = dest & 1;
  uint16_t result = dest >> 1;
  set_d(result);
  rot_flags16(result, carry);
}

void Cpu6301::ror(uint8_t& dest) {
  bool carry = dest & 1;
  dest = dest >> 1 | (uint8_t)sr.C << 7;
  rot_flags(dest, carry);
}

void Cpu6301::rol(uint8_t& dest) {
  bool carry = dest & 0x80;
  dest = dest << 1 | (uint8_t)sr.C;
  rot_flags(dest, carry);
}

void Cpu6301::asr(uint8_t& dest) {
  bool carry = dest & 1;
  dest = (dest & 0x80) | dest >> 1;
  rot_flags(dest, carry);
}

void Cpu6301::asl(uint8_t& dest) {
  bool carry = dest & 0x80;
  dest = dest << 1;
  rot_flags(dest, carry);
}

void Cpu6301::asld(uint16_t dest) {
  bool carry = dest & 0x8000;
  uint16_t result = dest << 1;
  set_d(result);
  rot_flags16(result, carry);
}

void Cpu6301::dec(uint8_t& dest) {
  sr.V = dest == 0x80;
  dest -= 1;
  sr.N = dest & 0x80;
  sr.Z = dest == 0;
}

void Cpu6301::inc(uint8_t& dest) {
  sr.V = dest == 0x7f;
  dest += 1;
  sr.N = dest & 0x80;
  sr.Z = dest == 0;
}

void Cpu6301::clr(uint8_t& dest) {
  dest = 0;
  sr.N = 0;
  sr.Z = 1;
  sr.V = 0;
  sr.C = 0;
}

void Cpu6301::bsr(int8_t offset) {
  psh16(pc);
  pc += offset;
}

void Cpu6301::jsr(uint16_t address) {
  psh16(pc);
  pc = address;
}

// Returns the number of cycles taken.
int Cpu6301::enter_interrupt(uint16_t vector) {
  VLOG(3) << "Entering interrupt at " << absl::Hex(vector, absl::kZeroPad4);
  psh16(pc);
  psh16(x);
  psh8(a);
  psh8(b);
  psh8(sr.as_integer());
  // Inhibit further interrupts.
  sr.I = 1;
  pc = memory_->get16(vector);
  // The number of cycles an interrupt takes is not documented - but it's a
  // fair guess that it's as many as there are memory writes.
  return 9;
}

uint8_t Cpu6301::execute(const Instruction& instruction) {
  uint16_t data = 0;
  switch (instruction.mode) {
    case kIMM:
      data = fetch();
      break;
    case kIM2:
      data = (uint16_t)fetch() << 8 | fetch();
      break;
    case kACA:
      data = a;
      break;
    case kACB:
      data = b;
      break;
    case kACD:
      data = (uint16_t)a << 8 | b;
      break;
    case kDIR:
      data = fetch();
      break;
    case kEXT:
      data = fetch16();
      break;
    case kIDX:
      data = x + fetch();
      break;
    case kIMP:
      break;
    case kREL:
      data = fetch();
      break;
    default:
      LOG(ERROR) << "Unhandled addressing mode";
      return -1;
  }
  VLOG(5) << absl::Hex(pc, absl::kZeroPad4) << ": " << instruction.name
          << " data " << absl::Hex(data, absl::kZeroPad4);
  instruction.exec(data);
  return 0;
}

Cpu6301::Cpu6301(AddressSpace* memory)
    : port1_("port1"),
      port2_("port2"),
      timer_(memory, &timer_interrupt_),
      memory_(memory) {
#define COMMA ,
#define OP(expr) [this](uint16_t __attribute__((unused)) d) { expr; }
  instructions_[0x01] = {"nop", 1, 1, kIMP, [](uint16_t) {}};
  instructions_[0x04] = {"lsrd", 1, 1, kACD, OP(lsrd(d))};
  instructions_[0x05] = {"asld", 1, 1, kACD, OP(asld(d))};
  instructions_[0x06] = {"tap", 1, 1, kIMP, OP(tap())};
  instructions_[0x07] = {"tpa", 1, 1, kIMP, OP(tpa())};
  instructions_[0x08] = {"inx", 1, 1, kIMP, OP(add_z(x, 1))};
  instructions_[0x09] = {"dex", 1, 1, kIMP, OP(add_z(x, -1))};
  instructions_[0x0a] = {"clv", 1, 1, kIMP, OP(sr.V = 0)};
  instructions_[0x0b] = {"sev", 1, 1, kIMP, OP(sr.V = 1)};
  instructions_[0x0c] = {"clc", 1, 1, kIMP, OP(sr.C = 0)};
  instructions_[0x0d] = {"sec", 1, 1, kIMP, OP(sr.C = 1)};
  instructions_[0x0e] = {"cli", 1, 1, kIMP, OP(sr.I = 0)};
  instructions_[0x0f] = {"sei", 1, 1, kIMP, OP(sr.I = 1)};
  instructions_[0x10] = {"sba", 1, 1, kACB, OP(sub(a, d))};
  instructions_[0x11] = {"cba", 1, 1, kACB, OP(cmp(a, d))};
  instructions_[0x16] = {"tab", 1, 1, kACA, OP(b = d COMMA nzv_sr(b))};
  instructions_[0x17] = {"tba", 1, 1, kACB, OP(a = d COMMA nzv_sr(a))};
  instructions_[0x18] = {"xgdx", 1, 2, kIMP, OP(xgdx())};
  // instructions_[0x19] = {"daa", 1, 2, kACA, OP() };
  // instructions_[0x1a] = {"slp", 1, 2, kIMP, OP() }; // sleep
  instructions_[0x1b] = {"aba", 1, 1, kACB, OP(add(a, 0, d))};

  // Branching
  instructions_[0x20] = {"bra", 2, 3, kREL, OP(brx(true, d))};
  instructions_[0x21] = {"brn", 2, 3, kREL, OP(brx(false, d))};
  instructions_[0x22] = {"bhi", 2, 3, kREL, OP(brx(sr.C + sr.Z == 0, d))};
  instructions_[0x23] = {"bls", 2, 3, kREL, OP(brx(sr.C + sr.Z == 1, d))};
  instructions_[0x24] = {"bcc", 2, 3, kREL, OP(brx(sr.C == 0, d))};
  instructions_[0x25] = {"bcs", 2, 3, kREL, OP(brx(sr.C == 1, d))};
  instructions_[0x26] = {"bne", 2, 3, kREL, OP(brx(sr.Z == 0, d))};
  instructions_[0x27] = {"beq", 2, 3, kREL, OP(brx(sr.Z == 1, d))};
  instructions_[0x28] = {"bvc", 2, 3, kREL, OP(brx(sr.V == 0, d))};
  instructions_[0x29] = {"bvs", 2, 3, kREL, OP(brx(sr.V == 1, d))};
  instructions_[0x2a] = {"bpl", 2, 3, kREL, OP(brx(sr.N == 0, d))};
  instructions_[0x2b] = {"bmi", 2, 3, kREL, OP(brx(sr.N == 1, d))};
  instructions_[0x2c] = {"bge", 2, 3, kREL, OP(brx((sr.N ^ sr.V) == 0, d))};
  instructions_[0x2d] = {"blt", 2, 3, kREL, OP(brx((sr.N ^ sr.V) == 1, d))};
  instructions_[0x2e] = {"bgt", 2, 3, kREL,
                         OP(brx(sr.Z + (sr.N ^ sr.V) == 0, d))};
  instructions_[0x2f] = {"ble", 2, 3, kREL,
                         OP(brx(sr.Z + (sr.N ^ sr.V) == 1, d))};
  instructions_[0x30] = {"tsx", 1, 1, kIMP, OP(x = sp + 1)};
  instructions_[0x31] = {"ins", 1, 1, kIMP, OP(sp += 1)};
  instructions_[0x32] = {"pula", 1, 3, kIMP, OP(a = pul8())};
  instructions_[0x33] = {"pulb", 1, 3, kIMP, OP(b = pul8())};
  instructions_[0x34] = {"des", 1, 1, kIMP, OP(sp -= 1)};
  instructions_[0x35] = {"txs", 1, 1, kIMP, OP(sp = x - 1)};
  instructions_[0x36] = {"psha", 1, 4, kACA, OP(psh8(d))};
  instructions_[0x37] = {"pshb", 1, 4, kACB, OP(psh8(d))};
  instructions_[0x38] = {"pulx", 1, 4, kIMP, OP(x = pul16())};
  instructions_[0x39] = {"rts", 1, 5, kIMP, OP(pc = pul16())};
  instructions_[0x3a] = {"abx", 1, 1, kIMP, OP(x += b)};
  instructions_[0x3b] = {"rti", 1, 10, kIMP, OP(rti())};
  instructions_[0x3c] = {"pshx", 1, 5, kIMP, OP(psh16(x))};
  instructions_[0x3d] = {"mul", 1, 7, kIMP, OP(mul())};
  // instructions_[0x3e] = {"wai", 1, 9, kIMP, [this](uint16_t) { }};
  // instructions_[0x3f] = {"swi", 1, 12, kIMP, [this](uint16_t) { }};
  // NEG
  instructions_[0x40] = {"nega", 1, 1, kACA, OP(neg(a))};
  instructions_[0x50] = {"negb", 1, 1, kACB, OP(neg(b))};
  instructions_[0x60] = {"neg", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::neg))};
  instructions_[0x70] = {"neg", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::neg))};
  // AIM / OIM / EIM / TIM
  instructions_[0x61] = {"aim", 3, 7, kIMM,
                         OP(logic_m(x + fetch(), d, std::bit_and()))};
  instructions_[0x71] = {"aim", 3, 6, kIMM,
                         OP(logic_m(fetch(), d, std::bit_and()))};
  instructions_[0x62] = {"oim", 3, 7, kIMM,
                         OP(logic_m(x + fetch(), d, std::bit_or()))};
  instructions_[0x72] = {"oim", 3, 6, kIMM,
                         OP(logic_m(fetch(), d, std::bit_or()))};
  instructions_[0x65] = {"eim", 3, 7, kIMM,
                         OP(logic_m(x + fetch(), d, std::bit_xor()))};
  instructions_[0x75] = {"eim", 3, 6, kIMM,
                         OP(logic_m(fetch(), d, std::bit_xor()))};
  instructions_[0x6b] = {"tim", 3, 7, kIMM,
                         OP(logic_m(x + fetch(), d, std::bit_and(), false))};
  instructions_[0x7b] = {"tim", 3, 6, kIMM,
                         OP(logic_m(fetch(), d, std::bit_and(), false))};
  // COM (1's complement)
  instructions_[0x43] = {"coma", 1, 1, kACA, OP(com(a))};
  instructions_[0x53] = {"comb", 1, 1, kACB, OP(com(b))};
  instructions_[0x63] = {"com", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::com))};
  instructions_[0x73] = {"com", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::com))};
  // LSR
  instructions_[0x44] = {"lsra", 1, 1, kACA, OP(lsr(a))};
  instructions_[0x54] = {"lsrb", 1, 1, kACB, OP(lsr(b))};
  instructions_[0x64] = {"lsr", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::lsr))};
  instructions_[0x74] = {"lsr", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::lsr))};
  // ROR
  instructions_[0x46] = {"rora", 1, 1, kACA, OP(ror(a))};
  instructions_[0x56] = {"rorb", 1, 1, kACB, OP(ror(b))};
  instructions_[0x66] = {"ror", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::ror))};
  instructions_[0x76] = {"ror", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::ror))};
  // ASR
  instructions_[0x47] = {"asra", 1, 1, kACA, OP(asr(a))};
  instructions_[0x57] = {"asrb", 1, 1, kACB, OP(asr(b))};
  instructions_[0x67] = {"asr", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::asr))};
  instructions_[0x77] = {"asr", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::asr))};
  // ASL
  instructions_[0x48] = {"asla", 1, 1, kACA, OP(asl(a))};
  instructions_[0x58] = {"aslb", 1, 1, kACB, OP(asl(b))};
  instructions_[0x68] = {"asl", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::asl))};
  instructions_[0x78] = {"asl", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::asl))};
  // ROL
  instructions_[0x49] = {"rola", 1, 1, kACA, OP(rol(a))};
  instructions_[0x59] = {"rolb", 1, 1, kACB, OP(rol(b))};
  instructions_[0x69] = {"rol", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::rol))};
  instructions_[0x79] = {"rol", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::rol))};
  // DEC
  instructions_[0x4a] = {"deca", 1, 1, kACA, OP(dec(a))};
  instructions_[0x5a] = {"decb", 1, 1, kACB, OP(dec(b))};
  instructions_[0x6a] = {"dec", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::dec))};
  instructions_[0x7a] = {"dec", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::dec))};
  // INC
  instructions_[0x4c] = {"inca", 1, 1, kACA, OP(inc(a))};
  instructions_[0x5c] = {"incb", 1, 1, kACB, OP(inc(b))};
  instructions_[0x6c] = {"inc", 2, 6, kIDX, OP(mem_op(d, &Cpu6301::inc))};
  instructions_[0x7c] = {"inc", 3, 6, kEXT, OP(mem_op(d, &Cpu6301::inc))};
  // TST
  instructions_[0x4d] = {"tsta", 1, 1, kACA, OP(cmp(d, 0))};
  instructions_[0x5d] = {"tstb", 1, 1, kACB, OP(cmp(d, 0))};
  instructions_[0x6d] = {"tst", 2, 4, kIDX, OP(cmp(get(d), 0))};
  instructions_[0x7d] = {"tst", 3, 4, kEXT, OP(cmp(get(d), 0))};
  // JMP
  instructions_[0x6e] = {"jmp", 2, 3, kIDX, OP(pc = d)};
  instructions_[0x7e] = {"jmp", 3, 3, kEXT, OP(pc = d)};
  // CLR
  instructions_[0x4f] = {"clra", 1, 1, kACA, OP(clr(a))};
  instructions_[0x5f] = {"clrb", 1, 1, kACB, OP(clr(b))};
  instructions_[0x6f] = {"clr", 2, 5, kIDX, OP(set_op(d, &Cpu6301::clr))};
  instructions_[0x7f] = {"clr", 3, 5, kEXT, OP(set_op(d, &Cpu6301::clr))};
  // SUB
  instructions_[0x80] = {"suba", 2, 2, kIMM, OP(sub(a, d))};
  instructions_[0x90] = {"suba", 2, 3, kDIR, OP(sub(a, get(d)))};
  instructions_[0xa0] = {"suba", 2, 4, kIDX, OP(sub(a, get(d)))};
  instructions_[0xb0] = {"suba", 3, 4, kEXT, OP(sub(a, get(d)))};
  instructions_[0xc0] = {"subb", 2, 2, kIMM, OP(sub(b, d))};
  instructions_[0xd0] = {"subb", 2, 3, kDIR, OP(sub(b, get(d)))};
  instructions_[0xe0] = {"subb", 2, 4, kIDX, OP(sub(b, get(d)))};
  instructions_[0xf0] = {"subb", 3, 4, kEXT, OP(sub(b, get(d)))};
  // CMP
  instructions_[0x81] = {"cmpa", 2, 2, kIMM, OP(cmp(a, d))};
  instructions_[0x91] = {"cmpa", 2, 3, kDIR, OP(cmp(a, get(d)))};
  instructions_[0xa1] = {"cmpa", 2, 4, kIDX, OP(cmp(a, get(d)))};
  instructions_[0xb1] = {"cmpa", 3, 4, kEXT, OP(cmp(a, get(d)))};
  instructions_[0xc1] = {"cmpb", 2, 2, kIMM, OP(cmp(b, d))};
  instructions_[0xd1] = {"cmpb", 2, 3, kDIR, OP(cmp(b, get(d)))};
  instructions_[0xe1] = {"cmpb", 2, 4, kIDX, OP(cmp(b, get(d)))};
  instructions_[0xf1] = {"cmpb", 3, 4, kEXT, OP(cmp(b, get(d)))};
  // SBC
  instructions_[0x82] = {"sbca", 2, 2, kIMM, OP(sbc(a, d))};
  instructions_[0x92] = {"sbca", 2, 3, kDIR, OP(sbc(a, get(d)))};
  instructions_[0xa2] = {"sbca", 2, 4, kIDX, OP(sbc(a, get(d)))};
  instructions_[0xb2] = {"sbca", 3, 4, kEXT, OP(sbc(a, get(d)))};
  instructions_[0xc2] = {"sbcb", 2, 2, kIMM, OP(sbc(b, d))};
  instructions_[0xd2] = {"sbcb", 2, 3, kDIR, OP(sbc(b, get(d)))};
  instructions_[0xe2] = {"sbcb", 2, 4, kIDX, OP(sbc(b, get(d)))};
  instructions_[0xf2] = {"sbcb", 3, 4, kEXT, OP(sbc(b, get(d)))};
  // SUBD
  instructions_[0x83] = {"subd", 3, 3, kIM2, OP(subd(d))};
  instructions_[0x93] = {"subd", 2, 4, kDIR, OP(subd(get16(d)))};
  instructions_[0xa3] = {"subd", 2, 5, kIDX, OP(subd(get16(d)))};
  instructions_[0xb3] = {"subd", 3, 5, kEXT, OP(subd(get16(d)))};
  // ADDD
  instructions_[0xc3] = {"addd", 3, 3, kIM2, OP(addd(d))};
  instructions_[0xd3] = {"addd", 2, 4, kDIR, OP(addd(get16(d)))};
  instructions_[0xe3] = {"addd", 2, 5, kIDX, OP(addd(get16(d)))};
  instructions_[0xf3] = {"addd", 3, 5, kEXT, OP(addd(get16(d)))};
  // AND
  instructions_[0x84] = {"anda", 2, 2, kIMM, OP(logic(d, std::bit_and(), a))};
  instructions_[0x94] = {"anda", 2, 3, kDIR,
                         OP(logic(get(d), std::bit_and(), a))};
  instructions_[0xa4] = {"anda", 2, 4, kIDX,
                         OP(logic(get(d), std::bit_and(), a))};
  instructions_[0xb4] = {"anda", 3, 4, kEXT,
                         OP(logic(get(d), std::bit_and(), a))};
  instructions_[0xc4] = {"andb", 2, 2, kIMM, OP(logic(d, std::bit_and(), b))};
  instructions_[0xd4] = {"andb", 2, 3, kDIR,
                         OP(logic(get(d), std::bit_and(), b))};
  instructions_[0xe4] = {"andb", 2, 4, kIDX,
                         OP(logic(get(d), std::bit_and(), b))};
  instructions_[0xf4] = {"andb", 3, 4, kEXT,
                         OP(logic(get(d), std::bit_and(), b))};
  // BIT
  instructions_[0x85] = {"bita", 2, 2, kIMM, OP(nzv_sr(d & a))};
  instructions_[0x95] = {"bita", 2, 3, kDIR, OP(nzv_sr(get(d) & a))};
  instructions_[0xa5] = {"bita", 2, 4, kIDX, OP(nzv_sr(get(d) & a))};
  instructions_[0xb5] = {"bita", 3, 4, kEXT, OP(nzv_sr(get(d) & a))};
  instructions_[0xc5] = {"bitb", 2, 2, kIMM, OP(nzv_sr(d & a))};
  instructions_[0xd5] = {"bitb", 2, 3, kDIR, OP(nzv_sr(get(d) & a))};
  instructions_[0xe5] = {"bitb", 2, 4, kIDX, OP(nzv_sr(get(d) & a))};
  instructions_[0xf5] = {"bitb", 3, 4, kEXT, OP(nzv_sr(get(d) & a))};
  // LDA
  instructions_[0x86] = {"ldaa", 2, 2, kIMM, OP(nzv_sr(a = d))};
  instructions_[0x96] = {"ldaa", 2, 3, kDIR, OP(nzv_sr(a = get(d)))};
  instructions_[0xa6] = {"ldaa", 2, 4, kIDX, OP(nzv_sr(a = get(d)))};
  instructions_[0xb6] = {"ldaa", 3, 4, kEXT, OP(nzv_sr(a = get(d)))};
  instructions_[0xc6] = {"ldab", 2, 2, kIMM, OP(nzv_sr(b = d))};
  instructions_[0xd6] = {"ldab", 2, 3, kDIR, OP(nzv_sr(b = get(d)))};
  instructions_[0xe6] = {"ldab", 2, 4, kIDX, OP(nzv_sr(b = get(d)))};
  instructions_[0xf6] = {"ldab", 3, 4, kEXT, OP(nzv_sr(b = get(d)))};
  // STA
  instructions_[0x97] = {"staa", 2, 3, kDIR, OP(set(d, a) COMMA nzv_sr(a))};
  instructions_[0xa7] = {"staa", 2, 4, kIDX, OP(set(d, a) COMMA nzv_sr(a))};
  instructions_[0xb7] = {"staa", 3, 4, kEXT, OP(set(d, a) COMMA nzv_sr(a))};
  instructions_[0xd7] = {"stab", 2, 3, kDIR, OP(set(d, b) COMMA nzv_sr(b))};
  instructions_[0xe7] = {"stab", 2, 4, kIDX, OP(set(d, b) COMMA nzv_sr(b))};
  instructions_[0xf7] = {"stab", 3, 4, kEXT, OP(set(d, b) COMMA nzv_sr(b))};
  // EOR
  instructions_[0x88] = {"eora", 2, 2, kIMM, OP(logic(d, std::bit_xor(), a))};
  instructions_[0x98] = {"eora", 2, 3, kDIR,
                         OP(logic(get(d), std::bit_xor(), a))};
  instructions_[0xa8] = {"eora", 2, 4, kIDX,
                         OP(logic(get(d), std::bit_xor(), a))};
  instructions_[0xb8] = {"eora", 3, 4, kEXT,
                         OP(logic(get(d), std::bit_xor(), a))};
  instructions_[0xc8] = {"eorb", 2, 2, kIMM, OP(logic(d, std::bit_xor(), b))};
  instructions_[0xd8] = {"eorb", 2, 3, kDIR,
                         OP(logic(get(d), std::bit_xor(), b))};
  instructions_[0xe8] = {"eorb", 2, 4, kIDX,
                         OP(logic(get(d), std::bit_xor(), b))};
  instructions_[0xf8] = {"eorb", 3, 4, kEXT,
                         OP(logic(get(d), std::bit_xor(), b))};
  // ADC
  instructions_[0x89] = {"adca", 2, 2, kIMM, OP(add(a, sr.C, d))};
  instructions_[0x99] = {"adca", 2, 3, kDIR, OP(add(a, sr.C, get(d)))};
  instructions_[0xa9] = {"adca", 2, 4, kIDX, OP(add(a, sr.C, get(d)))};
  instructions_[0xb9] = {"adca", 3, 4, kEXT, OP(add(a, sr.C, get(d)))};
  instructions_[0xc9] = {"adcb", 2, 2, kIMM, OP(add(b, sr.C, d))};
  instructions_[0xd9] = {"adcb", 2, 3, kDIR, OP(add(b, sr.C, get(d)))};
  instructions_[0xe9] = {"adcb", 2, 4, kIDX, OP(add(b, sr.C, get(d)))};
  instructions_[0xf9] = {"adcb", 3, 4, kEXT, OP(add(b, sr.C, get(d)))};
  // ORA
  instructions_[0x8a] = {"oraa", 2, 2, kIMM, OP(logic(d, std::bit_or(), a))};
  instructions_[0x9a] = {"oraa", 2, 3, kDIR,
                         OP(logic(get(d), std::bit_or(), a))};
  instructions_[0xaa] = {"oraa", 2, 4, kIDX,
                         OP(logic(get(d), std::bit_or(), a))};
  instructions_[0xba] = {"oraa", 3, 4, kEXT,
                         OP(logic(get(d), std::bit_or(), a))};
  instructions_[0xca] = {"orab", 2, 2, kIMM, OP(logic(d, std::bit_or(), b))};
  instructions_[0xda] = {"orab", 2, 3, kDIR,
                         OP(logic(get(d), std::bit_or(), b))};
  instructions_[0xea] = {"orab", 2, 4, kIDX,
                         OP(logic(get(d), std::bit_or(), b))};
  instructions_[0xfa] = {"orab", 3, 4, kEXT,
                         OP(logic(get(d), std::bit_or(), b))};
  // ADD
  instructions_[0x8b] = {"adda", 2, 2, kIMM, OP(add(a, false, d))};
  instructions_[0x9b] = {"adda", 2, 3, kDIR, OP(add(a, false, get(d)))};
  instructions_[0xab] = {"adda", 2, 4, kIDX, OP(add(a, false, get(d)))};
  instructions_[0xbb] = {"adda", 3, 4, kEXT, OP(add(a, false, get(d)))};
  instructions_[0xcb] = {"addb", 2, 2, kIMM, OP(add(b, false, d))};
  instructions_[0xdb] = {"addb", 2, 3, kDIR, OP(add(b, false, get(d)))};
  instructions_[0xeb] = {"addb", 2, 4, kIDX, OP(add(b, false, get(d)))};
  instructions_[0xfb] = {"addb", 3, 4, kEXT, OP(add(b, false, get(d)))};
  // CPX
  instructions_[0x8c] = {"cpx", 3, 3, kIM2, OP(cmp16(x, d))};
  instructions_[0x9c] = {"cpx", 2, 4, kDIR, OP(cmp16(x, get16(d)))};
  instructions_[0xac] = {"cpx", 2, 5, kIDX, OP(cmp16(x, get16(d)))};
  instructions_[0xbc] = {"cpx", 3, 5, kEXT, OP(cmp16(x, get16(d)))};
  // LDD
  instructions_[0xcc] = {"ldd", 3, 3, kIM2, OP(nzv_sr16(set_d(d)))};
  instructions_[0xdc] = {"ldd", 2, 4, kDIR, OP(nzv_sr16(set_d(get16(d))))};
  instructions_[0xec] = {"ldd", 2, 5, kIDX, OP(nzv_sr16(set_d(get16(d))))};
  instructions_[0xfc] = {"ldd", 3, 5, kEXT, OP(nzv_sr16(set_d(get16(d))))};
  // BSR
  instructions_[0x8d] = {"bsr", 2, 5, kIMM, OP(bsr(d))};
  // JSR
  instructions_[0x9d] = {"jsr", 2, 5, kDIR, OP(jsr(d))};
  instructions_[0xad] = {"jsr", 2, 5, kIDX, OP(jsr(d))};
  instructions_[0xbd] = {"jsr", 3, 6, kEXT, OP(jsr(d))};
  // STD
  instructions_[0xdd] = {"std", 2, 4, kDIR,
                         OP(set16(d, get_d()) COMMA nzv_sr16(get_d()))};
  instructions_[0xed] = {"std", 2, 5, kIDX,
                         OP(set16(d, get_d()) COMMA nzv_sr16(get_d()))};
  instructions_[0xfd] = {"std", 3, 5, kEXT,
                         OP(set16(d, get_d()) COMMA nzv_sr16(get_d()))};
  // LDS
  instructions_[0x8e] = {"lds", 3, 3, kIM2, OP(nzv_sr16(sp = d))};
  instructions_[0x9e] = {"lds", 2, 4, kDIR, OP(nzv_sr16(sp = get16(d)))};
  instructions_[0xae] = {"lds", 2, 5, kIDX, OP(nzv_sr16(sp = get16(d)))};
  instructions_[0xbe] = {"lds", 3, 5, kEXT, OP(nzv_sr16(sp = get16(d)))};
  // LDX
  instructions_[0xce] = {"ldx", 3, 3, kIM2, OP(nzv_sr16(x = d))};
  instructions_[0xde] = {"ldx", 2, 4, kDIR, OP(nzv_sr16(x = get16(d)))};
  instructions_[0xee] = {"ldx", 2, 5, kIDX, OP(nzv_sr16(x = get16(d)))};
  instructions_[0xfe] = {"ldx", 3, 5, kEXT, OP(nzv_sr16(x = get16(d)))};
  // STS
  instructions_[0x9f] = {"sts", 2, 4, kDIR,
                         OP(set16(d, sp) COMMA nzv_sr16(sp))};
  instructions_[0xaf] = {"sts", 2, 5, kIDX,
                         OP(set16(d, sp) COMMA nzv_sr16(sp))};
  instructions_[0xbf] = {"sts", 3, 5, kEXT,
                         OP(set16(d, sp) COMMA nzv_sr16(sp))};
  // STX
  instructions_[0xdf] = {"stx", 2, 4, kDIR, OP(set16(d, x) COMMA nzv_sr16(x))};
  instructions_[0xef] = {"stx", 2, 5, kIDX, OP(set16(d, x) COMMA nzv_sr16(x))};
  instructions_[0xff] = {"stx", 3, 5, kEXT, OP(set16(d, x) COMMA nzv_sr16(x))};
#undef OP
#undef COMMA
}

absl::Status Cpu6301::initialize() {
  auto serial = HD6301Serial::create(memory_, 0x0010, &serial_interrupt_);
  if (!serial.ok()) {
    return serial.status();
  }
  serial_ = std::move(serial.value());

  // Set up reads, writes to port 1 & port 1 DDR.
  auto status = memory_->register_read(
      0x0000, 0x0000, [this](uint16_t) { return port1_.get_direction(); });
  if (!status.ok()) {
    return status;
  }
  status = memory_->register_write(
      0x0000, 0x0000,
      [this](uint16_t, uint8_t data) { port1_.set_direction(data); });
  if (!status.ok()) {
    return status;
  }
  status = memory_->register_read(0x0002, 0x0002,
                                  [this](uint16_t) { return port1_.read(); });
  if (!status.ok()) {
    return status;
  }
  status = memory_->register_write(
      0x0002, 0x0002, [this](uint16_t, uint8_t data) { port1_.write(data); });
  if (!status.ok()) {
    return status;
  }

  // Set up reads, writes to port 2 & port 2 DDR.
  status = memory_->register_read(
      0x0001, 0x0001, [this](uint16_t) { return port2_.get_direction(); });
  if (!status.ok()) {
    return status;
  }
  status = memory_->register_write(
      0x0001, 0x0001,
      [this](uint16_t, uint8_t data) { port2_.set_direction(data); });
  if (!status.ok()) {
    return status;
  }
  status = memory_->register_read(0x0003, 0x0003,
                                  [this](uint16_t) { return port2_.read(); });
  if (!status.ok()) {
    return status;
  }
  status = memory_->register_write(
      0x0003, 0x0003, [this](uint16_t, uint8_t data) { port2_.write(data); });
  return status;
}

}  // namespace eight_bit