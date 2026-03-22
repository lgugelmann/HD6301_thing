#include "instruction_test_parser.h"

#include <charconv>
#include <fstream>
#include <ostream>
#include <sstream>

namespace eight_bit {
namespace {

std::string Trim(std::string_view s) {
  while (!s.empty() && std::isspace(s.front())) s.remove_prefix(1);
  while (!s.empty() && std::isspace(s.back())) s.remove_suffix(1);
  return std::string(s);
}

// Parse a hex byte like "8B" into uint8_t.
bool ParseHexByte(std::string_view s, uint8_t& out) {
  if (s.empty() || s.size() > 2) return false;
  auto r = std::from_chars(s.data(), s.data() + s.size(), out, 16);
  return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

// Parse a hex value into uint16_t (1-4 hex digits).
bool ParseHex16(std::string_view s, uint16_t& out) {
  if (s.empty() || s.size() > 4) return false;
  auto r = std::from_chars(s.data(), s.data() + s.size(), out, 16);
  return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}

// Parse space-separated hex bytes like "8B 42 FF".
bool ParseHexBytes(std::string_view s, std::vector<uint8_t>& out) {
  std::istringstream iss{std::string(s)};
  std::string token;
  while (iss >> token) {
    uint8_t byte;
    if (!ParseHexByte(token, byte)) return false;
    out.push_back(byte);
  }
  return !out.empty();
}

// Parse key=value pairs like "a=10 b=FF x=1234 N=1 Z=0".
// Applies values to the given test case depending on 'is_expect'.
std::string ParseKeyValues(std::string_view s, InstructionTestCase& tc,
                           bool is_expect) {
  std::istringstream iss{std::string(s)};
  std::string token;
  while (iss >> token) {
    auto eq = token.find('=');
    if (eq == std::string::npos) return "missing '=' in: " + token;
    std::string key = token.substr(0, eq);
    std::string val = token.substr(eq + 1);

    uint16_t v16;
    uint8_t v8;

    if (key == "a") {
      if (!ParseHexByte(val, v8)) return "bad hex for a: " + val;
      if (is_expect)
        tc.expect_a = v8;
      else
        tc.a = v8;
    } else if (key == "b") {
      if (!ParseHexByte(val, v8)) return "bad hex for b: " + val;
      if (is_expect)
        tc.expect_b = v8;
      else
        tc.b = v8;
    } else if (key == "x") {
      if (!ParseHex16(val, v16)) return "bad hex for x: " + val;
      if (is_expect)
        tc.expect_x = v16;
      else
        tc.x = v16;
    } else if (key == "sp") {
      if (!ParseHex16(val, v16)) return "bad hex for sp: " + val;
      if (is_expect)
        tc.expect_sp = v16;
      else
        tc.sp = v16;
    } else if (key == "pc") {
      if (!ParseHex16(val, v16)) return "bad hex for pc: " + val;
      if (is_expect)
        tc.expect_pc = v16;
      else
        tc.pc = v16;
    } else if (key == "sr") {
      if (!ParseHexByte(val, v8)) return "bad hex for sr: " + val;
      if (is_expect)
        tc.expect_sr = v8;
      else
        tc.sr = v8;
    } else if (key == "H" || key == "I" || key == "N" || key == "Z" ||
               key == "V" || key == "C") {
      if (val != "0" && val != "1") return "bad flag value for " + key + ": " + val;
      bool flag = (val == "1");
      if (!is_expect) {
        // For setup, apply to sr directly.
        uint8_t bit = 0;
        if (key == "H") bit = 0x20;
        else if (key == "I") bit = 0x10;
        else if (key == "N") bit = 0x08;
        else if (key == "Z") bit = 0x04;
        else if (key == "V") bit = 0x02;
        else if (key == "C") bit = 0x01;
        if (flag)
          tc.sr |= bit;
        else
          tc.sr &= ~bit;
      } else {
        if (key == "H") tc.expect_H = flag;
        else if (key == "I") tc.expect_I = flag;
        else if (key == "N") tc.expect_N = flag;
        else if (key == "Z") tc.expect_Z = flag;
        else if (key == "V") tc.expect_V = flag;
        else if (key == "C") tc.expect_C = flag;
      }
    } else {
      return "unknown key: " + key;
    }
  }
  return "";
}

// Parse a single test case block (lines between --- separators).
std::string ParseBlock(const std::vector<std::string>& lines,
                       const std::string& filename,
                       InstructionTestCase& tc) {
  tc.file = filename;
  for (const auto& line : lines) {
    std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed[0] == '#') continue;

    auto colon = trimmed.find(':');
    if (colon == std::string::npos)
      return "line missing ':' — " + trimmed;

    std::string key = Trim(trimmed.substr(0, colon));
    std::string value = Trim(trimmed.substr(colon + 1));

    if (key == "name") {
      tc.name = value;
    } else if (key == "memory") {
      if (!ParseHexBytes(value, tc.memory))
        return "bad memory hex: " + value;
    } else if (key == "setup") {
      auto err = ParseKeyValues(value, tc, false);
      if (!err.empty()) return "setup: " + err;
    } else if (key == "expect") {
      auto err = ParseKeyValues(value, tc, true);
      if (!err.empty()) return "expect: " + err;
    } else if (key == "cycles") {
      int c;
      auto r = std::from_chars(value.data(), value.data() + value.size(), c);
      if (r.ec != std::errc{}) return "bad cycles value: " + value;
      tc.cycles = c;
    } else if (key.starts_with("at ")) {
      // "at XXXX" — pre-load memory
      std::string addr_str = Trim(key.substr(3));
      uint16_t addr;
      if (!ParseHex16(addr_str, addr))
        return "bad address in 'at': " + addr_str;
      std::vector<uint8_t> bytes;
      if (!ParseHexBytes(value, bytes))
        return "bad hex bytes in 'at " + addr_str + "': " + value;
      tc.memory_at[addr] = std::move(bytes);
    } else if (key.starts_with("expect_at ")) {
      // "expect_at XXXX" — expected memory after execution
      std::string addr_str = Trim(key.substr(10));
      uint16_t addr;
      if (!ParseHex16(addr_str, addr))
        return "bad address in 'expect_at': " + addr_str;
      // Value can be multiple bytes.
      std::vector<uint8_t> bytes;
      if (!ParseHexBytes(value, bytes))
        return "bad hex bytes in 'expect_at " + addr_str + "': " + value;
      for (size_t i = 0; i < bytes.size(); ++i) {
        tc.expect_at[addr + i] = bytes[i];
      }
    } else {
      return "unknown field: " + key;
    }
  }

  if (tc.name.empty()) return "missing 'name' field";
  if (tc.memory.empty()) return "missing 'memory' field";
  return "";
}

}  // namespace

ParseResult ParseTestFile(const std::filesystem::path& path) {
  ParseResult result;
  std::ifstream file(path);
  if (!file.is_open()) {
    result.error = "cannot open file: " + path.string();
    return result;
  }

  std::string filename = path.filename().string();
  std::vector<std::string> block_lines;
  bool in_block = false;
  int block_number = 0;

  auto flush_block = [&]() -> std::string {
    if (!in_block || block_lines.empty()) return "";
    // Check if block has any non-empty, non-comment lines.
    bool has_content = false;
    for (const auto& l : block_lines) {
      std::string t = Trim(l);
      if (!t.empty() && t[0] != '#') {
        has_content = true;
        break;
      }
    }
    if (!has_content) return "";

    ++block_number;
    InstructionTestCase tc;
    auto err = ParseBlock(block_lines, filename, tc);
    if (!err.empty())
      return filename + " block " + std::to_string(block_number) + ": " + err;
    result.cases.push_back(std::move(tc));
    return "";
  };

  std::string line;
  while (std::getline(file, line)) {
    std::string trimmed = Trim(line);
    if (trimmed == "---") {
      auto err = flush_block();
      if (!err.empty()) {
        result.error = err;
        return result;
      }
      block_lines.clear();
      in_block = true;
      continue;
    }
    if (in_block) {
      block_lines.push_back(line);
    }
    // Lines before the first --- are file-level comments; ignore them.
  }

  // Flush the last block.
  auto err = flush_block();
  if (!err.empty()) {
    result.error = err;
    return result;
  }

  if (result.cases.empty()) {
    result.error = filename + ": no test cases found";
  }
  return result;
}

ParseResult ParseAllTestFiles(const std::filesystem::path& directory) {
  ParseResult combined;
  std::vector<std::filesystem::path> paths;
  for (const auto& entry : std::filesystem::directory_iterator(directory)) {
    if (entry.path().extension() == ".test") {
      paths.push_back(entry.path());
    }
  }
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    auto file_result = ParseTestFile(path);
    if (!file_result.error.empty()) {
      combined.error = file_result.error;
      return combined;
    }
    for (auto& tc : file_result.cases) {
      combined.cases.push_back(std::move(tc));
    }
  }
  return combined;
}

void PrintTo(const InstructionTestCase& tc, std::ostream* os) {
  *os << std::filesystem::path(tc.file).stem().string() << "/" << tc.name;
}

}  // namespace eight_bit
