#include "mcp7940n.h"

#include <gtest/gtest.h>

#include "ioport.h"

namespace eight_bit {
namespace {

// Drives the MCP7940N via DDR-based open-drain I2C (the same protocol used by
// the ASM bit-bang code: DDR bit = 1 drives line low, DDR bit = 0 floats high).
class MCP7940NTest : public ::testing::Test {
 protected:
  MCP7940NTest() : port_a_("Test Port A") {}

  void SetUp() override {
    mcp7940n_ = MCP7940N::create(&port_a_);
    // Both lines floating high (idle).
    ddr_ = 0;
    port_a_.write_data_direction_register(ddr_);
    // Simulate pull-up resistors: SDA/SCL read as 1 when not driven.
    port_a_.provide_inputs(0xFF, 0xFF);
  }

  // --- Low-level line control ---

  void SetSCL(bool high) {
    if (high)
      ddr_ &= ~0x01u;
    else
      ddr_ |= 0x01u;
    port_a_.write_data_direction_register(ddr_);
  }

  void SetSDA(bool high) {
    if (high)
      ddr_ &= ~0x02u;
    else
      ddr_ |= 0x02u;
    port_a_.write_data_direction_register(ddr_);
  }

  // Read what the slave (MCP7940N) is currently driving on SDA.
  bool ReadSDA() {
    return (port_a_.read_input_register() & 0x02) != 0;
  }

  // --- I2C primitives ---

  void SendStart() {
    SetSCL(true);
    SetSDA(true);
    SetSDA(false);  // SDA falls while SCL high → START
  }

  void SendStop() {
    SetSCL(false);
    SetSDA(false);
    SetSCL(true);
    SetSDA(true);  // SDA rises while SCL high → STOP
  }

  // Send one byte, return true if slave ACKed (SDA=0 after 9th clock).
  bool SendByte(uint8_t byte) {
    for (int i = 7; i >= 0; --i) {
      SetSCL(false);
      SetSDA((byte >> i) & 1);
      SetSCL(true);
    }
    // Clock the ACK bit: release SDA so slave can drive it.
    SetSCL(false);
    SetSDA(true);
    SetSCL(true);
    return !ReadSDA();  // true = ACK (slave drove SDA low)
  }

  // Read one byte, send ACK (send_ack=true) or NACK (send_ack=false).
  uint8_t ReadByte(bool send_ack) {
    uint8_t result = 0;
    for (int i = 7; i >= 0; --i) {
      SetSCL(false);
      SetSDA(true);  // release SDA so slave can drive it
      SetSCL(true);
      if (ReadSDA()) result |= static_cast<uint8_t>(1u << i);
    }
    // ACK/NACK: drive SDA low for ACK, release for NACK.
    SetSCL(false);
    SetSDA(!send_ack);
    SetSCL(true);
    return result;
  }

  // --- Higher-level helpers ---

  // Write the register pointer (write-only transaction).
  void SetRegPointer(uint8_t reg) {
    SendStart();
    SendByte(0xDE);  // address + W
    SendByte(reg);
    SendStop();
  }

  // Read `count` consecutive registers starting at `start_reg`.
  std::vector<uint8_t> ReadRegs(uint8_t start_reg, int count) {
    SetRegPointer(start_reg);
    SendStart();
    SendByte(0xDF);  // address + R
    std::vector<uint8_t> out;
    out.reserve(count);
    for (int i = 0; i < count; ++i) {
      out.push_back(ReadByte(i < count - 1));  // ACK all but last
    }
    SendStop();
    return out;
  }

  IOPort port_a_;
  std::unique_ptr<MCP7940N> mcp7940n_;
  uint8_t ddr_ = 0;
};

// --- Write-path tests ---

TEST_F(MCP7940NTest, WriteAddressByteIsAcked) {
  SendStart();
  EXPECT_TRUE(SendByte(0xDE));
  SendStop();
}

TEST_F(MCP7940NTest, RegisterPointerByteIsAcked) {
  SendStart();
  EXPECT_TRUE(SendByte(0xDE));
  EXPECT_TRUE(SendByte(0x00));
  SendStop();
}

TEST_F(MCP7940NTest, DataByteInWriteTransactionIsAcked) {
  SendStart();
  EXPECT_TRUE(SendByte(0xDE));
  EXPECT_TRUE(SendByte(0x07));  // register pointer (CONTROL reg)
  EXPECT_TRUE(SendByte(0x42));  // data
  SendStop();
}

TEST_F(MCP7940NTest, WrittenControlRegisterIsReadBack) {
  // Write 0x42 to register 0x07.
  SendStart();
  SendByte(0xDE);
  SendByte(0x07);
  SendByte(0x42);
  SendStop();

  auto regs = ReadRegs(0x07, 1);
  EXPECT_EQ(regs[0], 0x42);
}

TEST_F(MCP7940NTest, MultiByteWriteIsAcked) {
  SendStart();
  EXPECT_TRUE(SendByte(0xDE));
  EXPECT_TRUE(SendByte(0x07));  // pointer
  EXPECT_TRUE(SendByte(0x11));  // data byte 0 → reg 0x07
  EXPECT_TRUE(SendByte(0x22));  // data byte 1 → reg 0x08
  SendStop();

  // Verify both registers were written.
  auto regs = ReadRegs(0x07, 2);
  EXPECT_EQ(regs[0], 0x11);
  EXPECT_EQ(regs[1], 0x22);
}

// --- Read-path tests ---

TEST_F(MCP7940NTest, ReadAddressByteIsAcked) {
  SetRegPointer(0x00);
  SendStart();
  EXPECT_TRUE(SendByte(0xDF));
  ReadByte(false);  // NACK (done)
  SendStop();
}

TEST_F(MCP7940NTest, SecondsRegisterHasSTBitSet) {
  auto regs = ReadRegs(0x00, 1);
  EXPECT_NE(regs[0] & 0x80u, 0u) << "ST bit must be set";
}

TEST_F(MCP7940NTest, SecondsRegisterIsValidBCD) {
  auto regs = ReadRegs(0x00, 1);
  uint8_t bcd = regs[0] & 0x7Fu;
  EXPECT_LE(bcd & 0x0Fu, 9u) << "seconds lower nibble must be 0-9";
  EXPECT_LE(bcd >> 4u, 5u)   << "seconds upper nibble must be 0-5";
}

TEST_F(MCP7940NTest, MinutesRegisterIsValidBCD) {
  auto regs = ReadRegs(0x00, 2);
  uint8_t bcd = regs[1];
  EXPECT_LE(bcd & 0x0Fu, 9u) << "minutes lower nibble must be 0-9";
  EXPECT_LE(bcd >> 4u, 5u)   << "minutes upper nibble must be 0-5";
}

TEST_F(MCP7940NTest, HoursRegisterIsValidBCD) {
  auto regs = ReadRegs(0x00, 3);
  uint8_t bcd = regs[2] & 0x3Fu;
  EXPECT_LE(bcd & 0x0Fu, 9u) << "hours lower nibble must be 0-9";
  EXPECT_LE(bcd >> 4u, 2u)   << "hours upper nibble must be 0-2";
}

TEST_F(MCP7940NTest, SequentialReadReturnsDifferentRegisters) {
  // Read sec, min, hour in one transaction and validate all three.
  auto regs = ReadRegs(0x00, 3);
  ASSERT_EQ(regs.size(), 3u);

  uint8_t sec  = regs[0] & 0x7Fu;
  uint8_t min_ = regs[1];
  uint8_t hour = regs[2] & 0x3Fu;

  EXPECT_LE(sec  & 0x0Fu, 9u);  EXPECT_LE(sec  >> 4u, 5u);
  EXPECT_LE(min_ & 0x0Fu, 9u);  EXPECT_LE(min_ >> 4u, 5u);
  EXPECT_LE(hour & 0x0Fu, 9u);  EXPECT_LE(hour >> 4u, 2u);
}

TEST_F(MCP7940NTest, NACKStopsSequentialRead) {
  // Read only one byte and NACK — state should return to idle.
  SetRegPointer(0x00);
  SendStart();
  SendByte(0xDF);
  ReadByte(false);  // NACK after first byte
  SendStop();

  // A fresh read starting at 0x00 should still work.
  auto regs = ReadRegs(0x00, 1);
  EXPECT_NE(regs[0] & 0x80u, 0u);  // ST bit set
}

TEST_F(MCP7940NTest, SecondTransactionAfterStopWorks) {
  // First complete transaction.
  auto first  = ReadRegs(0x00, 1);
  // Second complete transaction.
  auto second = ReadRegs(0x00, 1);

  // Both should be valid BCD seconds.
  EXPECT_LE((first[0]  & 0x7Fu) >> 4u, 5u);
  EXPECT_LE((second[0] & 0x7Fu) >> 4u, 5u);
}

}  // namespace
}  // namespace eight_bit
