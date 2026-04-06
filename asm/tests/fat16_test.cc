// Unit tests for the FAT16 filesystem implementation (asm/include/file.inc).
//
// Strategy: load the assembled ROM binary into a flat 64 KB address space,
// stub out sd_card_read_block / sd_card_initialize / output functions with
// no-op stubs, pre-populate relevant buffers with test fixtures, then call
// assembly subroutines directly by pointing PC at the entry point and pushing
// a sentinel return address onto the stack.
//
// Tests that require data from the SD card (file_open, file_read, file_init,
// fat_cluster_offset) pre-fill the relevant buffer (fat_current_dir_buffer,
// fat_buffer, or the I/O buffer) before calling the subroutine, since the
// sd_card_read_block stub returns success (A=0) without writing anything.

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "address_space.h"
#include "cpu6301.h"
#include "gtest/gtest.h"

namespace eight_bit {
namespace {

using ::absl_testing::IsOk;

// ─── Helpers ─────────────────────────────────────────────────────────────────

std::map<std::string, uint16_t> ParseSymbols(const char* path) {
  std::map<std::string, uint16_t> syms;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    if (line.size() < 5 || line[4] != ' ') continue;
    uint16_t addr =
        static_cast<uint16_t>(std::stoul(line.substr(0, 4), nullptr, 16));
    syms[line.substr(5)] = addr;
  }
  return syms;
}

std::vector<uint8_t> LoadBinary(const char* path) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return {};
  auto size = f.tellg();
  f.seekg(0);
  std::vector<uint8_t> buf(static_cast<size_t>(size));
  f.read(reinterpret_cast<char*>(buf.data()), size);
  return buf;
}

// ─── Base fixture ─────────────────────────────────────────────────────────────

class Fat16Test : public ::testing::Test {
 protected:
  // Constants mirroring file.inc (kept in sync manually).
  static constexpr int kFdStructSize         = 11;
  static constexpr int kFdTableEntries       = 2;
  static constexpr int kFdStateOffset        = 0;
  static constexpr int kFdIoAddressOffset    = 1;
  static constexpr int kFdStartClusterOffset = 3;
  static constexpr int kFdStartSectorOffset  = 5;
  static constexpr int kFdFileSizeOffset     = 7;
  static constexpr int kFdReadPtrOffset      = 9;
  static constexpr uint8_t kFdStateOpen      = 0x01;

  static constexpr int kDirEntrySize          = 32;
  static constexpr int kDirEntryAttrOffset    = 11;
  static constexpr int kDirEntryClusterOffset = 26;  // little-endian
  static constexpr int kDirEntrySizeOffset    = 28;  // little-endian (32-bit)
  static constexpr uint8_t kAttrArchive       = 0x20;
  static constexpr uint8_t kAttrSubdir        = 0x10;
  static constexpr uint8_t kDeletedEntry      = 0xE5;

  // Stack and sentinel placement.  The sentinel is in user RAM; the stack
  // uses addresses in the I/O space range (unused as hardware in tests).
  static constexpr uint16_t kStackBase    = 0x7F00;
  static constexpr uint16_t kSentinelAddr = 0x7700;
  static constexpr int      kMaxCycles    = 500'000;

  // Fixed RAM areas for test strings and the I/O buffer used by file_open/read.
  static constexpr uint16_t kStringArea = 0x0400;
  static constexpr uint16_t kIoBuffer   = 0x3000;  // 512 bytes

  // ── SetUp ──────────────────────────────────────────────────────────────────

  void SetUp() override {
    auto syms     = ParseSymbols(SYMBOLS_PATH);
    auto rom_data = LoadBinary(ROM_PATH);
    ASSERT_FALSE(syms.empty())     << "Failed to load symbols: " SYMBOLS_PATH;
    ASSERT_FALSE(rom_data.empty()) << "Failed to load ROM: " ROM_PATH;

    auto sym = [&](const char* n) -> uint16_t {
      auto it = syms.find(n);
      EXPECT_NE(it, syms.end()) << "Symbol not found: " << n;
      return (it != syms.end()) ? it->second : 0u;
    };

    // FAT16 data variables (defined by reserve_system_memory in file.inc).
    // fat_current_dir_buffer is immediately before fat_dir_guard_byte.
    s_dir_buf_        = sym("fat_dir_guard_byte") - 512;
    s_dir_guard_      = sym("fat_dir_guard_byte");
    s_spc_            = sym("fat_sectors_per_cluster");
    s_spc_mask_       = sym("fat_sectors_per_cluster_mask");
    s_reserved_       = sym("fat_reserved_sectors");
    s_spf_            = sym("fat_sectors_per_fat");
    s_root_dir_       = sym("fat_root_dir_start_sector");
    s_data_start_     = sym("fat_data_start_sector");
    s_fat_buf_        = sym("fat_buffer");
    s_fat_buf_sector_ = sym("fat_buffer_sector");
    s_fd_table_       = sym("fd_table");
    s_sd_blk_addr_    = sym("sd_card_block_address");
    s_sd_io_addr_     = sym("sd_card_io_buffer_address");

    // Subroutine entry points.
    s_file_init_  = sym("file_init");
    s_file_open_  = sym("file_open");
    s_file_close_ = sym("file_close");
    s_file_read_  = sym("file_read");
    s_file_at_eof_= sym("file_at_eof");
    s_clu2sec_    = sym("fat_cluster_to_sector");
    s_clu_offset_ = sym("fat_cluster_offset");
    s_find_dir_   = sym("find_dir_entry");
    s_validate_   = sym("validate_name");
    s_sd_read_    = sym("sd_card_read_block");
    s_sd_init_    = sym("sd_card_initialize");
    s_putstring_  = sym("putstring");
    s_putchar_    = sym("putchar");
    s_put_nl_     = sym("put_newline");

    // Build flat 64 KB memory and wire it into AddressSpace.
    mem_.fill(0);
    memory_ = std::make_unique<AddressSpace>();
    ASSERT_THAT(memory_->register_read(0x0020, 0xffff,
                    [this](uint16_t a) { return mem_[a]; }),
                IsOk());
    ASSERT_THAT(memory_->register_write(0x0020, 0xffff,
                    [this](uint16_t a, uint8_t v) { mem_[a] = v; }),
                IsOk());

    // Load the ROM binary at 0x8000 (covers 0x8000–0xFFFF, 32 KB).
    constexpr uint16_t kRomBase = 0x8000;
    for (size_t i = 0; i < rom_data.size() && i < 0x8000u; ++i)
      mem_[kRomBase + i] = rom_data[i];

    // Stub SD card and output functions.
    // sd_card_read_block / sd_card_initialize → CLRA (0x4F) + RTS (0x39)
    // putstring / putchar / put_newline       → RTS (0x39)
    mem_[s_sd_read_]    = 0x4F; mem_[s_sd_read_    + 1] = 0x39;
    mem_[s_sd_init_]    = 0x4F; mem_[s_sd_init_    + 1] = 0x39;
    mem_[s_putstring_]  = 0x39;
    mem_[s_putchar_]    = 0x39;
    mem_[s_put_nl_]     = 0x39;

    cpu_ = Cpu6301::create(memory_.get()).value();
  }

  // ── Subroutine execution ───────────────────────────────────────────────────

  // Run the subroutine at `pc` with the given initial registers.
  // A synthetic return address (kSentinelAddr) is pre-placed on the stack so
  // that the subroutine's RTS returns to the sentinel, terminating the loop.
  // Returns true if the subroutine returned within kMaxCycles.
  bool Run(uint16_t pc, uint8_t a = 0, uint8_t b = 0, uint16_t x = 0) {
    // HD6301 RTS: SP++; PCH = mem[SP]; SP++; PCL = mem[SP].
    // Place sentinel so that mem[init_SP+1] = sentinel_hi,
    //                         mem[init_SP+2] = sentinel_lo.
    uint16_t sp = kStackBase;
    mem_[sp] = kSentinelAddr & 0xFF;   // PCL at kStackBase
    sp--;
    mem_[sp] = kSentinelAddr >> 8;     // PCH at kStackBase-1
    sp--;                               // SP = kStackBase-2, as after a JSR

    Cpu6301::CpuState s{};
    s.a  = a;
    s.b  = b;
    s.x  = x;
    s.sp = sp;
    s.pc = pc;
    s.sr = 0xC0;  // standard default
    cpu_->set_state(s);

    for (int i = 0; i < kMaxCycles; ++i) {
      if (cpu_->get_state().pc == kSentinelAddr) return true;
      cpu_->tick(1);
    }
    return cpu_->get_state().pc == kSentinelAddr;
  }

  Cpu6301::CpuState State() const { return cpu_->get_state(); }
  bool     ZFlag() const { return Cpu6301::StatusRegister(State().sr).Z != 0; }
  uint8_t  A()     const { return State().a; }
  uint8_t  B()     const { return State().b; }
  uint16_t X()     const { return State().x; }

  // ── Memory helpers (HD6301 stores 16-bit values big-endian) ───────────────

  uint8_t  M8(uint16_t addr)  const { return mem_[addr]; }
  uint16_t M16(uint16_t addr) const {
    return static_cast<uint16_t>((mem_[addr] << 8) | mem_[addr + 1]);
  }
  void W8(uint16_t addr, uint8_t v) { mem_[addr] = v; }
  void W16(uint16_t addr, uint16_t v) {
    mem_[addr] = v >> 8; mem_[addr + 1] = v & 0xFF;
  }

  // ── FD table helpers ───────────────────────────────────────────────────────
  // 16-bit fields within the FD struct are stored big-endian.

  uint16_t FdBase(int fd) const {
    return static_cast<uint16_t>(s_fd_table_ + fd * kFdStructSize);
  }
  void     SetFdState(int fd, uint8_t v)    { W8(FdBase(fd)  + kFdStateOffset,    v); }
  void     SetFdIoAddr(int fd, uint16_t v)  { W16(FdBase(fd) + kFdIoAddressOffset, v); }
  void     SetFdStartCluster(int fd, uint16_t v) { W16(FdBase(fd) + kFdStartClusterOffset, v); }
  void     SetFdStartSector(int fd, uint16_t v)  { W16(FdBase(fd) + kFdStartSectorOffset,  v); }
  void     SetFdSize(int fd, uint16_t v)    { W16(FdBase(fd) + kFdFileSizeOffset,   v); }
  void     SetFdReadPtr(int fd, uint16_t v) { W16(FdBase(fd) + kFdReadPtrOffset,    v); }
  uint8_t  GetFdState(int fd)    const { return M8(FdBase(fd)  + kFdStateOffset);    }
  uint16_t GetFdSize(int fd)     const { return M16(FdBase(fd) + kFdFileSizeOffset); }
  uint16_t GetFdReadPtr(int fd)  const { return M16(FdBase(fd) + kFdReadPtrOffset);  }

  // ── Directory buffer helpers ───────────────────────────────────────────────
  // Directory entries on-disk (and in fat_current_dir_buffer) use little-endian
  // for cluster and size fields.

  uint16_t DirAddr(int slot) const {
    return static_cast<uint16_t>(s_dir_buf_ + slot * kDirEntrySize);
  }
  // name83: exactly 11 chars — 8-char name + 3-char ext, space-padded, no dot.
  void SetDirEntry(int slot, const char* name83, uint8_t attr,
                   uint16_t cluster, uint16_t size) {
    uint16_t b = DirAddr(slot);
    for (int i = 0; i < 11; ++i) mem_[b + i] = static_cast<uint8_t>(name83[i]);
    mem_[b + kDirEntryAttrOffset] = attr;
    mem_[b + kDirEntryClusterOffset]     = cluster & 0xFF;  // LE
    mem_[b + kDirEntryClusterOffset + 1] = cluster >> 8;
    mem_[b + kDirEntrySizeOffset]        = size & 0xFF;     // LE 32-bit
    mem_[b + kDirEntrySizeOffset + 1]    = size >> 8;
    mem_[b + kDirEntrySizeOffset + 2]    = 0;
    mem_[b + kDirEntrySizeOffset + 3]    = 0;
  }
  void TerminateDir(int slot) { mem_[DirAddr(slot)] = 0x00; }
  void MarkDeleted(int slot)  { mem_[DirAddr(slot)] = kDeletedEntry; }
  void ClearDirBuf() {
    std::fill(mem_.begin() + s_dir_buf_, mem_.begin() + s_dir_buf_ + 512, 0);
    mem_[s_dir_guard_] = 0;
  }

  // ── String helper ──────────────────────────────────────────────────────────

  // Write a null-terminated string at kStringArea and return its address.
  uint16_t Str(const char* s) {
    uint16_t addr = kStringArea;
    for (size_t i = 0; i <= std::strlen(s); ++i)
      mem_[addr + i] = static_cast<uint8_t>(s[i]);
    return addr;
  }

  // ── Common BPB global setup ────────────────────────────────────────────────

  void SetBpbDefaults(uint16_t data_start = 35, uint8_t spc = 1) {
    W8(s_spc_,             spc);
    W8(s_spc_mask_,        static_cast<uint8_t>(spc > 1 ? spc - 1 : 0));
    W16(s_reserved_,       1);
    W16(s_spf_,            1);
    W16(s_root_dir_,       3);
    W16(s_data_start_,     data_start);
    W8(s_fat_buf_sector_,  0xFF);  // no FAT sector cached
    mem_[s_dir_guard_]   = 0;
  }

  // ── Members ────────────────────────────────────────────────────────────────

  std::array<uint8_t, 65536> mem_{};
  std::unique_ptr<AddressSpace> memory_;
  std::unique_ptr<Cpu6301> cpu_;

  // Symbol addresses, resolved in SetUp.
  uint16_t s_dir_buf_, s_dir_guard_;
  uint16_t s_spc_, s_spc_mask_, s_reserved_, s_spf_, s_root_dir_, s_data_start_;
  uint16_t s_fat_buf_, s_fat_buf_sector_;
  uint16_t s_fd_table_, s_sd_blk_addr_, s_sd_io_addr_;
  uint16_t s_file_init_, s_file_open_, s_file_close_, s_file_read_, s_file_at_eof_;
  uint16_t s_clu2sec_, s_clu_offset_;
  uint16_t s_find_dir_, s_validate_;
  uint16_t s_sd_read_, s_sd_init_;
  uint16_t s_putstring_, s_putchar_, s_put_nl_;
};

// ═══════════════════════════════════════════════════════════════════════════════
// fat_cluster_to_sector
// Contract: sector = fat_data_start_sector + (cluster - 2) * fat_sectors_per_cluster
//           Result returned in D (A:B) and written to sd_card_block_address[2:3].
//           sd_card_block_address[0:1] are always cleared to 0.
// ═══════════════════════════════════════════════════════════════════════════════

class ClusterToSectorTest : public Fat16Test {
 protected:
  void SetUp() override {
    Fat16Test::SetUp();
    SetBpbDefaults();
  }
  uint16_t ResultD() const { return static_cast<uint16_t>((A() << 8) | B()); }
  uint16_t BlockAddrLo() const { return M16(s_sd_blk_addr_ + 2); }
};

TEST_F(ClusterToSectorTest, Cluster2IsAlwaysDataStartSector) {
  W16(s_data_start_, 100); W8(s_spc_, 1);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=cluster*/2));
  EXPECT_EQ(ResultD(), 100u);
  EXPECT_EQ(BlockAddrLo(), 100u);
}

TEST_F(ClusterToSectorTest, Cluster3OneSectorPerCluster) {
  W16(s_data_start_, 100); W8(s_spc_, 1);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=*/3));
  EXPECT_EQ(ResultD(), 101u);
}

TEST_F(ClusterToSectorTest, Cluster5OneSectorPerCluster) {
  W16(s_data_start_, 100); W8(s_spc_, 1);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=*/5));
  // (5-2)*1 + 100 = 103
  EXPECT_EQ(ResultD(), 103u);
  EXPECT_EQ(BlockAddrLo(), 103u);
}

TEST_F(ClusterToSectorTest, Cluster5FourSectorsPerCluster) {
  W16(s_data_start_, 100); W8(s_spc_, 4);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=*/5));
  // (5-2)*4 + 100 = 112
  EXPECT_EQ(ResultD(), 112u);
}

TEST_F(ClusterToSectorTest, Cluster5EightSectorsPerCluster) {
  W16(s_data_start_, 200); W8(s_spc_, 8);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=*/5));
  // (5-2)*8 + 200 = 224
  EXPECT_EQ(ResultD(), 224u);
}

TEST_F(ClusterToSectorTest, UpperTwoBytesOfBlockAddressAreZero) {
  // Pre-fill block address with garbage; upper bytes must be cleared.
  mem_[s_sd_blk_addr_]     = 0xFF;
  mem_[s_sd_blk_addr_ + 1] = 0xFF;
  mem_[s_sd_blk_addr_ + 2] = 0xFF;
  mem_[s_sd_blk_addr_ + 3] = 0xFF;
  W16(s_data_start_, 100); W8(s_spc_, 1);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=*/3));
  EXPECT_EQ(M8(s_sd_blk_addr_),     0x00u);
  EXPECT_EQ(M8(s_sd_blk_addr_ + 1), 0x00u);
}

TEST_F(ClusterToSectorTest, Cluster254MaximumSupported) {
  W16(s_data_start_, 50); W8(s_spc_, 1);
  ASSERT_TRUE(Run(s_clu2sec_, 0, 0, /*x=*/254));
  // (254-2)*1 + 50 = 302
  EXPECT_EQ(ResultD(), 302u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// validate_name
// Contract: Z=1 if string at X is null-terminated within 12 bytes; Z=0 otherwise.
//           X is preserved.
// ═══════════════════════════════════════════════════════════════════════════════

class ValidateNameTest : public Fat16Test {};

TEST_F(ValidateNameTest, EmptyStringIsValid) {
  ASSERT_TRUE(Run(s_validate_, 0, 0, Str("")));
  EXPECT_TRUE(ZFlag());
}

TEST_F(ValidateNameTest, ShortNameIsValid) {
  ASSERT_TRUE(Run(s_validate_, 0, 0, Str("A.B")));
  EXPECT_TRUE(ZFlag());
}

TEST_F(ValidateNameTest, MaximumLength12CharsIsValid) {
  // "LONGTEST.TXT" = 12 chars, null at byte 12.
  ASSERT_TRUE(Run(s_validate_, 0, 0, Str("LONGTEST.TXT")));
  EXPECT_TRUE(ZFlag());
}

TEST_F(ValidateNameTest, ThirteenCharsWithoutNullIsInvalid) {
  // 13 printable chars, no null in first 12 bytes.
  ASSERT_TRUE(Run(s_validate_, 0, 0, Str("TOOLONGNAMES.")));
  EXPECT_FALSE(ZFlag());
}

TEST_F(ValidateNameTest, ElevenCharsIsValid) {
  ASSERT_TRUE(Run(s_validate_, 0, 0, Str("12345678.AB")));
  EXPECT_TRUE(ZFlag());
}

TEST_F(ValidateNameTest, XRegisterPreservedOnReturn) {
  uint16_t addr = Str("TEST");
  ASSERT_TRUE(Run(s_validate_, 0, 0, addr));
  EXPECT_EQ(X(), addr);
}

// ═══════════════════════════════════════════════════════════════════════════════
// find_dir_entry
// Contract: searches fat_current_dir_buffer for an entry whose 8.3 name matches
// the null-terminated string in X and whose attr & A is non-zero.
// Returns: X = address of matching entry, or X = 0 if not found.
// ═══════════════════════════════════════════════════════════════════════════════

class FindDirEntryTest : public Fat16Test {
 protected:
  void SetUp() override {
    Fat16Test::SetUp();
    ClearDirBuf();
  }
};

TEST_F(FindDirEntryTest, FindsExactMatch) {
  SetDirEntry(0, "TEST    TXT", kAttrArchive, 5, 512);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("TEST.TXT")));
  EXPECT_EQ(X(), DirAddr(0));
}

TEST_F(FindDirEntryTest, ReturnsZeroWhenNotFound) {
  SetDirEntry(0, "OTHER   TXT", kAttrArchive, 5, 512);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("MISSING.TXT")));
  EXPECT_EQ(X(), 0u);
}

TEST_F(FindDirEntryTest, SkipsDeletedEntry) {
  SetDirEntry(0, "TEST    TXT", kAttrArchive, 5, 512);
  MarkDeleted(0);
  SetDirEntry(1, "TEST    TXT", kAttrArchive, 7, 256);
  TerminateDir(2);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("TEST.TXT")));
  EXPECT_EQ(X(), DirAddr(1));
}

TEST_F(FindDirEntryTest, FindsSubdirectory) {
  SetDirEntry(0, "MYDIR      ", kAttrSubdir, 10, 0);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrSubdir, 0, Str("MYDIR")));
  EXPECT_EQ(X(), DirAddr(0));
}

TEST_F(FindDirEntryTest, AttributeMismatchNotFound) {
  // Entry has subdir attribute; searching for archive should not match.
  SetDirEntry(0, "TEST    TXT", kAttrSubdir, 5, 0);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("TEST.TXT")));
  EXPECT_EQ(X(), 0u);
}

TEST_F(FindDirEntryTest, MatchesSecondEntry) {
  SetDirEntry(0, "FILE1   TXT", kAttrArchive, 5, 100);
  SetDirEntry(1, "FILE2   TXT", kAttrArchive, 6, 200);
  TerminateDir(2);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("FILE2.TXT")));
  EXPECT_EQ(X(), DirAddr(1));
}

TEST_F(FindDirEntryTest, FilenameWithNoExtension) {
  SetDirEntry(0, "MAKEFILE   ", kAttrArchive, 3, 0);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("MAKEFILE")));
  EXPECT_EQ(X(), DirAddr(0));
}

TEST_F(FindDirEntryTest, SingleCharName) {
  SetDirEntry(0, "A       TXT", kAttrArchive, 3, 0);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("A.TXT")));
  EXPECT_EQ(X(), DirAddr(0));
}

TEST_F(FindDirEntryTest, CaseSensitive) {
  // FAT16 stores uppercase names; lowercase search should not match.
  SetDirEntry(0, "TEST    TXT", kAttrArchive, 5, 100);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("test.txt")));
  EXPECT_EQ(X(), 0u);
}

TEST_F(FindDirEntryTest, ExtensionOnlyDiffers) {
  SetDirEntry(0, "TEST    BIN", kAttrArchive, 5, 100);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("TEST.TXT")));
  EXPECT_EQ(X(), 0u);
}

TEST_F(FindDirEntryTest, DotEntry) {
  SetDirEntry(0, ".          ", kAttrSubdir, 0, 0);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrSubdir, 0, Str(".")));
  EXPECT_EQ(X(), DirAddr(0));
}

TEST_F(FindDirEntryTest, DotDotEntry) {
  SetDirEntry(0, "..         ", kAttrSubdir, 0, 0);
  TerminateDir(1);
  ASSERT_TRUE(Run(s_find_dir_, kAttrSubdir, 0, Str("..")));
  EXPECT_EQ(X(), DirAddr(0));
}

TEST_F(FindDirEntryTest, GuardByteStopsSearchAtBufferEnd) {
  // Fill all 16 slots with non-matching entries; the guard byte terminates.
  for (int i = 0; i < 16; ++i)
    SetDirEntry(i, "OTHER   TXT", kAttrArchive, static_cast<uint16_t>(i + 2), 0);
  // Guard byte is already 0 from ClearDirBuf.
  ASSERT_TRUE(Run(s_find_dir_, kAttrArchive, 0, Str("NOTHERE.TXT")));
  EXPECT_EQ(X(), 0u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// file_close
// Contract: zeroes the state byte of the FD entry identified by A.
//           A >= FD_TABLE_ENTRIES is silently ignored.
// ═══════════════════════════════════════════════════════════════════════════════

class FileCloseTest : public Fat16Test {};

TEST_F(FileCloseTest, CloseFd0ClearsStateByte) {
  SetFdState(0, kFdStateOpen);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/0));
  EXPECT_EQ(GetFdState(0), 0x00u);
}

TEST_F(FileCloseTest, CloseFd1ClearsStateByte) {
  SetFdState(1, kFdStateOpen);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/1));
  EXPECT_EQ(GetFdState(1), 0x00u);
}

TEST_F(FileCloseTest, CloseFd0DoesNotAffectFd1) {
  SetFdState(0, kFdStateOpen);
  SetFdState(1, kFdStateOpen);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/0));
  EXPECT_EQ(GetFdState(1), kFdStateOpen);
}

TEST_F(FileCloseTest, CloseFd1DoesNotAffectFd0) {
  SetFdState(0, kFdStateOpen);
  SetFdState(1, kFdStateOpen);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/1));
  EXPECT_EQ(GetFdState(0), kFdStateOpen);
}

TEST_F(FileCloseTest, InvalidFdIsIgnored) {
  SetFdState(0, kFdStateOpen);
  SetFdState(1, kFdStateOpen);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/2));
  EXPECT_EQ(GetFdState(0), kFdStateOpen);
  EXPECT_EQ(GetFdState(1), kFdStateOpen);
}

TEST_F(FileCloseTest, CloseAlreadyClosedFdIsIdempotent) {
  SetFdState(0, 0x00);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/0));
  EXPECT_EQ(GetFdState(0), 0x00u);
}

TEST_F(FileCloseTest, FdNumberAtMaxBoundaryIsIgnored) {
  // A = 0xFF is beyond FD_TABLE_ENTRIES (2); must be a no-op.
  SetFdState(0, kFdStateOpen);
  SetFdState(1, kFdStateOpen);
  ASSERT_TRUE(Run(s_file_close_, /*a=fd*/0xFF));
  EXPECT_EQ(GetFdState(0), kFdStateOpen);
  EXPECT_EQ(GetFdState(1), kFdStateOpen);
}

// ═══════════════════════════════════════════════════════════════════════════════
// file_at_eof
// Contract: sets Z if read_ptr == file_size for the FD given in A.
//           All registers (A, B, X) are preserved.
// ═══════════════════════════════════════════════════════════════════════════════

class FileAtEofTest : public Fat16Test {};

TEST_F(FileAtEofTest, AtEofWhenReadPtrEqualsFileSize) {
  SetFdState(0, kFdStateOpen);
  SetFdSize(0, 0x0400);
  SetFdReadPtr(0, 0x0400);
  ASSERT_TRUE(Run(s_file_at_eof_, /*a=fd*/0));
  EXPECT_TRUE(ZFlag());
}

TEST_F(FileAtEofTest, NotAtEofWhenReadPtrBelowFileSize) {
  SetFdState(0, kFdStateOpen);
  SetFdSize(0, 0x0400);
  SetFdReadPtr(0, 0x0200);
  ASSERT_TRUE(Run(s_file_at_eof_, /*a=fd*/0));
  EXPECT_FALSE(ZFlag());
}

TEST_F(FileAtEofTest, ZeroLengthFileIsAtEof) {
  SetFdState(0, kFdStateOpen);
  SetFdSize(0, 0x0000);
  SetFdReadPtr(0, 0x0000);
  ASSERT_TRUE(Run(s_file_at_eof_, /*a=fd*/0));
  EXPECT_TRUE(ZFlag());
}

TEST_F(FileAtEofTest, RegistersPreserved) {
  SetFdState(0, kFdStateOpen);
  SetFdSize(0, 0x0200);
  SetFdReadPtr(0, 0x0200);
  ASSERT_TRUE(Run(s_file_at_eof_, /*a=fd*/0, /*b=*/0x42, /*x=*/0x1234));
  EXPECT_EQ(A(),    0x00u);
  EXPECT_EQ(B(),    0x42u);
  EXPECT_EQ(X(), 0x1234u);
}

TEST_F(FileAtEofTest, Fd1AtEof) {
  SetFdState(1, kFdStateOpen);
  SetFdSize(1, 0x0800);
  SetFdReadPtr(1, 0x0800);
  ASSERT_TRUE(Run(s_file_at_eof_, /*a=fd*/1));
  EXPECT_TRUE(ZFlag());
}

TEST_F(FileAtEofTest, Fd1NotAtEof) {
  SetFdState(1, kFdStateOpen);
  SetFdSize(1, 0x0800);
  SetFdReadPtr(1, 0x0200);
  ASSERT_TRUE(Run(s_file_at_eof_, /*a=fd*/1));
  EXPECT_FALSE(ZFlag());
}

// ═══════════════════════════════════════════════════════════════════════════════
// fat_cluster_offset
// Contract: from start cluster X, hop A times through the FAT chain.
//           Returns final cluster in X, A=0 on success.
//           Tests pre-populate fat_buffer with chain data and mark it loaded
//           (fat_buffer_sector = 0) to bypass the SD card read.
// ═══════════════════════════════════════════════════════════════════════════════

class ClusterOffsetTest : public Fat16Test {
 protected:
  void SetUp() override {
    Fat16Test::SetUp();
    SetBpbDefaults();
    // Mark FAT sector 0 as already loaded so no SD read is triggered.
    W8(s_fat_buf_sector_, 0x00);
  }

  // Write the FAT entry for `cluster` to point to `next` (little-endian 16-bit).
  void SetFatEntry(uint8_t cluster, uint16_t next) {
    uint16_t off = static_cast<uint16_t>(s_fat_buf_ + cluster * 2);
    mem_[off]     = next & 0xFF;
    mem_[off + 1] = next >> 8;
  }
};

TEST_F(ClusterOffsetTest, ZeroHopsReturnsStartCluster) {
  // Set an end-of-chain marker so the loop terminates if entered.
  SetFatEntry(5, 0xFFFF);
  ASSERT_TRUE(Run(s_clu_offset_, /*a=hops*/0, 0, /*x=start*/5));
  // Expects the start cluster (5) when no hops are requested.
  EXPECT_EQ(X(), 5u);
  EXPECT_EQ(A(), 0u);
}

TEST_F(ClusterOffsetTest, OneHopFollowsOneLink) {
  SetFatEntry(5, 6);
  SetFatEntry(6, 0xFFFF);
  ASSERT_TRUE(Run(s_clu_offset_, /*a=hops*/1, 0, /*x=start*/5));
  EXPECT_EQ(X(), 6u);
  EXPECT_EQ(A(), 0u);
}

TEST_F(ClusterOffsetTest, TwoHopsFollowsTwoLinks) {
  SetFatEntry(5, 6);
  SetFatEntry(6, 7);
  SetFatEntry(7, 0xFFFF);
  ASSERT_TRUE(Run(s_clu_offset_, /*a=hops*/2, 0, /*x=start*/5));
  EXPECT_EQ(X(), 7u);
  EXPECT_EQ(A(), 0u);
}

TEST_F(ClusterOffsetTest, ClusterAbove254ReturnsError) {
  // Cluster 255 exceeds the maximum supported value (254).
  ASSERT_TRUE(Run(s_clu_offset_, /*a=*/0, 0, /*x=*/255));
  EXPECT_GT(A(), 127u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// file_open
// Contract: finds a free FD, locates the named file in the current directory,
//           populates the FD struct, and returns the FD number in A (<=127).
//           Returns A=128 (not found), A=129 (no free FD) on error.
// ═══════════════════════════════════════════════════════════════════════════════

class FileOpenTest : public Fat16Test {
 protected:
  void SetUp() override {
    Fat16Test::SetUp();
    SetBpbDefaults();
    ClearDirBuf();
    // Clear FD table.
    std::fill(mem_.begin() + s_fd_table_,
              mem_.begin() + s_fd_table_ + kFdTableEntries * kFdStructSize,
              uint8_t{0});
  }

  // Call file_open with X=name and D=kIoBuffer; return A on completion.
  uint8_t Open(const char* name) {
    EXPECT_TRUE(Run(s_file_open_,
                    static_cast<uint8_t>(kIoBuffer >> 8),
                    static_cast<uint8_t>(kIoBuffer & 0xFF),
                    Str(name)));
    return A();
  }
};

TEST_F(FileOpenTest, OpenExistingFileAllocatesFd0) {
  SetDirEntry(0, "TEST    TXT", kAttrArchive, 2, 1024);
  TerminateDir(1);
  uint8_t fd = Open("TEST.TXT");
  EXPECT_LE(fd, 127u) << "Success: FD number must be <=127";
  EXPECT_EQ(fd, 0u)   << "FD 0 should be the first allocated";
}

TEST_F(FileOpenTest, FileNotFoundReturns128) {
  TerminateDir(0);
  EXPECT_EQ(Open("MISSING.TXT"), 128u);
}

TEST_F(FileOpenTest, NoFreeFdReturns129) {
  SetFdState(0, kFdStateOpen);
  SetFdState(1, kFdStateOpen);
  SetDirEntry(0, "TEST    TXT", kAttrArchive, 2, 512);
  TerminateDir(1);
  EXPECT_EQ(Open("TEST.TXT"), 129u);
}

TEST_F(FileOpenTest, SecondOpenWhenFd0InUseAllocatesFd1) {
  SetFdState(0, kFdStateOpen);  // FD 0 occupied
  SetDirEntry(0, "DATA    BIN", kAttrArchive, 2, 512);
  TerminateDir(1);
  uint8_t fd = Open("DATA.BIN");
  EXPECT_LE(fd, 127u) << "Success: FD number must be <=127";
  EXPECT_EQ(fd, 1u)   << "FD 1 should be allocated when FD 0 is busy";
}

TEST_F(FileOpenTest, PopulatesFileSizeFromDirEntry) {
  SetDirEntry(0, "DATA    BIN", kAttrArchive, 2, 600);
  TerminateDir(1);
  Open("DATA.BIN");
  EXPECT_EQ(GetFdSize(0), 600u);
}

TEST_F(FileOpenTest, PopulatesReadPtrAsZero) {
  SetDirEntry(0, "DATA    BIN", kAttrArchive, 2, 600);
  TerminateDir(1);
  Open("DATA.BIN");
  EXPECT_EQ(GetFdReadPtr(0), 0u);
}

TEST_F(FileOpenTest, PopulatesFdStateOpen) {
  SetDirEntry(0, "DATA    BIN", kAttrArchive, 2, 600);
  TerminateDir(1);
  Open("DATA.BIN");
  EXPECT_EQ(GetFdState(0), kFdStateOpen);
}

// ═══════════════════════════════════════════════════════════════════════════════
// file_read
// Contract: reads the next 512-byte sector for the FD in A.
//           Returns bytes-read in X; A is unchanged on success.
//           Error codes: 128=bad FD, 129=FD not open, 130=SD error.
//
// Because sd_card_read_block is stubbed (no-op), these tests only verify the
// bookkeeping (read pointer advancement, byte count returned).  The actual
// sector address calculation is exercised indirectly via the SD block address.
// ═══════════════════════════════════════════════════════════════════════════════

class FileReadTest : public Fat16Test {
 protected:
  void SetUp() override {
    Fat16Test::SetUp();
    SetBpbDefaults();
  }

  // Configure FD 0 to a ready-to-read state.
  void SetupFd0(uint16_t file_size, uint16_t read_ptr,
                uint16_t start_cluster = 2) {
    uint16_t start_sector =
        static_cast<uint16_t>(35 + (start_cluster - 2));  // data_start=35, spc=1
    SetFdState(0, kFdStateOpen);
    SetFdIoAddr(0, kIoBuffer);
    SetFdStartCluster(0, start_cluster);
    SetFdStartSector(0, start_sector);
    SetFdSize(0, file_size);
    SetFdReadPtr(0, read_ptr);
  }
};

TEST_F(FileReadTest, InvalidFdNumberReturnsError) {
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/5));
  EXPECT_EQ(A(), 128u);
  EXPECT_EQ(X(), 0u);
}

TEST_F(FileReadTest, ClosedFdReturnsError) {
  SetFdState(0, 0x00);
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/0));
  EXPECT_EQ(A(), 129u);
  EXPECT_EQ(X(), 0u);
}

TEST_F(FileReadTest, ReadAtEofReturnsZeroBytes) {
  SetupFd0(/*file_size=*/512, /*read_ptr=*/512);
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/0));
  EXPECT_EQ(X(), 0u);
}

TEST_F(FileReadTest, FirstReadOfTwoSectorFileReturns512Bytes) {
  SetupFd0(/*file_size=*/1024, /*read_ptr=*/0);
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/0));
  EXPECT_EQ(X(), 512u);
  EXPECT_EQ(GetFdReadPtr(0), 512u);
}

TEST_F(FileReadTest, SecondReadOfTwoSectorFileReturns512Bytes) {
  SetupFd0(/*file_size=*/1024, /*read_ptr=*/512);
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/0));
  EXPECT_EQ(X(), 512u);
  EXPECT_EQ(GetFdReadPtr(0), 1024u);
}

TEST_F(FileReadTest, PartialLastSectorReturnsCorrectCount) {
  // File is 600 bytes; first 512 already read.
  SetupFd0(/*file_size=*/600, /*read_ptr=*/512);
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/0));
  // 600 - 512 = 88 bytes in the final sector.
  EXPECT_EQ(X(), 88u);
  EXPECT_EQ(GetFdReadPtr(0), 600u);
}

TEST_F(FileReadTest, ExactlyOneSectorFileReturns512Bytes) {
  SetupFd0(/*file_size=*/512, /*read_ptr=*/0);
  ASSERT_TRUE(Run(s_file_read_, /*a=fd*/0));
  EXPECT_EQ(X(), 512u);
  EXPECT_EQ(GetFdReadPtr(0), 512u);
}

// ═══════════════════════════════════════════════════════════════════════════════
// file_init — BPB parsing
// Pre-populate fat_current_dir_buffer with a synthetic BPB.  The SD stub does
// nothing, so the buffer content remains available for the parser.
// ═══════════════════════════════════════════════════════════════════════════════

class FileInitTest : public Fat16Test {
 protected:
  // Write a minimal valid FAT16 BPB into fat_current_dir_buffer.
  // All multi-byte fields are in little-endian (as on disk).
  void WriteBpb(uint16_t bytes_per_sector, uint8_t num_fats, uint8_t spc,
                uint16_t reserved, uint16_t root_entries, uint16_t spf) {
    uint16_t b = s_dir_buf_;
    std::fill(mem_.begin() + b, mem_.begin() + b + 512, uint8_t{0});
    mem_[b + 0x0b] = bytes_per_sector & 0xFF;
    mem_[b + 0x0c] = bytes_per_sector >> 8;
    mem_[b + 0x0d] = spc;
    mem_[b + 0x0e] = reserved & 0xFF;
    mem_[b + 0x0f] = reserved >> 8;
    mem_[b + 0x10] = num_fats;
    mem_[b + 0x11] = root_entries & 0xFF;
    mem_[b + 0x12] = root_entries >> 8;
    mem_[b + 0x16] = spf & 0xFF;
    mem_[b + 0x17] = spf >> 8;
  }
};

TEST_F(FileInitTest, ParsesSectorsPerCluster) {
  WriteBpb(512, 2, /*spc=*/4, 1, 512, 1);
  ASSERT_TRUE(Run(s_file_init_));
  EXPECT_EQ(M8(s_spc_), 4u);
}

TEST_F(FileInitTest, ComputesSpcMaskForFourSectorsPerCluster) {
  // spc=4 → mask=3 (spc-1).
  WriteBpb(512, 2, /*spc=*/4, 1, 512, 1);
  ASSERT_TRUE(Run(s_file_init_));
  EXPECT_EQ(M8(s_spc_mask_), 3u);
}

TEST_F(FileInitTest, ComputesSpcMaskForOneSectorPerCluster) {
  // spc=1 → mask=0.
  WriteBpb(512, 2, /*spc=*/1, 1, 512, 1);
  ASSERT_TRUE(Run(s_file_init_));
  EXPECT_EQ(M8(s_spc_mask_), 0u);
}

TEST_F(FileInitTest, ComputesSpcMaskForEightSectorsPerCluster) {
  // spc=8 → mask=7.
  WriteBpb(512, 2, /*spc=*/8, 1, 512, 1);
  ASSERT_TRUE(Run(s_file_init_));
  EXPECT_EQ(M8(s_spc_mask_), 7u);
}

TEST_F(FileInitTest, ComputesRootDirStartSector) {
  // root_dir_start = reserved + num_fats * spf = 1 + 2*1 = 3.
  WriteBpb(512, 2, 1, /*reserved=*/1, 512, /*spf=*/1);
  ASSERT_TRUE(Run(s_file_init_));
  EXPECT_EQ(M16(s_root_dir_), 3u);
}

TEST_F(FileInitTest, ComputesDataStartSector) {
  // root_sectors = root_entries / 16 = 512 / 16 = 32.
  // data_start = root_dir_start + root_sectors = 3 + 32 = 35.
  WriteBpb(512, 2, 1, /*reserved=*/1, /*root_entries=*/512, /*spf=*/1);
  ASSERT_TRUE(Run(s_file_init_));
  EXPECT_EQ(M16(s_data_start_), 35u);
}

TEST_F(FileInitTest, ReturnsGracefullyOnWrongBytesPerSector) {
  // 1024 bytes/sector is not supported; should print an error and return.
  WriteBpb(/*bytes_per_sector=*/1024, 2, 1, 1, 512, 1);
  ASSERT_TRUE(Run(s_file_init_)) << "Must not hang on invalid BPB";
}

TEST_F(FileInitTest, ReturnsGracefullyOnWrongFatCount) {
  WriteBpb(512, /*num_fats=*/1, 1, 1, 512, 1);
  ASSERT_TRUE(Run(s_file_init_)) << "Must not hang on invalid FAT count";
}

}  // namespace
}  // namespace eight_bit
