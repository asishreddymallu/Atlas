import random, struct, math, sys
sys.path.insert(0, '.')
from fpo import *
d2b = lambda x: struct.unpack('<Q', struct.pack('<d', x))[0]
b2d = lambda b: struct.unpack('<d', struct.pack('<Q', b))[0]
random.seed(1); bad = 0
for _ in range(20000):
    a, b = (b2d(random.getrandbits(64)) for _ in range(2))
    if math.isnan(a) or math.isnan(b): continue
    for name, fn, py in (("add", lambda: add(F64, d2b(a), d2b(b), RNE), lambda: a + b),
                         ("mul", lambda: mul(F64, d2b(a), d2b(b), RNE), lambda: a * b),
                         ("div", lambda: div(F64, d2b(a), d2b(b), RNE), lambda: a / b if b else None)):
        r = py()
        if r is None or math.isnan(r): continue
        got = fn()[0]
        if got != d2b(r): bad += 1; print(name, a, b, hex(got), hex(d2b(r)))
for _ in range(5000):
    a = abs(b2d(random.getrandbits(64)))
    if math.isnan(a) or math.isinf(a): continue
    if sqrt(F64, d2b(a), RNE)[0] != d2b(math.sqrt(a)): bad += 1; print("sqrt", a)
print("bad", bad)
f = F32
one, three, zero = 0x3F800000, 0x40400000, 0
print(div(f, one, three, RNE), div(f, one, zero, RNE), div(f, zero, zero, RNE))
print(mul(f, 0x00800000, 0x3F000000, RNE))   # 2^-126 * 0.5 exact subnormal -> no UF
print(mul(f, 0x00800001, 0x3F000000, RNE))   # inexact tiny -> UF|NX
print(mul(f, 0x7F7FFFFF, 0x40000000, RTZ), mul(f, 0x7F7FFFFF, 0x40000000, RNE))
print(to_int(f, 0x3FC00000, "w", RNE), to_int(f, 0x4F000000, "w", RNE), to_int(f, 0xBF000000, "wu", RNE))
# tininess-after-rounding edge: largest subnormal-ish product rounding up to 2^-126
x = F32.from_fraction(Fraction(2)**-126 * (1 - Fraction(1, 2**25)), RNE); print("edge", hex(x[0]), x[1])
