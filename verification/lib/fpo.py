"""Stage E5: an IEEE 754 / RISC-V F+D oracle with exception flags.

Written from the RISC-V unprivileged spec (chapters F and D) and IEEE 754-2008,
not from the simulator. Every operation works on bit patterns and exact
Fraction arithmetic; nothing here uses the host FPU.

fflags layout (RISC-V): NX bit 0, UF 1, OF 2, DZ 3, NV 4.
Rounding modes: 0 RNE, 1 RTZ, 2 RDN, 3 RUP, 4 RMM (ties away from zero).
Tininess is detected AFTER rounding (RISC-V), and UF is raised only when the
result is also inexact. OF always comes with NX. A NaN result is the
canonical NaN. Any operation on a signaling NaN raises NV.
"""
from fractions import Fraction
from math import isqrt

NX, UF, OF, DZ, NV = 1, 2, 4, 8, 16
RNE, RTZ, RDN, RUP, RMM = 0, 1, 2, 3, 4
MODES = {"rne": RNE, "rtz": RTZ, "rdn": RDN, "rup": RUP, "rmm": RMM}


class Fmt:
    def __init__(self, w, ebits, mbits):
        self.w, self.E, self.M = w, ebits, mbits
        self.bias = (1 << (ebits - 1)) - 1
        self.emin, self.emax = 1 - self.bias, self.bias
        self.sbit = 1 << (w - 1)
        self.expmask = ((1 << ebits) - 1) << mbits
        self.fracmask = (1 << mbits) - 1
        self.inf = self.expmask
        self.canon = self.expmask | (1 << (mbits - 1))
        self.maxfin = (((1 << ebits) - 2) << mbits) | self.fracmask

    # ---- classification -------------------------------------------------
    def sign(self, x): return 1 if x & self.sbit else 0
    def isnan(self, x): return (x & self.expmask) == self.expmask and (x & self.fracmask) != 0
    def issnan(self, x): return self.isnan(x) and not (x >> (self.M - 1)) & 1
    def isinf(self, x): return (x & ~self.sbit) == self.inf
    def iszero(self, x): return (x & ~self.sbit) == 0

    def value(self, x):
        """Exact value of a finite pattern."""
        e = (x & self.expmask) >> self.M
        f = x & self.fracmask
        mag = Fraction(f, 1 << self.M) * Fraction(2) ** self.emin if e == 0 else \
            Fraction((1 << self.M) | f) * Fraction(2) ** (e - self.bias - self.M)
        return -mag if self.sign(x) else mag

    def zero(self, s): return self.sbit if s else 0
    def infinity(self, s): return (self.sbit if s else 0) | self.inf

    # ---- rounding -------------------------------------------------------
    def _round_at(self, s, cls_fn, e):
        """Round |value| to M+1 significant bits with exponent e.
        cls_fn(k) -> (fl, c): fl = floor(|v| / 2^k), c = -1/0/1/None for the
        remainder below/at/above one half, None when exact."""
        fl, c = cls_fn(e - self.M)
        return fl, c

    def _inc(self, s, fl, c, mode):
        if c is None:
            return fl
        if mode == RNE:
            up = c > 0 or (c == 0 and fl & 1)
        elif mode == RMM:
            up = c >= 0
        elif mode == RTZ:
            up = False
        elif mode == RDN:
            up = s == 1
        else:  # RUP
            up = s == 0
        return fl + 1 if up else fl

    def round(self, s, cls_fn, e_true, mode):
        """s: sign; cls_fn as above; e_true = floor(log2 |v|). Returns (bits, flags)."""
        flags = 0
        # unbounded-exponent rounding, for overflow and after-rounding tininess
        fl, c = cls_fn(e_true - self.M)
        m = self._inc(s, fl, c, mode)
        e_u = e_true
        if m >> (self.M + 1):
            m >>= 1
            e_u += 1
        inexact = c is not None
        if e_u > self.emax:
            flags |= OF | NX
            to_inf = mode in (RNE, RMM) or (mode == RUP and s == 0) or (mode == RDN and s == 1)
            return (self.infinity(s) if to_inf else (self.sbit if s else 0) | self.maxfin), flags
        tiny = e_u < self.emin
        # bounded rounding: subnormal quantum when below emin
        e = max(e_true, self.emin)
        fl, c = cls_fn(e - self.M)
        m = self._inc(s, fl, c, mode)
        if m >> (self.M + 1):
            m >>= 1
            e += 1
        inexact = c is not None
        if inexact:
            flags |= NX
            if tiny:
                flags |= UF
        if m == 0:
            return self.zero(s), flags
        if m >> self.M:  # normal
            bits = ((e + self.bias) << self.M) | (m & self.fracmask)
        else:  # subnormal
            bits = m
        return (self.sbit if s else 0) | bits, flags

    def from_fraction(self, q, mode, zero_sign=0):
        """Round an exact rational."""
        if q == 0:
            return self.zero(zero_sign), 0
        s = 1 if q < 0 else 0
        a = -q if s else q
        e = a.numerator.bit_length() - a.denominator.bit_length()
        if Fraction(2) ** e > a:
            e -= 1
        assert Fraction(2) ** e <= a < Fraction(2) ** (e + 1)

        def cls(k):
            n = a / Fraction(2) ** k
            fl = n.numerator // n.denominator
            r = n - fl
            if r == 0:
                return fl, None
            return fl, (r > Fraction(1, 2)) - (r < Fraction(1, 2))
        return self.round(s, cls, e, mode)


F32, F64 = Fmt(32, 8, 23), Fmt(64, 11, 52)
FMT = {"s": F32, "d": F64}


def _nan_in(f, *xs):
    """(any NaN, any sNaN)"""
    return any(f.isnan(x) for x in xs), any(f.issnan(x) for x in xs)


def _exact_zero_sign(mode, s1, s2):
    """Sign of an exact zero sum x + y where the operands' signs are s1, s2
    (IEEE 754 6.3): same signs keep it; otherwise +0, -0 under RDN."""
    if s1 == s2:
        return s1
    return 1 if mode == RDN else 0


def add(f, a, b, mode, neg_b=False):
    if neg_b and not f.isnan(b):
        b ^= f.sbit
    anynan, snan = _nan_in(f, a, b)
    if anynan:
        return f.canon, NV if snan else 0
    if f.isinf(a) and f.isinf(b):
        return (f.canon, NV) if f.sign(a) != f.sign(b) else (a, 0)
    if f.isinf(a):
        return a, 0
    if f.isinf(b):
        return b, 0
    q = f.value(a) + f.value(b)
    if q == 0:
        if f.iszero(a) and f.iszero(b):
            return f.zero(_exact_zero_sign(mode, f.sign(a), f.sign(b))), 0
        return f.zero(1 if mode == RDN else 0), 0
    return f.from_fraction(q, mode)


def mul(f, a, b, mode):
    anynan, snan = _nan_in(f, a, b)
    if anynan:
        return f.canon, NV if snan else 0
    s = f.sign(a) ^ f.sign(b)
    if (f.isinf(a) and f.iszero(b)) or (f.iszero(a) and f.isinf(b)):
        return f.canon, NV
    if f.isinf(a) or f.isinf(b):
        return f.infinity(s), 0
    q = f.value(a) * f.value(b)
    if q == 0:
        return f.zero(s), 0
    return f.from_fraction(q, mode)


def div(f, a, b, mode):
    anynan, snan = _nan_in(f, a, b)
    if anynan:
        return f.canon, NV if snan else 0
    s = f.sign(a) ^ f.sign(b)
    if (f.iszero(a) and f.iszero(b)) or (f.isinf(a) and f.isinf(b)):
        return f.canon, NV
    if f.isinf(a):
        return f.infinity(s), 0
    if f.isinf(b):
        return f.zero(s), 0
    if f.iszero(b):
        return f.infinity(s), DZ
    if f.iszero(a):
        return f.zero(s), 0
    return f.from_fraction(f.value(a) / f.value(b), mode)


def sqrt(f, a, mode):
    if f.isnan(a):
        return f.canon, NV if f.issnan(a) else 0
    if f.iszero(a):
        return a, 0
    if f.sign(a):
        return f.canon, NV
    if f.isinf(a):
        return a, 0
    v = f.value(a)
    ea = v.numerator.bit_length() - v.denominator.bit_length()
    if Fraction(2) ** ea > v:
        ea -= 1
    e = ea // 2

    def cls(k):
        n2 = v / Fraction(4) ** k          # (sqrt(v) / 2^k)^2
        fl = isqrt(n2.numerator // n2.denominator)
        if Fraction(fl * fl) == n2:
            return fl, None
        half = Fraction(2 * fl + 1, 2) ** 2
        return fl, (n2 > half) - (n2 < half)
    return f.round(0, cls, e, mode)


def fma(f, a, b, c, mode, neg_prod=False, neg_c=False):
    """fmadd: a*b+c; fmsub: a*b-c; fnmsub: -(a*b)+c; fnmadd: -(a*b)-c."""
    anynan, snan = _nan_in(f, a, b, c)
    zero_inf = (f.isinf(a) and f.iszero(b)) or (f.iszero(a) and f.isinf(b))
    if snan or zero_inf:
        return f.canon, NV          # RISC-V: 0*inf raises NV even when c is a qNaN
    if anynan:
        return f.canon, 0
    ps = f.sign(a) ^ f.sign(b) ^ (1 if neg_prod else 0)
    cs = f.sign(c) ^ (1 if neg_c else 0)
    pinf = f.isinf(a) or f.isinf(b)
    if pinf and f.isinf(c):
        return (f.canon, NV) if ps != cs else (f.infinity(ps), 0)
    if pinf:
        return f.infinity(ps), 0
    if f.isinf(c):
        return f.infinity(cs), 0
    pv = f.value(a) * f.value(b) * (-1 if neg_prod else 1)
    cv = f.value(c) * (-1 if neg_c else 1)
    q = pv + cv
    if q == 0:
        if pv == 0 and cv == 0:
            return f.zero(_exact_zero_sign(mode, ps, cs)), 0
        return f.zero(1 if mode == RDN else 0), 0
    return f.from_fraction(q, mode)


def minmax(f, a, b, is_max):
    anynan, snan = _nan_in(f, a, b)
    fl = NV if snan else 0
    if f.isnan(a) and f.isnan(b):
        return f.canon, fl
    if f.isnan(a):
        return b, fl
    if f.isnan(b):
        return a, fl
    def key(x):
        if f.isinf(x):
            return (Fraction(-1 if f.sign(x) else 1) * Fraction(2) ** 2000, 0)
        return (f.value(x), 0 if f.sign(x) else 1)   # -0 < +0
    ka, kb = key(a), key(b)
    if is_max:
        return (a if ka >= kb else b), fl
    return (a if ka <= kb else b), fl


def _ord(f, x):
    if f.isinf(x):
        return Fraction(-1 if f.sign(x) else 1) * Fraction(2) ** 2000
    return f.value(x)


def compare(f, a, b, op):
    anynan, snan = _nan_in(f, a, b)
    if anynan:
        if op == "eq":
            return 0, NV if snan else 0
        return 0, NV                 # flt/fle: signaling compares
    x, y = _ord(f, a), _ord(f, b)
    return int({"eq": x == y, "lt": x < y, "le": x <= y}[op]), 0


INT_RANGE = {"w": (-(1 << 31), (1 << 31) - 1, 32), "wu": (0, (1 << 32) - 1, 32),
             "l": (-(1 << 63), (1 << 63) - 1, 64), "lu": (0, (1 << 64) - 1, 64)}


def round_to_int(q, mode):
    """Round an exact rational to an integer; returns (n, inexact)."""
    fl = q.numerator // q.denominator
    r = q - fl
    if r == 0:
        return fl, False
    s = 1 if q < 0 else 0
    if mode == RNE:
        up = r > Fraction(1, 2) or (r == Fraction(1, 2) and fl & 1)
    elif mode == RMM:
        up = r > Fraction(1, 2) or (r == Fraction(1, 2) and s == 0)
    elif mode == RTZ:
        up = s == 1
    elif mode == RDN:
        up = False
    else:
        up = True
    return fl + 1 if up else fl, True


def to_int(f, a, kind, mode):
    """fcvt.{w,wu,l,lu}.{s,d}; returns (x register value, 64-bit, flags)."""
    lo, hi, width = INT_RANGE[kind]

    def out(n):
        n &= (1 << width) - 1
        if width == 32 and n >> 31:
            n |= 0xFFFFFFFF00000000       # RV64: 32-bit results are sign-extended
        return n
    if f.isnan(a):
        return out(hi), NV
    if f.isinf(a):
        return out(lo if f.sign(a) else hi), NV
    n, inexact = round_to_int(f.value(a), mode)
    if n < lo:
        return out(lo), NV
    if n > hi:
        return out(hi), NV
    return out(n), NX if inexact else 0


def from_int(f, x, kind, mode):
    """fcvt.{s,d}.{w,wu,l,lu}; x is the 64-bit register value."""
    _, _, width = INT_RANGE[kind]
    n = x & ((1 << width) - 1)
    if kind in ("w", "l") and n >> (width - 1):
        n -= 1 << width
    return f.from_fraction(Fraction(n), mode)


def convert(src, dst, a, mode):
    """fcvt.s.d / fcvt.d.s."""
    if src.isnan(a):
        return dst.canon, NV if src.issnan(a) else 0
    if src.isinf(a):
        return dst.infinity(src.sign(a)), 0
    if src.iszero(a):
        return dst.zero(src.sign(a)), 0
    return dst.from_fraction(src.value(a), mode)
