#ifndef EIGHT_BIT_CPU6301_H
#define EIGHT_BIT_CPU6301_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include "address_space.h"
#include "interrupt.h"
#include "ioport.h"
#include "timer.h"

namespace eight_bit {

class Cpu6301 {
 public:
  Cpu6301(AddressSpace* memory);

  void reset();

  uint8_t tick();

  void print_state();

  IOPort* get_port1();
  IOPort* get_port2();
  Interrupt* get_irq();

 private:
  struct StatusRegister {
    StatusRegister() : StatusRegister(0) {}
    explicit StatusRegister(uint8_t r)
        : H(r & 0x20),
          I(r & 0x10),
          N(r & 0x08),
          Z(r & 0x04),
          V(r & 0x02),
          C(r & 0x01) {}

    uint8_t as_integer() {
      return 0xC0 | H << 5 | I << 4 | N << 3 | Z << 2 | V << 1 | (uint8_t)C;
    }

    bool _U1 = 1;
    bool _U2 = 1;
    bool H = 0;
    bool I = 0;
    bool N = 0;
    bool Z = 0;
    bool V = 0;
    bool C = 0;
  };

  enum AddressingMode {
    kIMM,  // 1-byte immediate
    kIM2,  // 2-byte immediate data
    kACA,
    kACB,
    kACD,
    kDIR,
    kEXT,
    kIDX,
    kIMP,
    kREL,
  };

  struct Instruction {
    std::string name;
    uint8_t bytes;
    uint8_t cycles;
    AddressingMode mode;
    std::function<void(uint16_t)> exec;
  };

  //
  // Memory helper methods
  //
  uint8_t fetch();

  uint16_t fetch16();

  uint8_t get(uint16_t address);

  uint16_t get16(uint16_t address);

  void set(uint16_t address, uint8_t data);

  void set16(uint16_t address, uint16_t data);

  uint16_t get_d();

  uint16_t set_d(uint16_t d);

  //
  // Instruction implementaions
  //
  void mem_op(uint16_t address, void (Cpu6301::*op)(uint8_t&), bool do_set);
  void set_op(uint16_t address, void (Cpu6301::*op)(uint8_t&));
  void tap();
  void tpa();
  void add_z(uint16_t& r, uint16_t data);
  void cmp(uint8_t dest, uint8_t mem);
  void cmp16(uint16_t dest, uint16_t mem);
  void sub(uint8_t& dest, uint8_t mem);
  void sbc(uint8_t& dest, uint8_t mem);
  void subd(uint16_t data);
  void add(uint8_t& dest, bool carry, uint8_t mem);
  void addd(uint16_t data);
  void xgdx();
  void brx(bool branch, uint16_t dest);
  uint8_t pul8();
  uint16_t pul16();
  void psh8(uint8_t data);
  void psh16(uint16_t data);
  void rti();
  void mul();
  void neg(uint8_t& dest);
  void nzv_sr(uint8_t result);
  void nzv_sr16(uint16_t result);
  void logic(uint8_t data, std::function<uint8_t(uint8_t, uint8_t)> op,
             uint8_t& dest);
  void logic_m(uint16_t address, uint8_t data,
               std::function<uint8_t(uint8_t, uint8_t)> op, bool do_set);
  void com(uint8_t& dest);
  void rot_flags(uint8_t result, bool carry);
  void rot_flags16(uint16_t result, bool carry);
  void lsr(uint8_t& dest);
  void lsrd(uint16_t& dest);
  void ror(uint8_t& dest);
  void rol(uint8_t& dest);
  void asr(uint8_t& dest);
  void asl(uint8_t& dest);
  void asld(uint16_t& dest);
  void dec(uint8_t& dest);
  void inc(uint8_t& dest);
  void clr(uint8_t& dest);
  void bsr(int8_t offset);
  void jsr(uint16_t address);

  void enter_interrupt(uint16_t vector);
  uint8_t execute(uint8_t opcode);

  IOPort port1_;
  IOPort port2_;
  Interrupt interrupt_;
  Interrupt timer_interrupt_;
  Timer timer_;

  uint8_t a = 0;
  uint8_t b = 0;
  uint16_t x = 0;
  uint16_t sp = 0x0200;
  uint16_t pc = 0xfffe;
  StatusRegister sr;

  uint8_t current_opcode_ = 0;
  uint8_t opcode_cycles_ = 0;
  AddressSpace* memory_ = nullptr;
  std::map<uint8_t, Instruction> instructions_;
};

}  // namespace eight_bit

#endif  // EIGHT_BIT_CPU6301_H
