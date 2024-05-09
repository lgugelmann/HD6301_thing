#include "hexdump.h"

#include <gtest/gtest.h>

namespace eight_bit {
namespace {

TEST(HexdumpTest, EmptyData) {
  std::vector<uint8_t> data;
  std::string result = hexdump(data);
  EXPECT_EQ(result, "");
}

TEST(HexdumpTest, ThirtyTwoPrintableBytes) {
  std::vector<uint8_t> data(32);
  for (int i = 0; i < data.size(); ++i) {
    data[i] = 'A' + i;
  }

  std::string result = hexdump(data);
  EXPECT_EQ(result,
            "00000000  41 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  "
            "|ABCDEFGHIJKLMNOP|\n"
            "00000010  51 52 53 54 55 56 57 58  59 5a 5b 5c 5d 5e 5f 60  "
            "|QRSTUVWXYZ[\\]^_`|\n");
}

TEST(HexdumpTest, NonAlignedOffsetAddsSpacesAtTheStart) {
  std::vector<uint8_t> data(32 - 5);
  for (int i = 0; i < data.size(); ++i) {
    data[i] = 'A' + i;
  }

  std::string result = hexdump(data, 5);
  EXPECT_EQ(result,
            "00000000                 41 42 43  44 45 46 47 48 49 4a 4b  "
            "|     ABCDEFGHIJK|\n"
            "00000010  4c 4d 4e 4f 50 51 52 53  54 55 56 57 58 59 5a 5b  "
            "|LMNOPQRSTUVWXYZ[|\n");
}

TEST(HexdumpTest, NonAlignedSizeSkipsDataAtTheEnd) {
  std::vector<uint8_t> data(26);
  for (int i = 0; i < data.size(); ++i) {
    data[i] = 'A' + i;
  }

  std::string result = hexdump(data);
  EXPECT_EQ(result,
            "00000000  41 42 43 44 45 46 47 48  49 4a 4b 4c 4d 4e 4f 50  "
            "|ABCDEFGHIJKLMNOP|\n"
            "00000010  51 52 53 54 55 56 57 58  59 5a                    "
            "|QRSTUVWXYZ|\n");
}

TEST(HexdumpTest, SingleLineMisalignedAtBothEnds) {
  std::vector<uint8_t> data(5);
  for (int i = 0; i < data.size(); ++i) {
    data[i] = 'A' + i;
  }

  std::string result = hexdump(data, 5);
  EXPECT_EQ(result,
            "00000000                 41 42 43  44 45                    "
            "|     ABCDE|\n");
}

TEST(HexdumpTest, NonPrintableCharactersAreDots) {
  std::vector<uint8_t> data(16);
  for (int i = 0; i < data.size(); ++i) {
    data[i] = i;
  }

  std::string result = hexdump(data);
  EXPECT_EQ(result,
            "00000000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
            "|................|\n");
}

TEST(HexdumpTest, RepeatedLinesAreReplacedByStar) {
  std::vector<uint8_t> data(80);
  for (int i = 0; i < 16; ++i) {
    for (int j = 0; j < 4; ++j) {
      data[i + 16 * j] = i;
    }
  }
  for (int i = 64; i < 80; ++i) {
    data[i] = i;
  }

  std::string result = hexdump(data);
  EXPECT_EQ(result,
            "00000000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  "
            "|................|\n"
            "*\n"
            "00000040  40 41 42 43 44 45 46 47  48 49 4a 4b 4c 4d 4e 4f  "
            "|@ABCDEFGHIJKLMNO|\n");
}

TEST(HexdumpTest, OffsetRepeatedLinesAreNotReplacedByStar) {
  std::vector<uint8_t> data(80);
  for (int i = 0; i < 16; ++i) {
    for (int j = 0; j < 4; ++j) {
      data[i + 16 * j] = i;
    }
  }
  for (int i = 64; i < 80; ++i) {
    data[i] = i;
  }

  // An offset of 3 bytes makes the first two lines different. Without the
  // offset, they would be identical.
  std::string result = hexdump(data, 3);
  EXPECT_EQ(result,
            "00000000           00 01 02 03 04  05 06 07 08 09 0a 0b 0c  "
            "|   .............|\n"
            "00000010  0d 0e 0f 00 01 02 03 04  05 06 07 08 09 0a 0b 0c  "
            "|................|\n"
            "*\n"
            "00000040  0d 0e 0f 40 41 42 43 44  45 46 47 48 49 4a 4b 4c  "
            "|...@ABCDEFGHIJKL|\n"
            "00000050  4d 4e 4f                                          "
            "|MNO|\n");
}

}  // namespace
}  // namespace eight_bit