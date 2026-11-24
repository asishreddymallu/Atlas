"""Tiny RV32IM interpreter for the cache-lab sources (independent of the Atlas assembler).

Only the mnemonics the generator emits: lui addi add sub slli mul lw sw blt bne beq.
Text starts at 0, 4 bytes per instruction, no pseudo-instructions. The program ends when the
pc falls off the end. Yields the fetch trace (pc) and data trace (addr, size, 'R'/'W').
"""
import re

M32 = (1 << 32) - 1


def s32(v):
    v &= M32
    return v - (1 << 32) if v & 0x80000000 else v


def parse(text):
    insns, labels = [], {}
    for raw in text.splitlines():
        line = raw.split("#")[0].strip()
        if not line:
            continue
        m = re.match(r"^(\w+):\s*(.*)$", line)
        if m:
            labels[m.group(1)] = len(insns)
            line = m.group(2).strip()
            if not line:
                continue
        op, _, rest = line.partition(" ")
        args = [a.strip() for a in rest.split(",")] if rest.strip() else []
        insns.append((op, args))
    return insns, labels


def reg(a):
    assert a.startswith("x"), a
    return int(a[1:])


def run(text, max_steps=10_000_000):
    insns, labels = parse(text)
    x = [0] * 32
    mem = {}
    fetch, data, events = [], [], []
    pc = 0
    steps = 0
    while pc // 4 < len(insns):
        steps += 1
        assert steps < max_steps
        fetch.append(pc)
        events.append(('F', pc, 4, 'R'))
        op, a = insns[pc // 4]
        npc = pc + 4
        if op == "lui":
            x[reg(a[0])] = s32(int(a[1], 0) << 12)
        elif op == "addi":
            x[reg(a[0])] = s32(x[reg(a[1])] + int(a[2], 0))
        elif op == "add":
            x[reg(a[0])] = s32(x[reg(a[1])] + x[reg(a[2])])
        elif op == "sub":
            x[reg(a[0])] = s32(x[reg(a[1])] - x[reg(a[2])])
        elif op == "slli":
            x[reg(a[0])] = s32(x[reg(a[1])] << int(a[2], 0))
        elif op == "mul":
            x[reg(a[0])] = s32(x[reg(a[1])] * x[reg(a[2])])
        elif op in ("lw", "sw"):
            m = re.match(r"(-?\d+)\((x\d+)\)", a[1])
            addr = (x[reg(m.group(2))] + int(m.group(1))) & M32
            assert addr % 4 == 0
            if op == "lw":
                data.append((addr, 4, "R"))
                events.append(('D', addr, 4, 'R'))
                x[reg(a[0])] = s32(mem.get(addr, 0))
            else:
                data.append((addr, 4, "W"))
                events.append(('D', addr, 4, 'W'))
                mem[addr] = x[reg(a[0])] & M32
        elif op in ("blt", "bne", "beq"):
            l, r = x[reg(a[0])], x[reg(a[1])]
            take = {"blt": l < r, "bne": l != r, "beq": l == r}[op]
            if take:
                npc = labels[a[2]] * 4
        else:
            raise ValueError(op)
        x[0] = 0
        pc = npc
    return dict(regs=x, mem=mem, fetch=fetch, data=data, events=events, steps=steps, ninsn=len(insns))
