"""Stage E4 Part 6: a single-precision oracle, written from IEEE 754.

round_f32(q, mode): the exact rational q rounded to binary32 under rne/rtz/
rdn/rup/rmm, by bracketing q between adjacent binary32 values (bit-pattern
neighbours) and comparing exactly with Fraction. Finite results only (inputs
are chosen not to overflow). sqrt is bracketed by comparing squares.
"""
import math, struct
from fractions import Fraction

f2b = lambda x: struct.unpack("<I", struct.pack("<f", x))[0]
b2f = lambda b: struct.unpack("<f", struct.pack("<I", b))[0]


def _next(b, up):
    """binary32 neighbour of pattern b, toward +inf (up) or -inf."""
    if b & 0x7FFFFFFF == 0:
        return 1 if up else 0x80000001
    neg = b >> 31
    return b + 1 if up != bool(neg) else b - 1


def bracket(q, approx_bits):
    b = approx_bits
    while Fraction(b2f(b)) > q: b = _next(b, False)
    while Fraction(b2f(_next(b, True))) <= q: b = _next(b, True)
    return b, _next(b, True)          # lo <= q < hi


def pick(lo, hi, below_mid, at_mid, q, mode):
    """lo/hi patterns around q (q != lo)."""
    if mode == "rtz": return lo if q > 0 else hi
    if mode == "rdn": return lo
    if mode == "rup": return hi
    if at_mid:
        if mode == "rmm": return hi if q > 0 else lo
        return lo if (lo & 1) == 0 else hi      # ties to even (mantissa lsb)
    return lo if below_mid else hi


def round_f32(q, mode):
    approx = f2b(float(q)) if q != 0 else 0
    lo, hi = bracket(q, approx)
    if Fraction(b2f(lo)) == q:
        return lo if not (q == 0) else 0
    mid = (Fraction(b2f(lo)) + Fraction(b2f(hi))) / 2
    return pick(lo, hi, q < mid, q == mid, q, mode)


def sqrt_f32(a_bits, mode):
    a = Fraction(b2f(a_bits))
    if a == 0: return a_bits
    lo, hi = bracket_sqrt(a)
    if Fraction(b2f(lo)) ** 2 == a: return lo
    mid = (Fraction(b2f(lo)) + Fraction(b2f(hi))) / 2
    return pick(lo, hi, mid * mid > a, mid * mid == a, Fraction(1), mode)


def bracket_sqrt(a):
    b = f2b(math.sqrt(float(a)))
    while Fraction(b2f(b)) ** 2 > a: b = _next(b, False)
    while Fraction(b2f(_next(b, True))) ** 2 <= a: b = _next(b, True)
    return b, _next(b, True)


def is_tie(q):
    if q == 0: return False
    lo, hi = bracket(q, f2b(float(q)))
    if Fraction(b2f(lo)) == q: return False
    return q == (Fraction(b2f(lo)) + Fraction(b2f(hi))) / 2
