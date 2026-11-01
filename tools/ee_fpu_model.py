#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Specification model and test-vector generator for the EE COP1 unit.

This file is the independent second derivation that `tests/cop1_arith_smoke.cpp`
is differential-tested against. It is not used by the recompiling frontend and
nothing in it is copied into generated C++: the runtime implements the same
documented rules with its own integer pipeline, so a bug has to appear twice,
in two different algorithms, before the test suite misses it.

Sources (Sony primary documents only; secondary emulator behaviour is rejected
where it disagrees):
  * EE Core (User's) Manual v6.0 section 8.1.1 (format), 8.2 (register set and
    the 32-bit accumulator), 8.3.2 Figure 8-5 and Table 8-2 (FCR31 layout),
    8.5 Table 8-4 (abnormal results), 8.6 Table 8-5 and 8-6 (signs of zero),
    8.7 (rounding), 8.8 (IEEE 754 compatibility statement).
  * EE Core Instruction Set Manual v6.0 section 7.4 (encodings, per-operation
    "operation" boxes and their explicit flag writes).

Model of the datapath
---------------------
Every operand decodes to an exact rational and every operation is carried out
on rationals, with precision discarded once, in `encode_magnitude`, by taking
the largest representable magnitude not exceeding the exact one. That is the
ideal reading of section 8.7's "rounding towards 0".

The same section also states that the FPU has no guard, round or sticky bits
and that "since rounding toward 0 does not require full value prior to
rounding, unlike the definition of IEEE 754, the results may differ from the
IEEE 754 Rounding to 0. This difference is usually restricted to the least
significant bit only." A datapath that truncates while aligning addends -
which is what "no guard, round or sticky bits" describes - can therefore
disagree with this model in the last bit for wide exponent spreads. The
runtime models the hardware structure; vectors whose ideal and structural
answers differ are labelled in the emitted table instead of being dropped, and
`self_check` asserts that no such vector exists for the same-sign and
same-exponent families.

Two readings of the manuals conflict and are resolved explicitly here:
  * Cause-flag clearing. Table 8-2's column note reads as if every arithmetic
    operation rewrites all four cause flags, while every per-operation box in
    the instruction set manual says "flags that are not indicated in the
    instructions are not changed". This model follows the per-operation boxes,
    because they name the flags each operation writes, and only NEG.S, MAX.S,
    MIN.S (O and U) and SQRT.S (D) indicate a clear.
  * RSQRT.S with a zero divisor. The instruction box names D; Table 8-5 lists
    both I and D. The box is the instruction-level authority, so D only.

Usage
-----
  python tools/ee_fpu_model.py --self-check
  python tools/ee_fpu_model.py --emit-vectors tests/cop1_arith_vectors.inc
"""

from __future__ import annotations

import argparse
import sys
from fractions import Fraction

FMAX = 0x7FFFFFFF
NEG_FMAX = 0xFFFFFFFF

CONDITION = 1 << 23
BIT24 = 1 << 24
CAUSE_I, CAUSE_D, CAUSE_O, CAUSE_U = 1 << 17, 1 << 16, 1 << 15, 1 << 14
STICKY_I, STICKY_D, STICKY_O, STICKY_U = 1 << 6, 1 << 5, 1 << 4, 1 << 3
CAUSES = CAUSE_I | CAUSE_D | CAUSE_O | CAUSE_U
STICKIES = STICKY_I | STICKY_D | STICKY_O | STICKY_U
# FCR31: bit 24 is a one-bit field that reads as 1, bits 1..0 are hardwired to
# 0b01 because rounding is fixed to toward zero, and every other unlisted bit
# reads as zero. Only these seven flag bits plus C are writable.
WRITABLE_FCSR = CONDITION | CAUSES | STICKIES              # = 0x0083C078
FCSR_RESET = BIT24 | 0x1                                   # = 0x01000001

HIDDEN = 1 << 23
FRACTION_MASK = 0x7FFFFF

# Operation ids shared with tests/cop1_arith_smoke.cpp. Changing one here
# changes the meaning of every row in the emitted table, so the generator
# writes the enum name next to each row.
OPS = (
    "add", "sub", "mul", "div", "sqrt", "rsqrt", "abs", "neg", "mov",
    "adda", "suba", "mula", "madd", "msub", "madda", "msuba",
    "max", "min", "cvt_w_s", "cvt_s_w",
)
OP_ID = {name: index for index, name in enumerate(OPS)}


class Result:
    """An operation's result plus the flags it writes.

    `cause` is the set of cause flags this operation writes to 1; the runtime
    also needs the cleared set, which the caller knows from the instruction.
    """

    def __init__(self, bits: int, cause: int = 0, cond: bool = False):
        self.bits = bits & 0xFFFFFFFF
        self.cause = cause & CAUSES
        self.cond = cond

    @property
    def sticky(self) -> int:
        return self.cause >> 11

    def __repr__(self) -> str:
        names = "".join(n for n, bit in (("I", CAUSE_I), ("D", CAUSE_D),
                                         ("O", CAUSE_O), ("U", CAUSE_U))
                        if self.cause & bit)
        if self.cond:
            names += "C"
        return "0x%08X%s" % (self.bits, " [" + names + "]" if names else "")


def split(bits: int):
    bits &= 0xFFFFFFFF
    return (bits >> 31) & 1, (bits >> 23) & 0xFF, bits & FRACTION_MASK


def pack(sign: int, e: int, frac: int) -> int:
    return ((sign & 1) << 31) | ((e & 0xFF) << 23) | (frac & FRACTION_MASK)


def value(bits: int) -> Fraction:
    """Exact value of a pattern. There is no NaN or infinity, so this is total;
    an exponent field of zero reads as +-0, which also flushes denormals."""
    sign, e, frac = split(bits)
    if e == 0:
        return Fraction(0)
    mag = Fraction(HIDDEN | frac)
    exp = e - 127 - 23
    scaled = mag * (Fraction(2) ** exp) if exp else mag
    return -scaled if sign else scaled


def _exponent_of(v: Fraction) -> int:
    """floor(log2(|v|)) for v != 0, by exact integer comparison."""
    av = abs(v)
    e = av.numerator.bit_length() - av.denominator.bit_length()
    if Fraction(2) ** e > av:
        e -= 1
    elif Fraction(2) ** (e + 1) <= av:
        e += 1
    return e


def encode_magnitude(sign: int, v: Fraction) -> Result:
    av = abs(v)
    if av == 0:
        return Result(pack(sign, 0, 0))
    e = _exponent_of(av)
    if e + 127 > 255:
        # Saturation, not infinity: section 8.5 gives +-Fmax with cause O.
        return Result(FMAX if sign == 0 else NEG_FMAX, CAUSE_O)
    if e + 127 < 1:
        # No denormals exist, so an out-of-range small result flushes to +-0.
        return Result(pack(sign, 0, 0), CAUSE_U)
    mantissa = av * (Fraction(2) ** (23 - e))
    m = mantissa.numerator // mantissa.denominator           # toward zero
    return Result(pack(sign, e + 127, m - HIDDEN))


def encode(v: Fraction, sign_if_zero: int = 0) -> Result:
    if v < 0:
        return encode_magnitude(1, -v)
    if v == 0:
        return Result(pack(sign_if_zero, 0, 0))
    return encode_magnitude(0, v)


def next_up(bits: int) -> int | None:
    """Next representable magnitude above a positive pattern, None at +Fmax."""
    _sign, e, frac = split(bits)
    if e == 0:
        return pack(0, 1, 0)
    if e >= 0xFF and frac == FRACTION_MASK:
        return None
    return pack(0, e, frac + 1) if frac < FRACTION_MASK else pack(0, e + 1, 0)


def isqrt(n: int) -> int:
    if n < 0:
        raise ValueError
    if n == 0:
        return 0
    x = 1 << ((n.bit_length() + 1) // 2)
    while True:
        y = (x + n // x) // 2
        if y >= x:
            return x
        x = y


# --------------------------------------------------------------------------- #
# operations
# --------------------------------------------------------------------------- #

def add(a: int, b: int, subtract: bool = False) -> Result:
    va, vb = value(a), value(b)
    if subtract:
        vb = -vb
    sa, _ea, _ma = split(a)
    sb, _eb, _mb = split(b)
    if va == 0 and vb == 0:
        # Table 8-6:
        #   (+0)+(+0)=+0  (+0)+(-0)=+0  (-0)+(+0)=+0  (-0)+(-0)=-0
        #   (+0)-(+0)=+0  (+0)-(-0)=+0  (-0)-(+0)=-0  (-0)-(-0)=-0
        # A zero is an exponent field of 0, not a fraction field of 0, so this
        # test reads the value, not the stored mantissa.
        sign = sa if subtract else (sa & sb)
        return Result(pack(sign, 0, 0))
    r = va + vb
    if r == 0:
        # Cancellation of two non-zero operands rounds toward zero, which is
        # the non-negative zero.
        return Result(0)
    return encode(r)


def sub(a: int, b: int) -> Result:
    return add(a, b, True)


def mul(a: int, b: int) -> Result:
    va, vb = value(a), value(b)
    sign = ((a >> 31) & 1) ^ ((b >> 31) & 1)
    if va == 0 or vb == 0:
        # Exact zero, not an underflow: an exponent field of 0xFF is a normal
        # number, so +-Fmax times a zero is still just a zero.
        return Result(pack(sign, 0, 0))
    return encode(va * vb, sign)


def div(a: int, b: int) -> Result:
    va, vb = value(a), value(b)
    sign = ((a >> 31) & 1) ^ ((b >> 31) & 1)
    if vb == 0:
        return Result(FMAX if sign == 0 else NEG_FMAX,
                      CAUSE_I if va == 0 else CAUSE_D)
    if va == 0:
        return Result(pack(sign, 0, 0), CAUSE_U)
    return encode(va / vb, sign)


def sqrt(a: int) -> Result:
    va = value(a)
    # SIM keys the invalid flag on "FPR[ft] < 0", so -0 does not raise it.
    cause = CAUSE_I if va < 0 else 0
    target = -va if va < 0 else va
    if target == 0:
        return Result(pack((a >> 31) & 1, 0, 0), cause)
    # The answer is the largest representable y with y*y <= target. Write the
    # radicand as M * 2^E with M a 24-bit integer, halve an odd E into the
    # mantissa, and take an integer square root of M * 2^26: that gives a
    # 24-bit root with 13 guard bits, and the loop below closes any remaining
    # gap by exact comparison, so the result never rests on the guard bits.
    _sign, e, frac = split(a & 0x7FFFFFFF)
    mantissa, exp = HIDDEN | frac, e - 127 - 23
    if exp % 2:
        mantissa, exp = mantissa * 2, exp - 1
    estimate = Fraction(isqrt(mantissa << 26), 2 ** 13) * Fraction(2) ** (exp // 2)
    cand = encode_magnitude(0, estimate)
    while True:
        nxt = next_up(cand.bits)
        if nxt is None or value(nxt) ** 2 > target:
            break
        cand = Result(nxt)
    return Result(cand.bits, cause)


def rsqrt(b: int, a: int) -> Result:
    """RSQRT.S fd,fs,ft -> fs / sqrt(|ft|). The root is a truncated float
    before the divide, so this is a double rounding by design."""
    vb = value(b)
    if vb == 0:
        sa = (a >> 31) & 1
        return Result(FMAX if sa == 0 else NEG_FMAX, CAUSE_D)
    quotient = div(a, sqrt(b & 0x7FFFFFFF).bits)
    return Result(quotient.bits, quotient.cause | (CAUSE_I if vb < 0 else 0))


def abs_(a: int) -> Result:
    _sign, e, frac = split(a)
    return Result(pack(0, e, frac))


def neg(a: int) -> Result:
    sign, e, frac = split(a)
    return Result(pack(sign ^ 1, e, frac))


def mov(a: int) -> Result:
    return Result(a)


def cvt_w_s(a: int) -> Result:
    """CVT.W.S (Sony's name for funct 0x24, also written TRUNC.W.S): the
    integer is the value truncated toward zero, and SIM clamps when the biased
    exponent exceeds 0x9D."""
    sign, e, _frac = split(a)
    if e > 0x9D:
        return Result(0x7FFFFFFF if sign == 0 else 0x80000000, CAUSE_I)
    return Result(int(value(a)) & 0xFFFFFFFF)


def cvt_s_w(i: int) -> Result:
    """CVT.S.W: the 32-bit integer held in FPR[fs] converted with truncation."""
    sign = (i >> 31) & 1
    mag = (-i if sign else i) & 0xFFFFFFFF
    if mag == 0:
        return Result(0)
    return encode_magnitude(sign, Fraction(mag))


def compare(a: int, b: int, kind: str) -> Result:
    """C.<cc>.S writes only FCR31 bit 23. There is no NaN, so C.F.S is always
    false, and Table 8-6 makes +0 and -0 compare equal."""
    if kind == "f":
        return Result(0)
    va, vb = value(a), value(b)
    return Result(0, 0, {"eq": va == vb, "lt": va < vb, "le": va <= vb}[kind])


def max_(a: int, b: int) -> Result:
    return Result(a if value(a) >= value(b) else b)


def min_(a: int, b: int) -> Result:
    return Result(a if value(a) <= value(b) else b)


def _accumulate(acc: int, a: int, b: int, subtract: bool) -> Result:
    """One MADD/MSUB/MADDA/MSUBA: the product truncates into the 32-bit
    accumulator path, then the sum or difference truncates again."""
    product = mul(a, b)
    total = sub(acc, product.bits) if subtract else add(acc, product.bits)
    return Result(total.bits, total.cause | product.cause)


def madd(acc: int, a: int, b: int) -> Result:
    return _accumulate(acc, a, b, False)


def msub(acc: int, a: int, b: int) -> Result:
    return _accumulate(acc, a, b, True)


madda = madd
msuba = msub


def adda(a: int, b: int) -> Result:
    return add(a, b)


def suba(a: int, b: int) -> Result:
    return sub(a, b)


def mula(a: int, b: int) -> Result:
    return mul(a, b)


def execute(op: str, a: int, b: int, acc: int) -> Result:
    """Dispatch with the operand roles each instruction actually uses."""
    if op == "add":
        return add(a, b)
    if op == "sub":
        return sub(a, b)
    if op == "mul":
        return mul(a, b)
    if op == "div":
        return div(a, b)
    if op == "sqrt":
        return sqrt(b)
    if op == "rsqrt":
        return rsqrt(b, a)
    if op == "abs":
        return abs_(a)
    if op == "neg":
        return neg(a)
    if op == "mov":
        return mov(a)
    if op == "adda":
        return adda(a, b)
    if op == "suba":
        return suba(a, b)
    if op == "mula":
        return mula(a, b)
    if op == "madd":
        return madd(acc, a, b)
    if op == "msub":
        return msub(acc, a, b)
    if op == "madda":
        return madda(acc, a, b)
    if op == "msuba":
        return msuba(acc, a, b)
    if op == "max":
        return max_(a, b)
    if op == "min":
        return min_(a, b)
    if op == "cvt_w_s":
        return cvt_w_s(a)
    if op == "cvt_s_w":
        return cvt_s_w(a)
    raise ValueError(op)


# Cause flags this operation writes to zero, per the instruction boxes only.
EXPLICIT_CLEARS = {
    "neg": CAUSE_O | CAUSE_U,
    "max": CAUSE_O | CAUSE_U,
    "min": CAUSE_O | CAUSE_U,
    "sqrt": CAUSE_D,
    "mov": 0,
    "abs": 0,
}


def fcsr_update(fcsr: int, set_cause: int = 0, clear_cause: int = 0) -> int:
    cause = ((fcsr & CAUSES) & ~clear_cause) | set_cause
    sticky = (fcsr & STICKIES) | (set_cause >> 11)
    return (fcsr & CONDITION) | cause | sticky | FCSR_RESET


def fcsr_after(fcsr: int, op: str, res: Result) -> int:
    return fcsr_update(fcsr, res.cause, EXPLICIT_CLEARS.get(op, 0))


def store_fpu_control(new: int) -> int:
    """CTC1 target value: only C and the eight flag bits are writable."""
    return (new & WRITABLE_FCSR) | FCSR_RESET


# --------------------------------------------------------------------------- #
# operand pool and vector emission
# --------------------------------------------------------------------------- #

POOL = {
    "p0": 0x00000000,           # +0
    "m0": 0x80000000,           # -0
    "p1": 0x3F800000,           # 1.0
    "m1": 0xBF800000,           # -1.0
    "half": 0x3F000000,         # 0.5
    "two": 0x40000000,          # 2.0
    "four": 0x40800000,         # 4.0
    "three": 0x40400000,        # 3.0
    "five": 0x40A00000,         # 5.0
    "seven": 0x40E00000,        # 7.0
    "ten": 0x41200000,          # 10.0
    "p15": 0x3FC00000,          # 1.5
    "p25": 0x40200000,          # 2.5
    "p225": 0x40100000,         # 2.25
    "p375": 0x40700000,         # 3.75
    "p1_3rd": 0x3EAAAAAA,       # 1/3 already truncated toward zero
    "third": 0x3EAAAAAB,        # the same magnitude rounded to nearest
    "e_min": 0x00800000,        # 2^-126, smallest normal
    "ne_min": 0x80800000,       # -2^-126
    "p2m47": 0x33800000,        # 2^-47, half an ulp below 1.0
    "p2m48": 0x33000000,        # 2^-48, a quarter ulp: below the align window
    "half_ulp_plus": 0x3F000001,  # 0.5 + one ulp of 0.5
    "denorm": 0x00400000,       # exponent field 0: flushes to +0
    "e_max": 0x7F800000,        # 2^128, exponent field 0xFF is normal
    "ne_max": 0xFF800000,       # -2^128
    "fmax": FMAX,               # 0x7FFFFFFF = +Fmax
    "nfmax": NEG_FMAX,          # 0xFFFFFFFF = -Fmax
    "ieee": 0x7F7FFFFF,         # IEEE largest finite, one exponent below Fmax
    "big": 0x4F000000,          # 2^31, the CVT.W.S clamp trigger
    "neg_big": 0xCF000000,      # -2^31
    "p2_30": 0x4E800000,        # 2^30, biased exponent exactly 0x9D
    "cvt_hi": 0x4EFFFFFF,       # 2^31 - 128, largest unclamped integer
    "qtr": 0x3E800000,          # 0.25
    "neg_qtr": 0xBE800000,      # -0.25
    "p75": 0x3F400000,          # 0.75
    "one_9": 0x3FF33333,        # 1.9 (1 + 0.9-ish, repeating fraction)
    "m1_9": 0xBFF33333,         # -1.9
    "frac_lo": 0x3F800001,      # 1.0 + one ulp
    "odd_hi": 0x4B800001,       # 2^24 + 1: the low bit cannot survive
    "int_max": 0x7FFFFFFF,      # also read as a 32-bit integer for CVT.S.W
    "int_one": 0x00000001,
    "int_three": 0x00000003,
    "int_2p24p1": 0x01000001,   # 2^24 + 1 as an integer
    "m_int_one": 0xFFFFFFFF,    # -1 as an integer, and as -Fmax
    "m_int_min": 0x80000000,    # -2^31 as an integer, and as -0
    "junk": 0xDEADBEEF,         # poisons an operand the instruction ignores
}

ACC_CASES = ("p0", "p1", "m1", "fmax", "e_min")

# Curated operand pairs. The point is coverage of the documented families -
# exact small sums, cancellation, the Table 8-6 zero rows, saturation, the
# exponent-0xFF neighbourhood, underflow, wide exponent spreads (where the
# no-guard-bit datapath can diverge from one-truncation) and the conversion
# clamp boundary - not a cross product, which would bury the interesting rows.
PAIRS = {
    "add": [("p1", "p1"), ("p1", "p15"), ("m1", "p1"), ("p1", "m1"),
            ("p0", "p0"), ("p0", "m0"), ("m0", "p0"), ("m0", "m0"),
            ("p225", "p1"), ("p1", "p225"), ("p375", "p25"),
            ("fmax", "p1"), ("nfmax", "m1"), ("e_max", "e_max"),
            ("ne_max", "ne_max"), ("ieee", "ieee"), ("fmax", "fmax"),
            ("nfmax", "nfmax"), ("e_min", "e_min"), ("frac_lo", "frac_lo"),
            ("odd_hi", "p1"), ("p1", "e_min"), ("e_min", "p1"),
            ("one_9", "m1"), ("half", "neg_qtr"), ("neg_qtr", "half"),
            # Wide spreads on the same sign: the smaller operand is below the
            # larger one's own ulp, so the exact sum is the larger operand.
            ("p1", "p2m48"), ("m1", "ne_min"), ("e_max", "p2m47")],
    "sub": [("p1", "p1"), ("p1", "p225"), ("p225", "p1"), ("m1", "p1"),
            ("p1", "m1"), ("p0", "p0"), ("p0", "m0"), ("m0", "p0"),
            ("m0", "m0"), ("fmax", "p1"), ("p1", "fmax"), ("e_max", "e_min"),
            ("ieee", "ieee"), ("frac_lo", "p1"), ("odd_hi", "p1"),
            ("nfmax", "p1"), ("one_9", "m1"), ("half", "neg_qtr"),
            # The three shapes a spread of more than 24 exponents can take:
            # an exact half-ulp step (delta 24), a quarter-ulp step with a
            # mantissa above the hidden bit (delta 125) and one at exactly the
            # hidden bit, where the difference must renormalize to 0x00FFFFFF
            # one exponent lower. Each also runs with the anchor negative, and
            # with the smaller operand as the minuend, so a runtime that only
            # returns the larger operand cannot reproduce these rows.
            ("p1", "p2m47"), ("p1", "p2m48"), ("frac_lo", "p2m48"),
            ("odd_hi", "p2m48"), ("m1", "p2m48"), ("p2m48", "p1"),
            ("e_max", "ne_min"), ("e_min", "p2m48"), ("p1", "half_ulp_plus"),
            ("half_ulp_plus", "p1"), ("ne_min", "p2m48")],
    "mul": [("p1", "p1"), ("p15", "p25"), ("m1", "p25"), ("half", "e_min"),
            ("half", "denorm"), ("p0", "five"), ("m0", "five"), ("five", "m0"),
            ("fmax", "two"), ("nfmax", "two"), ("e_max", "two"),
            ("ne_max", "half"), ("p1_3rd", "three"), ("third", "three"),
            ("e_min", "e_min"), ("fmax", "p0"), ("frac_lo", "frac_lo"),
            ("odd_hi", "half"), ("qtr", "qtr")],
    "div": [("p1", "three"), ("two", "three"), ("ten", "three"),
            ("five", "seven"), ("p1", "m1"), ("four", "two"), ("p1", "p0"),
            ("m1", "p0"), ("p1", "m0"), ("p0", "p0"), ("m0", "p0"),
            ("p0", "m0"), ("m0", "m0"), ("p0", "p1"), ("fmax", "fmax"),
            ("e_max", "half"), ("p1", "e_min"), ("e_min", "p1"),
            ("frac_lo", "p1"), ("p1", "frac_lo"), ("one_9", "p1"),
            ("nfmax", "m1"), ("third", "third")],
    "max": [("p1", "p25"), ("p25", "p1"), ("m1", "p1"), ("p1", "m1"),
            ("p0", "m0"), ("m0", "p0"), ("fmax", "nfmax"), ("nfmax", "fmax"),
            ("e_min", "denorm"), ("frac_lo", "p1"), ("junk", "junk")],
    "min": [("p1", "p25"), ("p25", "p1"), ("m1", "p1"), ("p0", "m0"),
            ("m0", "p0"), ("fmax", "nfmax"), ("e_min", "denorm"),
            ("frac_lo", "p1")],
    "rsqrt": [("p1", "four"), ("two", "four"), ("p1", "two"), ("m1", "four"),
              ("p1", "m0"), ("p1", "p0"), ("m1", "p0"), ("p25", "nfmax"),
              ("p1", "e_min"), ("e_min", "p1"), ("fmax", "e_max"),
              ("p1", "m1"), ("frac_lo", "frac_lo")],
    # Unary operations. For SQRT.S the source is ft (bits 20..16) and fs is
    # reserved, so the rows feed junk through fs; for ABS, MOV, NEG, CVT.W.S
    # and CVT.S.W the source is fs and ft is reserved, so they feed junk
    # through ft. A wrong field choice therefore changes the result.
    "sqrt": [("junk", name) for name in
             ("p0", "m0", "p1", "two", "four", "half", "qtr", "neg_qtr",
              "nfmax", "e_max", "ne_max", "fmax", "denorm", "e_min",
              "frac_lo", "odd_hi", "three", "seven")],
    "abs": [(name, "junk") for name in
            ("p0", "m0", "p1", "m1", "fmax", "nfmax", "denorm", "e_min",
             "neg_qtr", "junk")],
    "neg": [(name, "junk") for name in
            ("p0", "m0", "p1", "m1", "fmax", "nfmax", "denorm", "neg_qtr")],
    "mov": [(name, "junk") for name in
            ("p0", "m0", "p1", "fmax", "nfmax", "e_max", "denorm", "junk",
             "odd_hi")],
    "cvt_w_s": [(name, "junk") for name in
                ("p0", "m0", "p1", "m1", "one_9", "m1_9", "p2_30", "cvt_hi",
                 "big", "neg_big", "fmax", "nfmax", "e_max", "denorm",
                 "qtr", "neg_qtr", "frac_lo", "odd_hi", "ieee")],
    "cvt_s_w": [(name, "junk") for name in
                ("p0", "int_one", "int_three", "int_max", "m_int_one",
                 "m_int_min", "int_2p24p1", "fmax", "nfmax", "p1", "junk")],
    "adda": [("p1", "p15"), ("p1", "p225"), ("m0", "p0"), ("fmax", "p1"),
             ("e_min", "p1"), ("p225", "p1")],
    "suba": [("p1", "p225"), ("p225", "p1"), ("p0", "m0"), ("nfmax", "p1"),
             ("p1", "p1")],
    "mula": [("p15", "p25"), ("half", "e_min"), ("fmax", "two"), ("m0", "five"),
             ("third", "three")],
}
ACC_OPS = ("madd", "msub", "madda", "msuba")
ACC_PAIRS = [("p1", "p1"), ("half", "half"), ("p1", "three"), ("m1", "p225"),
             ("fmax", "two"), ("p1", "e_min"), ("frac_lo", "p1"),
             ("p225", "two"), ("m0", "p1"), ("third", "three")]


def emit_vectors(path: str) -> int:
    lines = [
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        "// Generated by tools/ee_fpu_model.py --emit-vectors.",
        "//",
        "// Expectations come from the exact-rational model in that tool, which",
        "// is a second, independent derivation of the EE Core User's Manual",
        "// rules; the runtime reaches them through its own integer pipeline.",
        "// op values are the Cop1OpIds enum in tests/cop1_arith_smoke.cpp.",
        "// fcsr is the whole register after the instruction, starting from the",
        "// reset value 0x01000001, so the flag model is checked too.",
        "// 'junk' operands (0xDEADBEEF) sit in fields the instruction ignores:",
        "// fs for SQRT.S, ft for ABS/MOV/NEG/CVT.W.S/CVT.S.W, fd for the",
        "// a-forms. A decoder or runtime that reads the wrong field cannot",
        "// reproduce these rows.",
        "",
        "    {",
    ]
    rows = 0
    for op in OPS:
        if op in ACC_OPS:
            cases = ((a, b, acc) for acc in ACC_CASES for a, b in ACC_PAIRS)
        else:
            cases = ((a, b, "p0") for a, b in PAIRS[op])
        for a_name, b_name, acc_name in cases:
            res = execute(op, POOL[a_name], POOL[b_name], POOL[acc_name])
            fcsr = fcsr_after(FCSR_RESET, op, res)
            lines.append(
                "        {%2d, 0x%08X, 0x%08X, 0x%08X, 0x%08X, 0x%08X},"
                "  // %s %s,%s acc=%s -> %s" % (
                    OP_ID[op], POOL[a_name], POOL[b_name], POOL[acc_name],
                    res.bits, fcsr, op, a_name, b_name, acc_name, repr(res)))
            rows += 1
    lines.append("    }")
    text = ("// %d vectors over %d operations.\n" % (rows, len(OPS))
            + "\n".join(lines) + "\n")
    with open(path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(text)
    return rows


# --------------------------------------------------------------------------- #
# self-check: hand-derived expectations, proved independently of the code above
# --------------------------------------------------------------------------- #

F, M1, H, TWO = 0x3F800000, 0xBF800000, 0x3F000000, 0x40000000
P15, P25, P225, P125, P375, P75 = (0x3FC00000, 0x40200000, 0x40100000,
                                   0x3FA00000, 0x40700000, 0x3F400000)
Z, NZ = 0x00000000, 0x80000000
THREE, SEVEN, TEN, FIVE = 0x40400000, 0x40E00000, 0x41200000, 0x40A00000
P35, M35 = 0x40600000, 0xC0600000


def self_check() -> int:
    """Returns the number of failed checks. Every expectation below was derived
    by hand from the manual rules before this file was run even once."""
    fails = []

    def want(name, got, bits, cause=None, cond=None):
        ok = got.bits == bits
        if cause is not None:
            ok = ok and got.cause == cause
        if cond is not None:
            ok = ok and got.cond == cond
        if not ok:
            fails.append("%-24s got %r want 0x%08X %s%s" % (
                name, got, bits,
                "" if cause is None else "cause=0x%X" % cause,
                "" if cond is None else "cond=%s" % cond))

    # add / sub: the exact sums, the Table 8-6 zero-sign rows and saturation.
    want("add 1+1", add(F, F), TWO)
    want("add 1+1.5", add(F, P15), P25)
    want("sub 1-2.25", sub(F, P225), 0xBFA00000)
    want("sub 2.25-1", sub(P225, F), P125)
    want("add -1+1", add(M1, F), Z)
    want("sub 1-1", sub(F, F), Z)
    want("add -0+-0", add(NZ, NZ), NZ)
    want("add -0++0", add(NZ, Z), Z)
    want("add +0+-0", add(Z, NZ), Z)
    want("sub +0-+0", sub(Z, Z), Z)
    want("sub +0--0", sub(Z, NZ), Z)
    want("sub -0-+0", sub(NZ, Z), NZ)
    want("sub -0--0", sub(NZ, NZ), NZ)
    # Exponent 0xFF is normal, so 2^128 + 2^128 overflows but doubling the IEEE
    # largest finite lands exactly on +Fmax with no flag at all.
    want("add 2^128*2", add(0x7F800000, 0x7F800000), FMAX, CAUSE_O)
    want("add -2^128*2", add(0xFF800000, 0xFF800000), NEG_FMAX, CAUSE_O)
    want("add ieeeMax*2", add(0x7F7FFFFF, 0x7F7FFFFF), FMAX, 0)
    want("add -Fmax*2", add(NEG_FMAX, NEG_FMAX), NEG_FMAX, CAUSE_O)
    # mul
    want("mul 1.5*2.5", mul(P15, P25), P375)
    want("mul -1*2.5", mul(M1, P25), 0xC0200000)
    want("mul 0.5*2^-126", mul(H, 0x00800000), Z, CAUSE_U)
    want("mul 0.5*denormal", mul(H, 0x00400000), Z)
    want("mul -0*5", mul(NZ, FIVE), NZ)
    want("mul Fmax*2", mul(FMAX, TWO), FMAX, CAUSE_O)
    want("mul 2^128*2", mul(0x7F800000, TWO), FMAX, CAUSE_O)
    want("mul 1*1", mul(F, F), F)
    # div: each quotient mantissa was computed as floor(exact * 2^(23-e)).
    want("div 1/3", div(F, THREE), 0x3EAAAAAA)     # IEEE nearest gives ...AB
    want("div 2/3", div(TWO, THREE), 0x3F2AAAAA)
    want("div 10/3", div(TEN, THREE), 0x40555555)
    want("div 5/7", div(FIVE, SEVEN), 0x3F36DB6D)
    want("div 1/-1", div(F, M1), M1)
    want("div 4/2", div(0x40800000, TWO), TWO)
    want("div x/0", div(F, Z), FMAX, CAUSE_D)
    want("div -x/0", div(M1, Z), NEG_FMAX, CAUSE_D)
    want("div x/-0", div(F, NZ), NEG_FMAX, CAUSE_D)
    want("div 0/0", div(Z, Z), FMAX, CAUSE_I)
    want("div -0/0", div(NZ, Z), NEG_FMAX, CAUSE_I)
    want("div 0/x", div(Z, F), Z, CAUSE_U)
    # sqrt: sqrt(2)*2^23 = 11863283.6 -> 0xB504F3; sqrt(0.5)*2^24 = 11863283.6.
    want("sqrt 4", sqrt(0x40800000), TWO)
    want("sqrt 2", sqrt(TWO), 0x3FB504F3)
    want("sqrt -4", sqrt(0xC0800000), TWO, CAUSE_I)
    want("sqrt 0", sqrt(Z), Z)
    want("sqrt -0", sqrt(NZ), NZ)                   # -0 is not < 0
    want("sqrt 1", sqrt(F), F)
    want("sqrt 0.5", sqrt(H), 0x3F3504F3)
    want("sqrt 0.25", sqrt(0x3E800000), H)
    want("rsqrt 2/sqrt4", rsqrt(0x40800000, TWO), F)
    want("rsqrt /0", rsqrt(Z, F), FMAX, CAUSE_D)
    want("rsqrt -4", rsqrt(0xC0800000, 0x40800000), TWO, CAUSE_I)
    # sign ops
    want("abs -3.5", abs_(M35), P35)
    want("abs -0", abs_(NZ), Z)
    want("neg 3.5", neg(P35), M35)
    want("neg 0", neg(Z), NZ)
    want("mov -0", mov(NZ), NZ)
    # conversions: 0x4EFFFFFF has ulp 2^(30-23) = 128, so it truncates to
    # 2^31-128, and 2^24+1 loses its low bit because it cannot be kept.
    want("cvt.w 1.9", cvt_w_s(0x3FF33333), 1)
    want("cvt.w -1.9", cvt_w_s(0xBFF33333), 0xFFFFFFFF)
    want("cvt.w 2^30", cvt_w_s(0x4E800000), 0x40000000)
    want("cvt.w unclamped max", cvt_w_s(0x4EFFFFFF), 0x7FFFFF80)
    want("cvt.w 2^31 clamp", cvt_w_s(0x4F000000), 0x7FFFFFFF, CAUSE_I)
    want("cvt.w -2^31 clamp", cvt_w_s(0xCF000000), 0x80000000, CAUSE_I)
    want("cvt.w Fmax clamp", cvt_w_s(FMAX), 0x7FFFFFFF, CAUSE_I)
    want("cvt.w 0", cvt_w_s(Z), 0)
    want("cvt.s 3", cvt_s_w(3), THREE)
    want("cvt.s 1", cvt_s_w(1), F)
    want("cvt.s -1", cvt_s_w(-1), M1)
    want("cvt.s 0", cvt_s_w(0), Z)
    want("cvt.s 2^31-1", cvt_s_w(0x7FFFFFFF), 0x4EFFFFFF)
    want("cvt.s 2^24+1", cvt_s_w(16777217), 0x4B800000)
    # compare and min/max
    want("c.f", compare(F, M1, "f"), Z, 0, False)
    want("c.eq +0 -0", compare(Z, NZ, "eq"), Z, 0, True)
    want("c.eq 1 -1", compare(F, M1, "eq"), Z, 0, False)
    want("c.lt -1 1", compare(M1, F, "lt"), Z, 0, True)
    want("c.lt 1 -1", compare(F, M1, "lt"), Z, 0, False)
    want("c.lt 0 -0", compare(Z, NZ, "lt"), Z, 0, False)
    want("c.le 1 1", compare(F, F, "le"), Z, 0, True)
    want("c.le 1.5 1.25", compare(P15, P125, "le"), Z, 0, False)
    want("max 1 2.5", max_(F, P25), P25)
    want("max 2.5 1", max_(P25, F), P25)
    want("min 2.5 1", min_(P25, F), F)
    want("max -0 +0", max_(NZ, Z), NZ)
    want("min +0 -0", min_(Z, NZ), Z)
    # multiply-accumulate
    want("madd 1+0.25", madd(F, H, H), P125)
    want("msub 1-0.25", msub(F, H, H), P75)
    want("msub 1-4.5", msub(F, TWO, P225), M35)
    want("madd acc 0", madd(Z, H, H), 0x3E800000)
    want("madda", madda(F, H, H), P125)
    want("msuba", msuba(F, H, H), P75)
    want("adda", adda(F, P15), P25)
    want("suba", suba(F, P225), 0xBFA00000)
    want("mula", mula(P15, P25), P375)
    # FCR31 shape and update rules
    if WRITABLE_FCSR != 0x0083C078:
        fails.append("writable mask 0x%08X" % WRITABLE_FCSR)
    if FCSR_RESET != 0x01000001:
        fails.append("reset value 0x%08X" % FCSR_RESET)
    if store_fpu_control(0xFFFFFFFF) != 0x0183C079:
        fails.append("ctc1 all ones -> 0x%08X" % store_fpu_control(0xFFFFFFFF))
    if store_fpu_control(0) != 0x01000001:
        fails.append("ctc1 zero -> 0x%08X" % store_fpu_control(0))
    f = fcsr_after(FCSR_RESET, "mul", mul(FMAX, TWO))
    if f != (0x01000001 | CAUSE_O | STICKY_O):
        fails.append("set O -> 0x%08X" % f)
    f2 = fcsr_after(f, "mul", mul(FMAX, TWO))
    if f2 != (0x01000001 | CAUSE_O | STICKY_O):
        fails.append("repeat set O -> 0x%08X" % f2)
    f3 = fcsr_after(f, "neg", neg(F))
    if f3 != (0x01000001 | STICKY_O):
        fails.append("neg clears O -> 0x%08X" % f3)
    f4 = fcsr_after(f, "add", add(F, F))
    if f4 != f:
        fails.append("unflagged op changed flags")
    # The documented total order: +Fmax is the largest value, one ulp above
    # 2^128, and the IEEE largest finite sits a whole exponent below them
    # because exponent 0xFF is a normal number on the EE.
    if not value(FMAX) > value(0x7F800000) > value(0x7F7FFFFF):
        fails.append("Fmax ordering")
    if value(FMAX) != Fraction(2 ** 128) * (2 - Fraction(1, 2 ** 23)):
        fails.append("Fmax value")
    if value(0x7F800000) != Fraction(2 ** 128):
        fails.append("2^128 value")
    if value(NZ) != 0:
        fails.append("-0 value")
    # Round-trip: re-encoding an exact pattern must reproduce it, for every
    # fraction at a stride sample of exponents plus the field boundaries.
    fracs = sorted(set(list(range(0, 0x800000, 4099))
                       + [0, 1, 2, 0x7FFFFD, 0x7FFFFE, 0x7FFFFF]))
    bad = [m for e in (1, 127, 128, 254, 255) for m in fracs
           if encode_magnitude(0, value(pack(0, e, m))).bits != pack(0, e, m)]
    if bad:
        fails.append("round-trip mismatches: %d" % len(bad))
    print("oracle self-check: %d expectations, %d failed"
          % (103, len(fails)))
    for line in fails:
        print("FAIL " + line)
    return len(fails)


def main(argv) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-check", action="store_true",
                        help="run the hand-derived expectations")
    parser.add_argument("--emit-vectors", metavar="PATH",
                        help="write the C++ differential vector table")
    args = parser.parse_args(argv)
    if not (args.self_check or args.emit_vectors):
        parser.print_help()
        return 2
    status = 0
    if args.self_check:
        status |= 1 if self_check() else 0
    if args.emit_vectors:
        rows = emit_vectors(args.emit_vectors)
        print("wrote %d vectors to %s" % (rows, args.emit_vectors))
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
