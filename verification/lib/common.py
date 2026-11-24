"""Shared pieces of the Stage E3 test generators.

Everything here is written from the RISC-V unprivileged spec, not from the
simulator: an RV64IM oracle, an instruction encoder by field layout, and a
builder for padded programs whose operands come from a .data table.
"""
M64 = (1 << 64) - 1
DATA = 0x10000000
SCRATCH = DATA + 0x400
GAP = ["nop"] * 4


def u64(v): return v & M64
def s64(v): v &= M64; return v - (1 << 64) if v >> 63 else v
def s32(v): v &= 0xFFFFFFFF; return v - (1 << 32) if v >> 31 else v
def sx(v, bits): v &= (1 << bits) - 1; return v - (1 << bits) if v >> (bits - 1) else v


# ---- RV64IM oracle -----------------------------------------------------------
def _div(x, y):
    if y == 0:
        return -1
    q = abs(x) // abs(y)
    return q if (x < 0) == (y < 0) else -q


def _rem(x, y):
    return x if y == 0 else x - _div(x, y) * y


def alu(mn, a, b):
    """rd for an RV64IM R/I instruction with operands a (rs1) and b (rs2/imm)."""
    a, b = u64(a), u64(b)
    sa, sb = s64(a), s64(b)
    a32, b32, sa32, sb32 = a & 0xFFFFFFFF, b & 0xFFFFFFFF, s32(a), s32(b)
    table = {
        "add": lambda: u64(a + b), "addi": lambda: u64(a + b), "sub": lambda: u64(a - b),
        "sll": lambda: u64(a << (b & 63)), "slli": lambda: u64(a << (b & 63)),
        "srl": lambda: a >> (b & 63), "srli": lambda: a >> (b & 63),
        "sra": lambda: u64(sa >> (b & 63)), "srai": lambda: u64(sa >> (b & 63)),
        "addw": lambda: u64(s32(a + b)), "addiw": lambda: u64(s32(a + b)),
        "subw": lambda: u64(s32(a - b)),
        "sllw": lambda: u64(s32(a32 << (b & 31))), "slliw": lambda: u64(s32(a32 << (b & 31))),
        "srlw": lambda: u64(s32(a32 >> (b & 31))), "srliw": lambda: u64(s32(a32 >> (b & 31))),
        "sraw": lambda: u64(s32(sa32 >> (b & 31))), "sraiw": lambda: u64(s32(sa32 >> (b & 31))),
        "mulw": lambda: u64(s32(a * b)),
        "divw": lambda: u64(s32(a32 if (sa32 == -(1 << 31) and sb32 == -1) else _div(sa32, sb32))),
        "divuw": lambda: u64(s32(0xFFFFFFFF if b32 == 0 else a32 // b32)),
        "remw": lambda: u64(s32(0 if (sa32 == -(1 << 31) and sb32 == -1) else _rem(sa32, sb32))),
        "remuw": lambda: u64(s32(a32 if b32 == 0 else a32 % b32)),
        "negw": lambda: u64(s32(-a)), "sext.w": lambda: u64(s32(a)),
    }
    return table[mn]()


# ---- encoder by field layout -------------------------------------------------
OPCODE = {"OP": 0b0110011, "OP-IMM": 0b0010011, "OP-32": 0b0111011, "OP-IMM-32": 0b0011011,
          "LUI": 0b0110111, "JAL": 0b1101111, "JALR": 0b1100111}


def r_type(opcode, funct3, funct7, rd, rs1, rs2):
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode


def i_type(opcode, funct3, rd, rs1, imm):
    return ((imm & 0xFFF) << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode


# ---- programs -----------------------------------------------------------------
class Program:
    """Operands come from a .data dword table at DATA (base in x10) and every
    instruction is followed by four nops, so the no-hazard models are inside
    their contract."""

    def __init__(self):
        self.table, self.body = [], []

    def value(self, v):
        """Offset of v in the table (added once)."""
        v = u64(v)
        if v not in self.table:
            self.table.append(v)
        return 8 * self.table.index(v)

    def ins(self, *lines):
        for line in lines:
            self.body.append(line)

    def load(self, reg, v):
        self.ins(f"ld {reg}, {self.value(v)}(x10)")

    def text(self, padded=True):
        out = []
        if self.table:
            out += [".data", "vals: .dword " + ", ".join(hex(t) for t in self.table)]
        out.append(".text")
        for line in ["lui x10, 0x10000", "addi x11, x10, 1024"] + self.body:
            out.append("    " + line)
            if padded:
                out += ["    " + g for g in GAP]
        return "\n".join(out) + "\n"


def cpp_case(name, program_text, x=None, dwords=None, f=None):
    """One IsaCase initializer."""
    xs = ", ".join(f"{{{r}, 0x{u64(v):X}ULL}}" for r, v in sorted((x or {}).items()))
    ds = ", ".join(f"{{0x{a:X}ULL, 0x{u64(v):X}ULL}}" for a, v in sorted((dwords or {}).items()))
    fs = ", ".join(f"{{{r}, 0x{u64(v):X}ULL}}" for r, v in sorted((f or {}).items()))
    return f'    {{"{name}",\nR"ASM({program_text})ASM",\n     {{{xs}}},\n     {{{ds}}},\n     {{{fs}}}}},'
