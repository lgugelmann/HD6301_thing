#!/usr/bin/env python3
"""Generate missing instruction test files for the HD6301 emulator."""

import os

TESTDATA_DIR = os.path.join(os.path.dirname(__file__), "instructions")


def write_test(filename, content):
    path = os.path.join(TESTDATA_DIR, filename)
    if os.path.exists(path):
        print(f"SKIP (exists): {filename}")
        return
    with open(path, "w") as f:
        f.write(content)
    print(f"Created: {filename}")


# =============================================================================
# BRANCH INSTRUCTIONS
# =============================================================================
# For all branches: pc starts at 0x0020, offset at 0x0021.
# After fetching 2 bytes, PC = 0x0022.
# Taken branch with offset 0x0E: PC = 0x0022 + 14 = 0x0030
# Not-taken branch: PC = 0x0022 (falls through)

BRANCHES = {
    "bra": (0x20, "BRA - Branch Always",
            "# Always branches",
            "C=0 Z=0", "C=1 Z=1"),  # taken_flags, not_taken_flags (BRA always taken)
    "brn": (0x21, "BRN - Branch Never",
            "# Never branches",
            None, None),  # special handling
    "bhi": (0x22, "BHI - Branch if Higher (unsigned: C=0 AND Z=0)",
            "# Branches when C=0 and Z=0",
            "C=0 Z=0", "C=1"),
    "bls": (0x23, "BLS - Branch if Lower or Same (unsigned: C=1 OR Z=1, but not both)",
            "# Branches when exactly one of C or Z is set (implementation: C+Z==1)",
            "C=1 Z=0", "C=0 Z=0"),
    "bcc": (0x24, "BCC - Branch if Carry Clear",
            "# Branches when C=0",
            "C=0", "C=1"),
    "bcs": (0x25, "BCS - Branch if Carry Set",
            "# Branches when C=1",
            "C=1", "C=0"),
    "bne": (0x26, "BNE - Branch if Not Equal",
            "# Branches when Z=0",
            "Z=0", "Z=1"),
    "beq": (0x27, "BEQ - Branch if Equal",
            "# Branches when Z=1",
            "Z=1", "Z=0"),
    "bvc": (0x28, "BVC - Branch if Overflow Clear",
            "# Branches when V=0",
            "V=0", "V=1"),
    "bvs": (0x29, "BVS - Branch if Overflow Set",
            "# Branches when V=1",
            "V=1", "V=0"),
    "bpl": (0x2A, "BPL - Branch if Plus",
            "# Branches when N=0",
            "N=0", "N=1"),
    "bmi": (0x2B, "BMI - Branch if Minus",
            "# Branches when N=1",
            "N=1", "N=0"),
    "bge": (0x2C, "BGE - Branch if Greater or Equal (signed: N^V==0)",
            "# Branches when N XOR V == 0",
            "N=0 V=0", "N=1 V=0"),
    "blt": (0x2D, "BLT - Branch if Less Than (signed: N^V==1)",
            "# Branches when N XOR V == 1",
            "N=1 V=0", "N=0 V=0"),
    "bgt": (0x2E, "BGT - Branch if Greater Than (signed: Z==0 AND N^V==0)",
            "# Branches when Z=0 and N XOR V == 0",
            "Z=0 N=0 V=0", "Z=1 N=0 V=0"),
    "ble": (0x2F, "BLE - Branch if Less or Equal (signed: Z==1 OR N^V==1, but not both)",
            "# Branches when exactly one of Z or (N^V) is set (implementation: Z+(N^V)==1)",
            "Z=1 N=0 V=0", "Z=0 N=0 V=0"),
}


def sr_with_flags(flag_str):
    """Calculate SR byte value from flag string like 'C=1 Z=1'."""
    sr = 0xC0  # bits 7,6 always set
    flags = {}
    if flag_str:
        for part in flag_str.split():
            name, val = part.split("=")
            flags[name] = int(val)
    sr |= flags.get("H", 0) << 5
    sr |= flags.get("I", 0) << 4
    sr |= flags.get("N", 0) << 3
    sr |= flags.get("Z", 0) << 2
    sr |= flags.get("V", 0) << 1
    sr |= flags.get("C", 0)
    return sr


def gen_branches():
    for name, (opcode, title, comment, taken_flags, not_taken_flags) in BRANCHES.items():
        lines = [f"# {title} (opcode 0x{opcode:02X})", comment, ""]

        if name == "bra":
            # Always taken
            lines.append("---")
            lines.append("name: taken forward")
            lines.append(f"memory: {opcode:02X} 0E")
            lines.append("expect: pc=0030")
            lines.append("cycles: 3")
            lines.append("")
            lines.append("---")
            lines.append("name: taken backward")
            lines.append(f"memory: {opcode:02X} FC")
            lines.append("# 0x0022 + (-4) = 0x001E")
            lines.append("expect: pc=001E")
            lines.append("cycles: 3")
        elif name == "brn":
            # Never taken
            lines.append("---")
            lines.append("name: not taken")
            lines.append(f"memory: {opcode:02X} 0E")
            lines.append("expect: pc=0022")
            lines.append("cycles: 3")
            lines.append("")
            lines.append("---")
            lines.append("name: not taken with flags set")
            lines.append(f"memory: {opcode:02X} 0E")
            sr = sr_with_flags("N=1 Z=1 V=1 C=1")
            lines.append(f"setup: sr={sr:02X}")
            lines.append("expect: pc=0022")
            lines.append("cycles: 3")
        else:
            # Taken case
            sr_taken = sr_with_flags(taken_flags)
            lines.append("---")
            lines.append("name: taken")
            lines.append(f"memory: {opcode:02X} 0E")
            lines.append(f"setup: sr={sr_taken:02X}")
            lines.append("expect: pc=0030")
            lines.append("cycles: 3")
            lines.append("")
            # Not taken case
            sr_not = sr_with_flags(not_taken_flags)
            lines.append("---")
            lines.append("name: not taken")
            lines.append(f"memory: {opcode:02X} 0E")
            lines.append(f"setup: sr={sr_not:02X}")
            lines.append("expect: pc=0022")
            lines.append("cycles: 3")

        write_test(f"{name}.test", "\n".join(lines) + "\n")


# =============================================================================
# ACCUMULATOR SINGLE-OPERAND INSTRUCTIONS
# =============================================================================

def gen_coma():
    write_test("coma.test", """\
# COMA - Complement A (opcode 0x43)
# ~A; N=bit7, Z=zero, V=0, C=1

---
name: complement nonzero
memory: 43
setup: a=0F
# ~0x0F = 0xF0
expect: a=F0 N=1 Z=0 V=0 C=1
cycles: 1

---
name: complement FF
memory: 43
setup: a=FF
# ~0xFF = 0x00
expect: a=00 N=0 Z=1 V=0 C=1
cycles: 1

---
name: complement zero
memory: 43
# ~0x00 = 0xFF
expect: a=FF N=1 Z=0 V=0 C=1
cycles: 1
""")


def gen_comb():
    write_test("comb.test", """\
# COMB - Complement B (opcode 0x53)
# ~B; N=bit7, Z=zero, V=0, C=1

---
name: complement nonzero
memory: 53
setup: b=0F
expect: b=F0 N=1 Z=0 V=0 C=1
cycles: 1

---
name: complement FF
memory: 53
setup: b=FF
expect: b=00 N=0 Z=1 V=0 C=1
cycles: 1
""")


def gen_lsra():
    write_test("lsra.test", """\
# LSRA - Logical Shift Right A (opcode 0x44)
# Bit 0 -> C, 0 -> bit 7. V = N ^ C.

---
name: shift right
memory: 44
setup: a=04
# 0x04 >> 1 = 0x02, C=0
expect: a=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: shift right odd
memory: 44
setup: a=03
# 0x03 >> 1 = 0x01, C=1, V=N^C=0^1=1
expect: a=01 N=0 Z=0 V=1 C=1
cycles: 1

---
name: shift right to zero
memory: 44
setup: a=01
# 0x01 >> 1 = 0x00, C=1, V=N^C=0^1=1
expect: a=00 N=0 Z=1 V=1 C=1
cycles: 1
""")


def gen_lsrb():
    write_test("lsrb.test", """\
# LSRB - Logical Shift Right B (opcode 0x54)

---
name: shift right
memory: 54
setup: b=04
expect: b=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: shift right odd
memory: 54
setup: b=03
expect: b=01 N=0 Z=0 V=1 C=1
cycles: 1
""")


def gen_rora():
    write_test("rora.test", """\
# RORA - Rotate Right A through Carry (opcode 0x46)
# Bit 0 -> C, old C -> bit 7. V = N ^ C.

---
name: rotate right carry clear
memory: 46
setup: a=02
# C=0 in, bit0=0 out. 0x02 >> 1 | 0<<7 = 0x01. C=0, V=0^0=0
expect: a=01 N=0 Z=0 V=0 C=0
cycles: 1

---
name: rotate right carry set
memory: 46
setup: a=00 C=1
# C=1 in, bit0=0 out. 0x00 >> 1 | 1<<7 = 0x80. C=0, N=1, V=1^0=1
expect: a=80 N=1 Z=0 V=1 C=0
cycles: 1

---
name: rotate right with bit0 set
memory: 46
setup: a=01
# C=0 in, bit0=1 out. 0x01 >> 1 | 0<<7 = 0x00. C=1, N=0, V=0^1=1
expect: a=00 N=0 Z=1 V=1 C=1
cycles: 1
""")


def gen_rorb():
    write_test("rorb.test", """\
# RORB - Rotate Right B through Carry (opcode 0x56)

---
name: rotate right carry clear
memory: 56
setup: b=02
expect: b=01 N=0 Z=0 V=0 C=0
cycles: 1

---
name: rotate right carry set
memory: 56
setup: b=00 C=1
expect: b=80 N=1 Z=0 V=1 C=0
cycles: 1
""")


def gen_asra():
    write_test("asra.test", """\
# ASRA - Arithmetic Shift Right A (opcode 0x47)
# Sign bit preserved. Bit 0 -> C. V = N ^ C.

---
name: shift positive
memory: 47
setup: a=04
# 0x04 >> 1 = 0x02, sign=0 preserved. C=0
expect: a=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: shift negative preserves sign
memory: 47
setup: a=80
# 0x80 >> 1 | 0x80 = 0xC0. C=0, N=1, V=1^0=1
expect: a=C0 N=1 Z=0 V=1 C=0
cycles: 1

---
name: shift odd negative
memory: 47
setup: a=FF
# 0xFF >> 1 | 0x80 = 0xFF. C=1, N=1, V=1^1=0
expect: a=FF N=1 Z=0 V=0 C=1
cycles: 1
""")


def gen_asrb():
    write_test("asrb.test", """\
# ASRB - Arithmetic Shift Right B (opcode 0x57)

---
name: shift positive
memory: 57
setup: b=04
expect: b=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: shift negative preserves sign
memory: 57
setup: b=80
expect: b=C0 N=1 Z=0 V=1 C=0
cycles: 1
""")


def gen_asla():
    write_test("asla.test", """\
# ASLA - Arithmetic Shift Left A (opcode 0x48)
# Bit 7 -> C, 0 -> bit 0. V = N ^ C.

---
name: shift left
memory: 48
setup: a=01
# 0x01 << 1 = 0x02. C=0
expect: a=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: shift left into carry
memory: 48
setup: a=80
# 0x80 << 1 = 0x00. C=1, N=0, V=0^1=1
expect: a=00 N=0 Z=1 V=1 C=1
cycles: 1

---
name: shift left with overflow
memory: 48
setup: a=40
# 0x40 << 1 = 0x80. C=0, N=1, V=1^0=1
expect: a=80 N=1 Z=0 V=1 C=0
cycles: 1
""")


def gen_aslb():
    write_test("aslb.test", """\
# ASLB - Arithmetic Shift Left B (opcode 0x58)

---
name: shift left
memory: 58
setup: b=01
expect: b=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: shift left into carry
memory: 58
setup: b=80
expect: b=00 N=0 Z=1 V=1 C=1
cycles: 1
""")


def gen_rola():
    write_test("rola.test", """\
# ROLA - Rotate Left A through Carry (opcode 0x49)
# Bit 7 -> C, old C -> bit 0. V = N ^ C.

---
name: rotate left carry clear
memory: 49
setup: a=01
# 0x01 << 1 | 0 = 0x02. C=0, N=0, V=0^0=0
expect: a=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: rotate left carry set
memory: 49
setup: a=00 C=1
# 0x00 << 1 | 1 = 0x01. C=0, N=0, V=0^0=0
expect: a=01 N=0 Z=0 V=0 C=0
cycles: 1

---
name: rotate left bit7 into carry
memory: 49
setup: a=80
# 0x80 << 1 | 0 = 0x00. C=1, N=0, V=0^1=1
expect: a=00 N=0 Z=1 V=1 C=1
cycles: 1
""")


def gen_rolb():
    write_test("rolb.test", """\
# ROLB - Rotate Left B through Carry (opcode 0x59)

---
name: rotate left carry clear
memory: 59
setup: b=01
expect: b=02 N=0 Z=0 V=0 C=0
cycles: 1

---
name: rotate left carry set
memory: 59
setup: b=00 C=1
expect: b=01 N=0 Z=0 V=0 C=0
cycles: 1
""")


def gen_deca():
    write_test("deca.test", """\
# DECA - Decrement A (opcode 0x4A)
# V = (old == 0x80); A -= 1; N,Z set from result.

---
name: basic decrement
memory: 4A
setup: a=05
expect: a=04 N=0 Z=0 V=0
cycles: 1

---
name: decrement to zero
memory: 4A
setup: a=01
expect: a=00 N=0 Z=1 V=0
cycles: 1

---
name: decrement zero wraps
memory: 4A
expect: a=FF N=1 Z=0 V=0
cycles: 1

---
name: decrement 0x80 sets overflow
memory: 4A
setup: a=80
# V=1 because old was 0x80
expect: a=7F N=0 Z=0 V=1
cycles: 1
""")


def gen_decb():
    write_test("decb.test", """\
# DECB - Decrement B (opcode 0x5A)

---
name: basic decrement
memory: 5A
setup: b=05
expect: b=04 N=0 Z=0 V=0
cycles: 1

---
name: decrement 0x80 sets overflow
memory: 5A
setup: b=80
expect: b=7F N=0 Z=0 V=1
cycles: 1
""")


def gen_inca():
    write_test("inca.test", """\
# INCA - Increment A (opcode 0x4C)
# V = (old == 0x7F); A += 1; N,Z set from result.

---
name: basic increment
memory: 4C
setup: a=05
expect: a=06 N=0 Z=0 V=0
cycles: 1

---
name: increment 0x7F sets overflow
memory: 4C
setup: a=7F
# V=1 because old was 0x7F
expect: a=80 N=1 Z=0 V=1
cycles: 1

---
name: increment FF wraps to zero
memory: 4C
setup: a=FF
expect: a=00 N=0 Z=1 V=0
cycles: 1
""")


def gen_incb():
    write_test("incb.test", """\
# INCB - Increment B (opcode 0x5C)

---
name: basic increment
memory: 5C
setup: b=05
expect: b=06 N=0 Z=0 V=0
cycles: 1

---
name: increment 0x7F sets overflow
memory: 5C
setup: b=7F
expect: b=80 N=1 Z=0 V=1
cycles: 1
""")


def gen_tsta():
    write_test("tsta.test", """\
# TSTA - Test A (opcode 0x4D)
# Compares A with 0. Sets N,Z,V,C. Does not modify A.

---
name: test positive
memory: 4D
setup: a=42
expect: a=42 N=0 Z=0 V=0 C=0
cycles: 1

---
name: test zero
memory: 4D
expect: a=00 N=0 Z=1 V=0 C=0
cycles: 1

---
name: test negative
memory: 4D
setup: a=80
expect: a=80 N=1 Z=0 V=0 C=0
cycles: 1
""")


def gen_tstb():
    write_test("tstb.test", """\
# TSTB - Test B (opcode 0x5D)

---
name: test positive
memory: 5D
setup: b=42
expect: b=42 N=0 Z=0 V=0 C=0
cycles: 1

---
name: test zero
memory: 5D
expect: b=00 N=0 Z=1 V=0 C=0
cycles: 1
""")


def gen_clra():
    write_test("clra.test", """\
# CLRA - Clear A (opcode 0x4F)
# A=0; N=0, Z=1, V=0, C=0

---
name: clear nonzero
memory: 4F
setup: a=42
expect: a=00 N=0 Z=1 V=0 C=0
cycles: 1

---
name: clear already zero
memory: 4F
expect: a=00 N=0 Z=1 V=0 C=0
cycles: 1
""")


def gen_clrb():
    write_test("clrb.test", """\
# CLRB - Clear B (opcode 0x5F)

---
name: clear nonzero
memory: 5F
setup: b=42
expect: b=00 N=0 Z=1 V=0 C=0
cycles: 1
""")


# =============================================================================
# MEMORY SINGLE-OPERAND INSTRUCTIONS (IDX + EXT)
# =============================================================================
# For IDX: opcode + offset. Address = X + offset.
# For EXT: opcode + addr_hi + addr_lo. Address = addr.

def gen_neg_mem():
    write_test("neg.test", """\
# NEG - Negate Memory (2's complement)
# Indexed: 0x60, Extended: 0x70

---
name: indexed
memory: 60 10
setup: x=1000
at 1010: 0A
# -0x0A = 0xF6
expect: N=1 C=1
expect_at 1010: F6
cycles: 6

---
name: extended
memory: 70 12 34
at 1234: 0A
expect: N=1 C=1
expect_at 1234: F6
cycles: 6

---
name: negate zero extended
memory: 70 12 34
at 1234: 00
expect: N=0 Z=1 V=0 C=0
expect_at 1234: 00
cycles: 6

---
name: negate 0x80 sets overflow
memory: 70 12 34
at 1234: 80
# -0x80 = 0x80 (wraps)
expect: N=1 Z=0 V=1 C=1
expect_at 1234: 80
cycles: 6
""")


def gen_com_mem():
    write_test("com.test", """\
# COM - Complement Memory (1's complement)
# Indexed: 0x63, Extended: 0x73

---
name: indexed
memory: 63 10
setup: x=1000
at 1010: 0F
expect: N=1 Z=0 V=0 C=1
expect_at 1010: F0
cycles: 6

---
name: extended
memory: 73 12 34
at 1234: FF
expect: N=0 Z=1 V=0 C=1
expect_at 1234: 00
cycles: 6
""")


def gen_lsr_mem():
    write_test("lsr.test", """\
# LSR - Logical Shift Right Memory
# Indexed: 0x64, Extended: 0x74

---
name: indexed
memory: 64 10
setup: x=1000
at 1010: 04
expect: N=0 Z=0 V=0 C=0
expect_at 1010: 02
cycles: 6

---
name: extended with carry
memory: 74 12 34
at 1234: 03
# 0x03 >> 1 = 0x01, C=1, V=N^C=0^1=1
expect: N=0 Z=0 V=1 C=1
expect_at 1234: 01
cycles: 6
""")


def gen_ror_mem():
    write_test("ror.test", """\
# ROR - Rotate Right Memory through Carry
# Indexed: 0x66, Extended: 0x76

---
name: indexed carry clear
memory: 66 10
setup: x=1000
at 1010: 02
expect: N=0 Z=0 V=0 C=0
expect_at 1010: 01
cycles: 6

---
name: extended carry in
memory: 76 12 34
setup: C=1
at 1234: 00
# C=1 in, bit0=0. 0x00>>1 | 1<<7 = 0x80. C=0, N=1, V=1^0=1
expect: N=1 Z=0 V=1 C=0
expect_at 1234: 80
cycles: 6
""")


def gen_asr_mem():
    write_test("asr.test", """\
# ASR - Arithmetic Shift Right Memory
# Indexed: 0x67, Extended: 0x77

---
name: indexed positive
memory: 67 10
setup: x=1000
at 1010: 04
expect: N=0 Z=0 V=0 C=0
expect_at 1010: 02
cycles: 6

---
name: extended negative preserves sign
memory: 77 12 34
at 1234: 80
# 0x80 >> 1 | 0x80 = 0xC0. C=0, N=1, V=1^0=1
expect: N=1 Z=0 V=1 C=0
expect_at 1234: C0
cycles: 6
""")


def gen_asl_mem():
    write_test("asl.test", """\
# ASL - Arithmetic Shift Left Memory
# Indexed: 0x68, Extended: 0x78

---
name: indexed
memory: 68 10
setup: x=1000
at 1010: 01
expect: N=0 Z=0 V=0 C=0
expect_at 1010: 02
cycles: 6

---
name: extended into carry
memory: 78 12 34
at 1234: 80
# 0x80 << 1 = 0x00. C=1, N=0, V=0^1=1
expect: N=0 Z=1 V=1 C=1
expect_at 1234: 00
cycles: 6
""")


def gen_rol_mem():
    write_test("rol.test", """\
# ROL - Rotate Left Memory through Carry
# Indexed: 0x69, Extended: 0x79

---
name: indexed carry clear
memory: 69 10
setup: x=1000
at 1010: 01
# 0x01 << 1 | 0 = 0x02. C=0, N=0, V=0
expect: N=0 Z=0 V=0 C=0
expect_at 1010: 02
cycles: 6

---
name: extended carry in
memory: 79 12 34
setup: C=1
at 1234: 80
# 0x80 << 1 | 1 = 0x01. C=1, N=0, V=0^1=1
expect: N=0 Z=0 V=1 C=1
expect_at 1234: 01
cycles: 6
""")


def gen_dec_mem():
    write_test("dec.test", """\
# DEC - Decrement Memory
# Indexed: 0x6A, Extended: 0x7A

---
name: indexed
memory: 6A 10
setup: x=1000
at 1010: 05
expect: N=0 Z=0 V=0
expect_at 1010: 04
cycles: 6

---
name: extended to zero
memory: 7A 12 34
at 1234: 01
expect: N=0 Z=1 V=0
expect_at 1234: 00
cycles: 6

---
name: decrement 0x80 sets overflow
memory: 7A 12 34
at 1234: 80
expect: N=0 Z=0 V=1
expect_at 1234: 7F
cycles: 6
""")


def gen_inc_mem():
    write_test("inc.test", """\
# INC - Increment Memory
# Indexed: 0x6C, Extended: 0x7C

---
name: indexed
memory: 6C 10
setup: x=1000
at 1010: 05
expect: N=0 Z=0 V=0
expect_at 1010: 06
cycles: 6

---
name: extended overflow
memory: 7C 12 34
at 1234: 7F
expect: N=1 Z=0 V=1
expect_at 1234: 80
cycles: 6
""")


def gen_tst_mem():
    write_test("tst.test", """\
# TST - Test Memory (compare with 0)
# Indexed: 0x6D, Extended: 0x7D

---
name: indexed positive
memory: 6D 10
setup: x=1000
at 1010: 42
expect: N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended zero
memory: 7D 12 34
at 1234: 00
expect: N=0 Z=1 V=0 C=0
cycles: 4

---
name: extended negative
memory: 7D 12 34
at 1234: 80
expect: N=1 Z=0 V=0 C=0
cycles: 4
""")


def gen_clr_mem():
    write_test("clr.test", """\
# CLR - Clear Memory
# Indexed: 0x6F, Extended: 0x7F

---
name: indexed
memory: 6F 10
setup: x=1000
at 1010: 42
expect: N=0 Z=1 V=0 C=0
expect_at 1010: 00
cycles: 5

---
name: extended
memory: 7F 12 34
at 1234: FF
expect: N=0 Z=1 V=0 C=0
expect_at 1234: 00
cycles: 5
""")


# =============================================================================
# HD6301 BIT MANIPULATION INSTRUCTIONS
# =============================================================================
# AIM: AND Immediate with Memory. Indexed: 0x61, Direct: 0x71
# OIM: OR Immediate with Memory. Indexed: 0x62, Direct: 0x72
# EIM: XOR Immediate with Memory. Indexed: 0x65, Direct: 0x75
# TIM: Test Immediate with Memory (no write). Indexed: 0x6B, Direct: 0x7B
#
# Encoding: [opcode, mask, address/offset]
# For indexed (0x6x): address = X + third_byte
# For direct (0x7x): address = third_byte

def gen_aim():
    write_test("aim.test", """\
# AIM - AND Immediate with Memory (HD6301)
# Indexed: 0x61 [opcode, mask, offset], Direct: 0x71 [opcode, mask, addr]

---
name: indexed
memory: 61 0F 10
setup: x=1000
at 1010: AB
# 0xAB & 0x0F = 0x0B
expect: N=0 Z=0 V=0
expect_at 1010: 0B
cycles: 7

---
name: direct
memory: 71 F0 42
at 0042: AB
# 0xAB & 0xF0 = 0xA0
expect: N=1 Z=0 V=0
expect_at 0042: A0
cycles: 6

---
name: direct zero result
memory: 71 0F 42
at 0042: F0
# 0xF0 & 0x0F = 0x00
expect: N=0 Z=1 V=0
expect_at 0042: 00
cycles: 6
""")


def gen_oim():
    write_test("oim.test", """\
# OIM - OR Immediate with Memory (HD6301)
# Indexed: 0x62 [opcode, mask, offset], Direct: 0x72 [opcode, mask, addr]

---
name: indexed
memory: 62 0F 10
setup: x=1000
at 1010: A0
# 0xA0 | 0x0F = 0xAF
expect: N=1 Z=0 V=0
expect_at 1010: AF
cycles: 7

---
name: direct
memory: 72 80 42
at 0042: 00
# 0x00 | 0x80 = 0x80
expect: N=1 Z=0 V=0
expect_at 0042: 80
cycles: 6
""")


def gen_eim():
    write_test("eim.test", """\
# EIM - XOR Immediate with Memory (HD6301)
# Indexed: 0x65 [opcode, mask, offset], Direct: 0x75 [opcode, mask, addr]

---
name: indexed
memory: 65 FF 10
setup: x=1000
at 1010: AA
# 0xAA ^ 0xFF = 0x55
expect: N=0 Z=0 V=0
expect_at 1010: 55
cycles: 7

---
name: direct
memory: 75 AA 42
at 0042: AA
# 0xAA ^ 0xAA = 0x00
expect: N=0 Z=1 V=0
expect_at 0042: 00
cycles: 6
""")


def gen_tim():
    write_test("tim.test", """\
# TIM - Test Immediate with Memory (HD6301)
# Like AIM but does NOT write back. Indexed: 0x6B, Direct: 0x7B

---
name: indexed nonzero
memory: 6B 0F 10
setup: x=1000
at 1010: AB
# 0xAB & 0x0F = 0x0B (test only, memory unchanged)
expect: N=0 Z=0 V=0
expect_at 1010: AB
cycles: 7

---
name: direct zero result
memory: 7B 0F 42
at 0042: F0
# 0xF0 & 0x0F = 0x00 (test only, memory unchanged)
expect: N=0 Z=1 V=0
expect_at 0042: F0
cycles: 6
""")


# =============================================================================
# A-REGISTER MULTI-MODE OPERATIONS
# =============================================================================

def gen_suba():
    write_test("suba.test", """\
# SUBA - Subtract from A
# Immediate: 0x80, Direct: 0x90, Indexed: 0xA0, Extended: 0xB0

---
name: immediate
memory: 80 05
setup: a=10
expect: a=0B N=0 Z=0 V=0 C=0
cycles: 2

---
name: immediate borrow
memory: 80 10
setup: a=05
# 0x05 - 0x10 = 0xF5 (unsigned), borrow
expect: a=F5 N=1 Z=0 V=0 C=1
cycles: 2

---
name: direct
memory: 90 42
setup: a=10
at 0042: 05
expect: a=0B N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: A0 10
setup: a=10 x=1000
at 1010: 05
expect: a=0B N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended
memory: B0 12 34
setup: a=10
at 1234: 05
expect: a=0B N=0 Z=0 V=0 C=0
cycles: 4
""")


def gen_cmpa():
    write_test("cmpa.test", """\
# CMPA - Compare A (subtract without storing)
# Immediate: 0x81, Direct: 0x91, Indexed: 0xA1, Extended: 0xB1

---
name: immediate equal
memory: 81 42
setup: a=42
# A unchanged
expect: a=42 N=0 Z=1 V=0 C=0
cycles: 2

---
name: immediate greater
memory: 81 05
setup: a=10
expect: a=10 N=0 Z=0 V=0 C=0
cycles: 2

---
name: immediate less
memory: 81 10
setup: a=05
expect: a=05 N=1 Z=0 V=0 C=1
cycles: 2

---
name: direct
memory: 91 42
setup: a=42
at 0042: 42
expect: a=42 N=0 Z=1 V=0 C=0
cycles: 3

---
name: indexed
memory: A1 10
setup: a=42 x=1000
at 1010: 42
expect: a=42 N=0 Z=1 V=0 C=0
cycles: 4

---
name: extended
memory: B1 12 34
setup: a=42
at 1234: 42
expect: a=42 N=0 Z=1 V=0 C=0
cycles: 4
""")


def gen_sbca():
    write_test("sbca.test", """\
# SBCA - Subtract with Carry from A
# Immediate: 0x82, Direct: 0x92, Indexed: 0xA2, Extended: 0xB2

---
name: immediate with borrow
memory: 82 01
setup: a=11 C=1
expect: a=0F N=0 Z=0 V=0 C=0
cycles: 2

---
name: immediate no borrow
memory: 82 01
setup: a=11
expect: a=10 N=0 Z=0 V=0 C=0
cycles: 2

---
name: direct
memory: 92 42
setup: a=11 C=1
at 0042: 01
expect: a=0F N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: A2 10
setup: a=11 C=1 x=1000
at 1010: 01
expect: a=0F N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended
memory: B2 12 34
setup: a=11 C=1
at 1234: 01
expect: a=0F N=0 Z=0 V=0 C=0
cycles: 4
""")


def gen_adca():
    write_test("adca.test", """\
# ADCA - Add with Carry to A
# Immediate: 0x89, Direct: 0x99, Indexed: 0xA9, Extended: 0xB9

---
name: immediate with carry
memory: 89 01
setup: a=0E C=1
expect: a=10 H=1 N=0 Z=0 V=0 C=0
cycles: 2

---
name: immediate no carry
memory: 89 01
setup: a=0E
expect: a=0F H=0 N=0 Z=0 V=0 C=0
cycles: 2

---
name: direct
memory: 99 42
setup: a=0E C=1
at 0042: 01
expect: a=10 H=1 N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: A9 10
setup: a=0E C=1 x=1000
at 1010: 01
expect: a=10 H=1 N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended
memory: B9 12 34
setup: a=0E C=1
at 1234: 01
expect: a=10 H=1 N=0 Z=0 V=0 C=0
cycles: 4
""")


def gen_ldaa():
    write_test("ldaa.test", """\
# LDAA - Load A
# Immediate: 0x86, Direct: 0x96, Indexed: 0xA6, Extended: 0xB6

---
name: immediate positive
memory: 86 42
expect: a=42 N=0 Z=0 V=0
cycles: 2

---
name: immediate zero
memory: 86 00
expect: a=00 N=0 Z=1 V=0
cycles: 2

---
name: immediate negative
memory: 86 80
expect: a=80 N=1 Z=0 V=0
cycles: 2

---
name: direct
memory: 96 42
at 0042: AB
expect: a=AB N=1 Z=0 V=0
cycles: 3

---
name: indexed
memory: A6 10
setup: x=1000
at 1010: 42
expect: a=42 N=0 Z=0 V=0
cycles: 4

---
name: extended
memory: B6 12 34
at 1234: 42
expect: a=42 N=0 Z=0 V=0
cycles: 4
""")


def gen_staa():
    write_test("staa.test", """\
# STAA - Store A
# Direct: 0x97, Indexed: 0xA7, Extended: 0xB7

---
name: direct
memory: 97 42
setup: a=AB
expect: N=1 Z=0 V=0
expect_at 0042: AB
cycles: 3

---
name: indexed
memory: A7 10
setup: a=42 x=1000
expect: N=0 Z=0 V=0
expect_at 1010: 42
cycles: 4

---
name: extended
memory: B7 12 34
setup: a=42
expect: N=0 Z=0 V=0
expect_at 1234: 42
cycles: 4

---
name: store zero
memory: 97 42
expect: N=0 Z=1 V=0
expect_at 0042: 00
cycles: 3
""")


# =============================================================================
# B-REGISTER MULTI-MODE OPERATIONS
# =============================================================================

def gen_subb():
    write_test("subb.test", """\
# SUBB - Subtract from B
# Immediate: 0xC0, Direct: 0xD0, Indexed: 0xE0, Extended: 0xF0

---
name: immediate
memory: C0 05
setup: b=10
expect: b=0B N=0 Z=0 V=0 C=0
cycles: 2

---
name: immediate borrow
memory: C0 10
setup: b=05
expect: b=F5 N=1 Z=0 V=0 C=1
cycles: 2

---
name: direct
memory: D0 42
setup: b=10
at 0042: 05
expect: b=0B N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: E0 10
setup: b=10 x=1000
at 1010: 05
expect: b=0B N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended
memory: F0 12 34
setup: b=10
at 1234: 05
expect: b=0B N=0 Z=0 V=0 C=0
cycles: 4
""")


def gen_cmpb():
    write_test("cmpb.test", """\
# CMPB - Compare B
# Immediate: 0xC1, Direct: 0xD1, Indexed: 0xE1, Extended: 0xF1

---
name: immediate equal
memory: C1 42
setup: b=42
expect: b=42 N=0 Z=1 V=0 C=0
cycles: 2

---
name: immediate less
memory: C1 10
setup: b=05
expect: b=05 N=1 Z=0 V=0 C=1
cycles: 2

---
name: direct
memory: D1 42
setup: b=42
at 0042: 42
expect: b=42 Z=1
cycles: 3

---
name: indexed
memory: E1 10
setup: b=42 x=1000
at 1010: 42
expect: b=42 Z=1
cycles: 4

---
name: extended
memory: F1 12 34
setup: b=42
at 1234: 42
expect: b=42 Z=1
cycles: 4
""")


def gen_ldab():
    write_test("ldab.test", """\
# LDAB - Load B
# Immediate: 0xC6, Direct: 0xD6, Indexed: 0xE6, Extended: 0xF6

---
name: immediate
memory: C6 42
expect: b=42 N=0 Z=0 V=0
cycles: 2

---
name: immediate zero
memory: C6 00
expect: b=00 Z=1
cycles: 2

---
name: direct
memory: D6 42
at 0042: AB
expect: b=AB N=1 Z=0 V=0
cycles: 3

---
name: indexed
memory: E6 10
setup: x=1000
at 1010: 42
expect: b=42 N=0 Z=0 V=0
cycles: 4

---
name: extended
memory: F6 12 34
at 1234: 42
expect: b=42 N=0 Z=0 V=0
cycles: 4
""")


def gen_stab():
    write_test("stab.test", """\
# STAB - Store B
# Direct: 0xD7, Indexed: 0xE7, Extended: 0xF7

---
name: direct
memory: D7 42
setup: b=AB
expect: N=1 Z=0 V=0
expect_at 0042: AB
cycles: 3

---
name: indexed
memory: E7 10
setup: b=42 x=1000
expect: N=0 Z=0 V=0
expect_at 1010: 42
cycles: 4

---
name: extended
memory: F7 12 34
setup: b=42
expect: N=0 Z=0 V=0
expect_at 1234: 42
cycles: 4
""")


def gen_addb():
    write_test("addb.test", """\
# ADDB - Add to B
# Immediate: 0xCB, Direct: 0xDB, Indexed: 0xEB, Extended: 0xFB

---
name: immediate
memory: CB 01
setup: b=0F
expect: b=10 H=1 N=0 Z=0 V=0 C=0
cycles: 2

---
name: immediate overflow
memory: CB 01
setup: b=7F
expect: b=80 N=1 V=1 C=0 H=1
cycles: 2

---
name: direct
memory: DB 42
setup: b=0F
at 0042: 01
expect: b=10 H=1 N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: EB 10
setup: b=0F x=1000
at 1010: 01
expect: b=10 H=1
cycles: 4

---
name: extended
memory: FB 12 34
setup: b=0F
at 1234: 01
expect: b=10 H=1
cycles: 4
""")


# =============================================================================
# 16-BIT OPERATIONS
# =============================================================================

def gen_ldx():
    write_test("ldx.test", """\
# LDX - Load Index Register X
# Immediate: 0xCE, Direct: 0xDE, Indexed: 0xEE, Extended: 0xFE

---
name: immediate
memory: CE 12 34
expect: x=1234 N=0 Z=0 V=0
cycles: 3

---
name: immediate zero
memory: CE 00 00
expect: x=0000 Z=1
cycles: 3

---
name: direct
memory: DE 42
at 0042: 12 34
expect: x=1234 N=0 Z=0 V=0
cycles: 4

---
name: indexed
memory: EE 10
setup: x=1000
at 1010: AB CD
expect: x=ABCD N=1 Z=0 V=0
cycles: 5

---
name: extended
memory: FE 12 34
at 1234: 56 78
expect: x=5678 N=0 Z=0 V=0
cycles: 5
""")


def gen_stx():
    write_test("stx.test", """\
# STX - Store Index Register X
# Direct: 0xDF, Indexed: 0xEF, Extended: 0xFF

---
name: direct
memory: DF 42
setup: x=1234
expect: N=0 Z=0 V=0
expect_at 0042: 12
expect_at 0043: 34
cycles: 4

---
name: indexed
memory: EF 10
setup: x=1000
# X=0x1000, store at 0x1000+0x10=0x1010
expect: N=0 Z=0 V=0
expect_at 1010: 10
expect_at 1011: 00
cycles: 5

---
name: extended
memory: FF 12 34
setup: x=5678
expect: N=0 Z=0 V=0
expect_at 1234: 56
expect_at 1235: 78
cycles: 5
""")


def gen_lds():
    write_test("lds.test", """\
# LDS - Load Stack Pointer
# Immediate: 0x8E, Direct: 0x9E, Indexed: 0xAE, Extended: 0xBE

---
name: immediate
memory: 8E 12 34
expect: sp=1234 N=0 Z=0 V=0
cycles: 3

---
name: immediate zero
memory: 8E 00 00
expect: sp=0000 Z=1
cycles: 3

---
name: direct
memory: 9E 42
at 0042: 12 34
expect: sp=1234 N=0 Z=0 V=0
cycles: 4

---
name: indexed
memory: AE 10
setup: x=1000
at 1010: AB CD
expect: sp=ABCD N=1 Z=0 V=0
cycles: 5

---
name: extended
memory: BE 12 34
at 1234: 56 78
expect: sp=5678 N=0 Z=0 V=0
cycles: 5
""")


def gen_sts():
    write_test("sts.test", """\
# STS - Store Stack Pointer
# Direct: 0x9F, Indexed: 0xAF, Extended: 0xBF

---
name: direct
memory: 9F 42
expect: N=1 Z=0 V=0
expect_at 0042: F0
expect_at 0043: 00
cycles: 4

---
name: indexed
memory: AF 10
setup: x=1000 sp=1234
expect: N=0 Z=0 V=0
expect_at 1010: 12
expect_at 1011: 34
cycles: 5

---
name: extended
memory: BF 12 34
setup: sp=5678
expect: N=0 Z=0 V=0
expect_at 1234: 56
expect_at 1235: 78
cycles: 5
""")


def gen_cpx():
    write_test("cpx.test", """\
# CPX - Compare X (16-bit)
# Immediate: 0x8C, Direct: 0x9C, Indexed: 0xAC, Extended: 0xBC

---
name: immediate equal
memory: 8C 12 34
setup: x=1234
expect: x=1234 N=0 Z=1 V=0 C=0
cycles: 3

---
name: immediate greater
memory: 8C 10 00
setup: x=2000
expect: x=2000 N=0 Z=0 V=0 C=0
cycles: 3

---
name: immediate less
memory: 8C 20 00
setup: x=1000
# 0x1000 - 0x2000 = 0xF000, N=1, C=1
expect: x=1000 N=1 Z=0 V=0 C=1
cycles: 3

---
name: direct
memory: 9C 42
setup: x=1234
at 0042: 12 34
expect: x=1234 Z=1
cycles: 4

---
name: indexed
memory: AC 10
setup: x=1000
at 1010: 10 00
expect: x=1000 Z=1
cycles: 5

---
name: extended
memory: BC 12 34
setup: x=5678
at 1234: 56 78
expect: x=5678 Z=1
cycles: 5
""")


# =============================================================================
# CONTROL FLOW
# =============================================================================

def gen_jmp():
    write_test("jmp.test", """\
# JMP - Jump
# Indexed: 0x6E, Extended: 0x7E

---
name: indexed
memory: 6E 10
setup: x=1000
# PC = X + 0x10 = 0x1010
expect: pc=1010
cycles: 3

---
name: extended
memory: 7E 12 34
expect: pc=1234
cycles: 3
""")


def gen_jsr():
    write_test("jsr.test", """\
# JSR - Jump to Subroutine
# Direct: 0x9D, Indexed: 0xAD, Extended: 0xBD
# Pushes return address, then jumps.

---
name: direct
memory: 9D 42
# PC after fetch = 0x0022, pushed to stack, then PC = 0x0042
expect: pc=0042 sp=EFFE
expect_at EFFF: 00
expect_at F000: 22
cycles: 5

---
name: indexed
memory: AD 10
setup: x=1000
# PC after fetch = 0x0022, pushed to stack, then PC = 0x1010
expect: pc=1010 sp=EFFE
expect_at EFFF: 00
expect_at F000: 22
cycles: 5

---
name: extended
memory: BD 12 34
# PC after fetch = 0x0023, pushed to stack, then PC = 0x1234
expect: pc=1234 sp=EFFE
expect_at EFFF: 00
expect_at F000: 23
cycles: 6
""")


def gen_bsr():
    write_test("bsr.test", """\
# BSR - Branch to Subroutine (opcode 0x8D)
# Pushes return address (PC after this instruction), then branches by signed offset.

---
name: forward
memory: 8D 10
# PC after fetch = 0x0022. Push 0x0022 to stack. PC = 0x0022 + 0x10 = 0x0032
expect: pc=0032 sp=EFFE
expect_at EFFF: 00
expect_at F000: 22
cycles: 5

---
name: backward
memory: 8D F0
# PC after fetch = 0x0022. Push 0x0022. PC = 0x0022 + (-16) = 0x0012
expect: pc=0012 sp=EFFE
expect_at EFFF: 00
expect_at F000: 22
cycles: 5
""")


def gen_rti():
    write_test("rti.test", """\
# RTI - Return from Interrupt (opcode 0x3B)
# Pulls: SR, B, A, X(16), PC(16) from stack.

---
name: restores all registers
memory: 3B
setup: sp=EFF9
at EFFA: C4
at EFFB: BB
at EFFC: AA
at EFFD: 12
at EFFE: 34
at EFFF: 56
at F000: 78
# SR=0xC4 (Z=1), B=0xBB, A=0xAA, X=0x1234, PC=0x5678
expect: a=AA b=BB x=1234 sp=F000 pc=5678 sr=C4
cycles: 10
""")


def gen_pshx():
    write_test("pshx.test", """\
# PSHX - Push X to Stack (opcode 0x3C)

---
name: pushes X to stack
memory: 3C
setup: x=1234
expect: sp=EFFE
expect_at EFFF: 12
expect_at F000: 34
cycles: 5
""")


# =============================================================================
# EXTEND EXISTING FILES WITH MORE ADDRESSING MODES
# =============================================================================
# These files already exist but only cover immediate mode.
# We append DIR, IDX, EXT modes via new test cases.

def extend_file(filename, extra_content):
    """Append additional test cases to an existing file."""
    path = os.path.join(TESTDATA_DIR, filename)
    if not os.path.exists(path):
        print(f"WARNING: {filename} not found, can't extend")
        return
    with open(path, "r") as f:
        existing = f.read()
    # Check if any of the new test names already exist (crude check)
    if "name: direct" in existing:
        print(f"SKIP extend (already has modes): {filename}")
        return
    with open(path, "a") as f:
        f.write("\n" + extra_content)
    print(f"Extended: {filename}")


def extend_adda():
    extend_file("adda.test", """\
---
name: direct
memory: 9B 42
setup: a=10
at 0042: 05
expect: a=15 N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: AB 10
setup: a=10 x=1000
at 1010: 05
expect: a=15 N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended
memory: BB 12 34
setup: a=10
at 1234: 05
expect: a=15 N=0 Z=0 V=0 C=0
cycles: 4
""")


def extend_adcb():
    extend_file("adcb.test", """\
---
name: direct with carry
memory: D9 42
setup: b=0E C=1
at 0042: 01
expect: b=10 H=1
cycles: 3

---
name: indexed
memory: E9 10
setup: b=0E C=1 x=1000
at 1010: 01
expect: b=10 H=1
cycles: 4

---
name: extended
memory: F9 12 34
setup: b=0E C=1
at 1234: 01
expect: b=10 H=1
cycles: 4
""")


def extend_anda():
    extend_file("anda.test", """\
---
name: direct
memory: 94 42
setup: a=FF
at 0042: 0F
expect: a=0F N=0 Z=0 V=0
cycles: 3

---
name: indexed
memory: A4 10
setup: a=FF x=1000
at 1010: 0F
expect: a=0F N=0 Z=0 V=0
cycles: 4

---
name: extended
memory: B4 12 34
setup: a=FF
at 1234: 0F
expect: a=0F N=0 Z=0 V=0
cycles: 4
""")


def extend_andb():
    extend_file("andb.test", """\
---
name: direct
memory: D4 42
setup: b=FF
at 0042: 0F
expect: b=0F N=0 Z=0 V=0
cycles: 3

---
name: indexed
memory: E4 10
setup: b=FF x=1000
at 1010: 0F
expect: b=0F N=0 Z=0 V=0
cycles: 4

---
name: extended
memory: F4 12 34
setup: b=FF
at 1234: 0F
expect: b=0F N=0 Z=0 V=0
cycles: 4
""")


def extend_bita():
    extend_file("bita.test", """\
---
name: direct nonzero
memory: 95 42
setup: a=FF
at 0042: 0F
expect: a=FF N=0 Z=0 V=0
cycles: 3

---
name: indexed
memory: A5 10
setup: a=FF x=1000
at 1010: 80
expect: a=FF N=1 Z=0 V=0
cycles: 4

---
name: extended
memory: B5 12 34
setup: a=FF
at 1234: 42
expect: a=FF N=0 Z=0 V=0
cycles: 4
""")


def extend_bitb():
    extend_file("bitb.test", """\
---
name: direct
memory: D5 42
setup: b=FF
at 0042: 0F
expect: b=FF N=0 Z=0 V=0
cycles: 3

---
name: indexed
memory: E5 10
setup: b=FF x=1000
at 1010: 80
expect: b=FF N=1 Z=0 V=0
cycles: 4

---
name: extended
memory: F5 12 34
setup: b=FF
at 1234: 42
expect: b=FF N=0 Z=0 V=0
cycles: 4
""")


def extend_eora():
    extend_file("eora.test", """\
---
name: direct
memory: 98 42
setup: a=FF
at 0042: AA
expect: a=55 N=0 Z=0 V=0
cycles: 3

---
name: indexed
memory: A8 10
setup: a=FF x=1000
at 1010: AA
expect: a=55 N=0 Z=0 V=0
cycles: 4

---
name: extended
memory: B8 12 34
setup: a=FF
at 1234: AA
expect: a=55 N=0 Z=0 V=0
cycles: 4
""")


def extend_eorb():
    extend_file("eorb.test", """\
---
name: direct
memory: D8 42
setup: b=FF
at 0042: AA
expect: b=55 N=0 Z=0 V=0
cycles: 3

---
name: indexed
memory: E8 10
setup: b=FF x=1000
at 1010: AA
expect: b=55 N=0 Z=0 V=0
cycles: 4

---
name: extended
memory: F8 12 34
setup: b=FF
at 1234: AA
expect: b=55 N=0 Z=0 V=0
cycles: 4
""")


def extend_oraa():
    extend_file("oraa.test", """\
---
name: direct
memory: 9A 42
setup: a=A0
at 0042: 0F
expect: a=AF N=1 Z=0 V=0
cycles: 3

---
name: indexed
memory: AA 10
setup: a=A0 x=1000
at 1010: 0F
expect: a=AF N=1 Z=0 V=0
cycles: 4

---
name: extended
memory: BA 12 34
setup: a=A0
at 1234: 0F
expect: a=AF N=1 Z=0 V=0
cycles: 4
""")


def extend_orab():
    extend_file("orab.test", """\
---
name: direct
memory: DA 42
setup: b=A0
at 0042: 0F
expect: b=AF N=1 Z=0 V=0
cycles: 3

---
name: indexed
memory: EA 10
setup: b=A0 x=1000
at 1010: 0F
expect: b=AF N=1 Z=0 V=0
cycles: 4

---
name: extended
memory: FA 12 34
setup: b=A0
at 1234: 0F
expect: b=AF N=1 Z=0 V=0
cycles: 4
""")


def extend_sbcb():
    extend_file("sbcb.test", """\
---
name: direct with borrow
memory: D2 42
setup: b=11 C=1
at 0042: 01
expect: b=0F N=0 Z=0 V=0 C=0
cycles: 3

---
name: indexed
memory: E2 10
setup: b=11 C=1 x=1000
at 1010: 01
expect: b=0F N=0 Z=0 V=0 C=0
cycles: 4

---
name: extended
memory: F2 12 34
setup: b=11 C=1
at 1234: 01
expect: b=0F N=0 Z=0 V=0 C=0
cycles: 4
""")


def extend_addd():
    extend_file("addd.test", """\
---
name: direct
memory: D3 42
setup: a=10 b=00
at 0042: 05 00
expect: a=15 b=00 N=0 Z=0 V=0 C=0
cycles: 4

---
name: indexed
memory: E3 10
setup: a=10 b=00 x=1000
at 1010: 05 00
expect: a=15 b=00 N=0 Z=0 V=0 C=0
cycles: 5

---
name: extended
memory: F3 12 34
setup: a=10 b=00
at 1234: 05 00
expect: a=15 b=00 N=0 Z=0 V=0 C=0
cycles: 5
""")


def extend_ldd():
    extend_file("ldd.test", """\
---
name: direct
memory: DC 42
at 0042: 12 34
expect: a=12 b=34 N=0 Z=0 V=0
cycles: 4

---
name: indexed
memory: EC 10
setup: x=1000
at 1010: AB CD
expect: a=AB b=CD N=1 Z=0 V=0
cycles: 5

---
name: extended
memory: FC 12 34
at 1234: 56 78
expect: a=56 b=78 N=0 Z=0 V=0
cycles: 5
""")


def extend_std():
    extend_file("std.test", """\
---
name: indexed
memory: ED 10
setup: a=12 b=34 x=1000
expect: N=0 Z=0 V=0
expect_at 1010: 12
expect_at 1011: 34
cycles: 5

---
name: extended
memory: FD 12 34
setup: a=56 b=78
expect: N=0 Z=0 V=0
expect_at 1234: 56
expect_at 1235: 78
cycles: 5
""")


# =============================================================================
# MAIN
# =============================================================================

def main():
    os.makedirs(TESTDATA_DIR, exist_ok=True)

    print("=== Generating new test files ===")

    # Branches
    gen_branches()

    # Accumulator A single-operand
    gen_coma()
    gen_lsra()
    gen_rora()
    gen_asra()
    gen_asla()
    gen_rola()
    gen_deca()
    gen_inca()
    gen_tsta()
    gen_clra()

    # Accumulator B single-operand
    gen_comb()
    gen_lsrb()
    gen_rorb()
    gen_asrb()
    gen_aslb()
    gen_rolb()
    gen_decb()
    gen_incb()
    gen_tstb()
    gen_clrb()

    # Memory single-operand (IDX + EXT)
    gen_neg_mem()
    gen_com_mem()
    gen_lsr_mem()
    gen_ror_mem()
    gen_asr_mem()
    gen_asl_mem()
    gen_rol_mem()
    gen_dec_mem()
    gen_inc_mem()
    gen_tst_mem()
    gen_clr_mem()

    # HD6301 bit manipulation
    gen_aim()
    gen_oim()
    gen_eim()
    gen_tim()

    # A-register multi-mode
    gen_suba()
    gen_cmpa()
    gen_sbca()
    gen_adca()
    gen_ldaa()
    gen_staa()

    # B-register multi-mode
    gen_subb()
    gen_cmpb()
    gen_ldab()
    gen_stab()
    gen_addb()

    # 16-bit operations
    gen_ldx()
    gen_stx()
    gen_lds()
    gen_sts()
    gen_cpx()

    # Control flow
    gen_jmp()
    gen_jsr()
    gen_bsr()
    gen_rti()
    gen_pshx()

    print("\n=== Extending existing test files ===")

    extend_adda()
    extend_adcb()
    extend_anda()
    extend_andb()
    extend_bita()
    extend_bitb()
    extend_eora()
    extend_eorb()
    extend_oraa()
    extend_orab()
    extend_sbcb()
    extend_addd()
    extend_ldd()
    extend_std()

    print("\nDone!")


if __name__ == "__main__":
    main()
