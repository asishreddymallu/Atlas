/**
 * @file canonical_nan_test.cpp
 * @brief RISC-V's canonical NaN rule, on rvss and all four rv5s variants.
 *
 * The F extension's "NaN Generation and Propagation" rule: unless an
 * instruction says otherwise, a NaN RESULT is the canonical NaN -
 * positive sign, quiet bit set, payload zero. 0x7FC00000 single,
 * 0x7FF8000000000000 double. The payload of an input NaN is not
 * propagated, and neither is its sign.
 *
 * Measured before writing this (306 cases x 5 models): every arithmetic
 * op propagated the input payload and sign verbatim, and the invalid
 * operations returned whatever x86 produces - a NEGATIVE NaN for
 * inf-inf, 0*inf, inf/inf and fma(inf,0,c).
 *
 * What is deliberately NOT asserted here, and why:
 *
 *   - fflags. CSR 0x001 is never written by anything (measured 0 in all
 *     1530 survey runs); the ALU writes CSR 0x003 with a bit order that
 *     is the reverse of RISC-V's, narrowed to a bool for every double
 *     op. See the fflags entry in docs/audit/known_issues.md. Asserting
 *     the current behaviour would pin an encoding that has to change.
 *   - fclass. It does not assemble - code_generator.cpp throws "Invalid
 *     instruction type: fclass.s" - because of a missing comma in
 *     instructions.cpp, so two adjacent string literals concatenate and
 *     neither name is in the R3-type set. known_issues 52.
 *   - fdiv of a non-zero by zero. It returns a NaN where the answer is
 *     an infinity; that is its own defect, with its own entry, and
 *     asserting a canonical NaN there would pin the bug.
 *   - fcvt.w.s of a NaN, which saturates the wrong way. Its own entry;
 *     fcvt.l.s/d carry the integer-result proof instead.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;

namespace
{
struct NanInput
{
    const char *what;
    uint64_t    bits;
};

/// The four NaN shapes, in both widths. A canonical input is included so
/// that "it was already canonical" and "it was made canonical" are told
/// apart by the other three.
const NanInput kSingleNaNs[] = {
    {"canonical qNaN", 0x7FC00000ULL},
    {"negative qNaN", 0xFFC00000ULL},
    {"qNaN with a high payload", 0x7FE00000ULL},
    {"sNaN", 0x7F800001ULL},
};
const NanInput kDoubleNaNs[] = {
    {"canonical qNaN", 0x7FF8000000000000ULL},
    {"negative qNaN", 0xFFF8000000000000ULL},
    {"qNaN with a high payload", 0x7FFC000000000000ULL},
    {"sNaN", 0x7FF0000000000001ULL},
};

constexpr uint64_t kOneS = 0x3F800000ULL, kOneD = 0x3FF0000000000000ULL;
constexpr uint64_t kInfS = 0x7F800000ULL, kInfD = 0x7FF0000000000000ULL;
constexpr uint64_t kZeroBits = 0x0ULL;
constexpr uint64_t kMinusOneS = 0xBF800000ULL, kMinusOneD = 0xBFF0000000000000ULL;

std::string loadW(int fpr, uint64_t bits, bool isDouble, int scratch)
{
    return isDouble ? loadDouble(fpr, bits, scratch)
                    : loadSingle(fpr, static_cast<uint32_t>(bits), scratch);
}
uint64_t canonical(bool isDouble) { return isDouble ? kCanonicalNaN64 : kCanonicalNaN32; }
} // namespace

TEST(CanonicalNaN, ArithmeticProducesTheCanonicalNaN)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (bool isDouble : {false, true})
        {
            const auto &nans = isDouble ? kDoubleNaNs : kSingleNaNs;
            const char *w    = isDouble ? "d" : "s";
            const uint64_t one = isDouble ? kOneD : kOneS;
            for (const NanInput &in : nans)
            {
                for (const char *op : {"fadd", "fsub", "fmul", "fdiv"})
                {
                    for (int nanSide : {1, 2})
                    {
                        const std::string body =
                            loadW(1, nanSide == 1 ? in.bits : one, isDouble, 5) +
                            loadW(2, nanSide == 1 ? one : in.bits, isDouble, 6) +
                            loadDouble(3, kSentinel, 7) + "    " + op + "." + w +
                            " f3, f1, f2\n" + kGap;
                        const Outcome r = assembleAndRun(model, dir, body);
                        ASSERT_TRUE(r.assembled) << r.complaint;
                        EXPECT_EQ(r.fpr(3), canonical(isDouble))
                            << op << "." << w << " with a " << in.what << " as rs" << nanSide
                            << ": got " << hex(r.fpr(3), 16) << ", expected "
                            << hex(canonical(isDouble), 16);
                    }
                }
                const std::string body = loadW(1, in.bits, isDouble, 5) +
                                         loadDouble(3, kSentinel, 7) + "    fsqrt." + w +
                                         " f3, f1\n" + kGap;
                const Outcome r = assembleAndRun(model, dir, body);
                ASSERT_TRUE(r.assembled) << r.complaint;
                EXPECT_EQ(r.fpr(3), canonical(isDouble))
                    << "fsqrt." << w << " of a " << in.what << ": got " << hex(r.fpr(3), 16);
            }
        }
    }
}

TEST(CanonicalNaN, FusedMultiplyAddProducesTheCanonicalNaN)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (bool isDouble : {false, true})
        {
            const auto &nans = isDouble ? kDoubleNaNs : kSingleNaNs;
            const char *w    = isDouble ? "d" : "s";
            const uint64_t one = isDouble ? kOneD : kOneS;
            for (const NanInput &in : nans)
            {
                for (const char *op : {"fmadd", "fmsub", "fnmsub", "fnmadd"})
                {
                    for (int nanSlot : {1, 3})
                    {
                        const std::string body =
                            loadW(1, nanSlot == 1 ? in.bits : one, isDouble, 5) +
                            loadW(2, one, isDouble, 6) +
                            loadW(3, nanSlot == 3 ? in.bits : one, isDouble, 7) +
                            loadDouble(4, kSentinel, 8) + "    " + op + "." + w +
                            " f4, f1, f2, f3\n" + kGap;
                        const Outcome r = assembleAndRun(model, dir, body);
                        ASSERT_TRUE(r.assembled) << r.complaint;
                        EXPECT_EQ(r.fpr(4), canonical(isDouble))
                            << op << "." << w << " with a " << in.what << " as rs" << nanSlot
                            << ": got " << hex(r.fpr(4), 16);
                    }
                }
            }
        }
    }
}

TEST(CanonicalNaN, TheInvalidOperationsProduceTheCanonicalNaN)
{
    // Measured before the fix: inf-inf, 0*inf, inf/inf and fma(inf,0,1)
    // all returned the x86 default NaN, which is NEGATIVE.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (bool isDouble : {false, true})
        {
            const char *w = isDouble ? "d" : "s";
            const uint64_t inf = isDouble ? kInfD : kInfS;
            const uint64_t one = isDouble ? kOneD : kOneS;
            const uint64_t minusOne = isDouble ? kMinusOneD : kMinusOneS;
            struct Case { const char *what, *op; uint64_t a, b; };
            const Case cases[] = {
                {"inf - inf", "fsub", inf, inf},
                {"0 * inf", "fmul", kZeroBits, inf},
                {"0 / 0", "fdiv", kZeroBits, kZeroBits},
                {"inf / inf", "fdiv", inf, inf},
            };
            for (const Case &c : cases)
            {
                const std::string body = loadW(1, c.a, isDouble, 5) + loadW(2, c.b, isDouble, 6) +
                                         loadDouble(3, kSentinel, 7) + "    " + c.op + "." + w +
                                         " f3, f1, f2\n" + kGap;
                const Outcome r = assembleAndRun(model, dir, body);
                ASSERT_TRUE(r.assembled) << r.complaint;
                EXPECT_EQ(r.fpr(3), canonical(isDouble))
                    << c.what << " (." << w << "): got " << hex(r.fpr(3), 16);
            }
            {
                const std::string body = loadW(1, minusOne, isDouble, 5) +
                                         loadDouble(3, kSentinel, 7) + "    fsqrt." + w +
                                         " f3, f1\n" + kGap;
                const Outcome r = assembleAndRun(model, dir, body);
                ASSERT_TRUE(r.assembled) << r.complaint;
                EXPECT_EQ(r.fpr(3), canonical(isDouble))
                    << "sqrt(-1) (." << w << "): got " << hex(r.fpr(3), 16);
            }
            {
                const std::string body = loadW(1, inf, isDouble, 5) + loadW(2, kZeroBits, isDouble, 6) +
                                         loadW(3, one, isDouble, 7) + loadDouble(4, kSentinel, 8) +
                                         "    fmadd." + w + " f4, f1, f2, f3\n" + kGap;
                const Outcome r = assembleAndRun(model, dir, body);
                ASSERT_TRUE(r.assembled) << r.complaint;
                EXPECT_EQ(r.fpr(4), canonical(isDouble))
                    << "fma(inf, 0, 1) (." << w << "): got " << hex(r.fpr(4), 16);
            }
        }
    }
}

TEST(CanonicalNaN, ConversionsBetweenWidthsProduceTheCanonicalNaN)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (const NanInput &in : kDoubleNaNs)
        {
            const std::string body = loadDouble(1, in.bits, 5) + loadDouble(3, kSentinel, 7) +
                                     "    fcvt.s.d f3, f1\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << r.complaint;
            EXPECT_EQ(r.fpr(3), kCanonicalNaN32)
                << "fcvt.s.d of a " << in.what << ": got " << hex(r.fpr(3), 16);
        }
        for (const NanInput &in : kSingleNaNs)
        {
            const std::string body = loadSingle(1, static_cast<uint32_t>(in.bits), 5) +
                                     loadDouble(3, kSentinel, 7) + "    fcvt.d.s f3, f1\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << r.complaint;
            EXPECT_EQ(r.fpr(3), kCanonicalNaN64)
                << "fcvt.d.s of a " << in.what << ": got " << hex(r.fpr(3), 16);
        }
    }
}

TEST(CanonicalNaN, MinAndMaxFollowTheOperandRule)
{
    // fmin/fmax are the exception to the propagation rule: with ONE NaN
    // operand the result is the other operand, not a NaN at all. Only
    // when both are NaN is the result the canonical NaN. The survey said
    // the first half already held, 32 of 32; the second half did not.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (bool isDouble : {false, true})
        {
            const auto &nans = isDouble ? kDoubleNaNs : kSingleNaNs;
            const char *w    = isDouble ? "d" : "s";
            const uint64_t one = isDouble ? kOneD : kOneS;
            for (const NanInput &in : nans)
            {
                for (const char *op : {"fmin", "fmax"})
                {
                    for (int nanSide : {1, 2})
                    {
                        const std::string body =
                            loadW(1, nanSide == 1 ? in.bits : one, isDouble, 5) +
                            loadW(2, nanSide == 1 ? one : in.bits, isDouble, 6) +
                            loadDouble(3, kSentinel, 7) + "    " + op + "." + w +
                            " f3, f1, f2\n" + kGap;
                        const Outcome r = assembleAndRun(model, dir, body);
                        ASSERT_TRUE(r.assembled) << r.complaint;
                        EXPECT_EQ(r.fpr(3), one)
                            << op << "." << w << " with a " << in.what << " as rs" << nanSide
                            << " must return the OTHER operand, got " << hex(r.fpr(3), 16);
                    }
                    const std::string body = loadW(1, in.bits, isDouble, 5) +
                                             loadW(2, in.bits, isDouble, 6) +
                                             loadDouble(3, kSentinel, 7) + "    " + op + "." + w +
                                             " f3, f1, f2\n" + kGap;
                    const Outcome r = assembleAndRun(model, dir, body);
                    ASSERT_TRUE(r.assembled) << r.complaint;
                    EXPECT_EQ(r.fpr(3), canonical(isDouble))
                        << op << "." << w << " with a " << in.what << " on BOTH sides: got "
                        << hex(r.fpr(3), 16);
                }
            }
        }
    }
}

TEST(CanonicalNaN, SignInjectionAndMovesArePassThrough)
{
    // The negative control. None of these is a NaN producer, so none of
    // them may be canonicalized: each is asserted against what its own
    // specification says, not against "the bits came out unchanged".
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (bool isDouble : {false, true})
        {
            const auto &nans = isDouble ? kDoubleNaNs : kSingleNaNs;
            const char *w    = isDouble ? "d" : "s";
            const uint64_t one  = isDouble ? kOneD : kOneS;   // sign bit clear
            const uint64_t sign = isDouble ? 0x8000000000000000ULL : 0x80000000ULL;
            const uint64_t rest = ~sign & (isDouble ? ~0ULL : 0xFFFFFFFFULL);
            for (const NanInput &in : nans)
            {
                struct Case { const char *op; uint64_t expected; };
                const Case cases[] = {
                    // rs2 is +1.0, so its sign is 0
                    {"fsgnj", in.bits & rest},                 // sign of rs2
                    {"fsgnjn", (in.bits & rest) | sign},       // sign of ~rs2
                    // rs1.sign XOR rs2.sign, and rs2 is +1.0, so the
                    // sign is rs1's own: the whole pattern survives.
                    {"fsgnjx", in.bits},
                };
                for (const Case &c : cases)
                {
                    const std::string body = loadW(1, in.bits, isDouble, 5) +
                                             loadW(2, one, isDouble, 6) +
                                             loadDouble(3, kSentinel, 7) + "    " + c.op + "." + w +
                                             " f3, f1, f2\n" + kGap;
                    const Outcome r = assembleAndRun(model, dir, body);
                    ASSERT_TRUE(r.assembled) << r.complaint;
                    EXPECT_EQ(r.fpr(3), c.expected)
                        << c.op << "." << w << " of a " << in.what
                        << " must keep the payload and take the specified sign: got "
                        << hex(r.fpr(3), 16) << ", expected " << hex(c.expected, 16);
                }
                // fmv.x.w sign-extends the 32-bit pattern into rd; fmv.x.d
                // moves all 64 bits. Both are specified that way.
                const char *mvx = isDouble ? "fmv.x.d" : "fmv.x.w";
                const uint64_t expectedGpr =
                    isDouble ? in.bits
                             : static_cast<uint64_t>(
                                   static_cast<int64_t>(static_cast<int32_t>(in.bits)));
                const std::string body = loadW(1, in.bits, isDouble, 5) + "    " + mvx +
                                         " x9, f1\n" + kGap;
                const Outcome r = assembleAndRun(model, dir, body);
                ASSERT_TRUE(r.assembled) << r.complaint;
                EXPECT_EQ(r.gpr(9), expectedGpr)
                    << mvx << " of a " << in.what << ": got " << hex(r.gpr(9), 16)
                    << ", expected " << hex(expectedGpr, 16);
            }
        }
    }
}

TEST(CanonicalNaN, IntegerResultsAreNotCanonicalized)
{
    // feq/flt/fle leave through the same `is_int_result` exit as
    // fcvt.s.d, and fcvt.l.s/d leave through an early return. None of
    // them may be touched: their results are integers that happen to
    // share a code path with a NaN producer.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (bool isDouble : {false, true})
        {
            const auto &nans = isDouble ? kDoubleNaNs : kSingleNaNs;
            const char *w    = isDouble ? "d" : "s";
            const uint64_t one = isDouble ? kOneD : kOneS;
            for (const NanInput &in : nans)
            {
                for (const char *op : {"feq", "flt", "fle"})
                {
                    const std::string body = loadW(1, in.bits, isDouble, 5) +
                                             loadW(2, one, isDouble, 6) +
                                             "    addi x9, x0, 7\n" + kGap + "    " + op + "." + w +
                                             " x9, f1, f2\n" + kGap;
                    const Outcome r = assembleAndRun(model, dir, body);
                    ASSERT_TRUE(r.assembled) << r.complaint;
                    EXPECT_EQ(r.gpr(9), 0u)
                        << op << "." << w << " against a " << in.what
                        << " must be 0 (an integer), got " << hex(r.gpr(9), 16);
                }
                // fcvt.l.s/d of a NaN saturates to the maximum, per the
                // spec's table - an integer, not a NaN.
                const std::string body = loadW(1, in.bits, isDouble, 5) + "    fcvt.l." + w +
                                         " x9, f1\n" + kGap;
                const Outcome r = assembleAndRun(model, dir, body);
                ASSERT_TRUE(r.assembled) << r.complaint;
                EXPECT_EQ(r.gpr(9), 0x7FFFFFFFFFFFFFFFULL)
                    << "fcvt.l." << w << " of a " << in.what << ": got " << hex(r.gpr(9), 16);
            }
        }
    }
}
