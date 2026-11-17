/**
 * @file float_completeness_test.cpp
 * @brief known_issues 57, 60 and 61: the eleven float instructions that
 *        assemble, run, and are wrong.
 *
 * Every expected bit pattern here was computed OUTSIDE the code under
 * test, in Python: `struct.pack` for the conversions, `math.fma` (3.14)
 * for the fused cases, and exact `Fraction` arithmetic where a tie had to
 * be constructed deliberately.
 *
 * The path under test is the real one. Each case is assembled from
 * source, loaded into a real processor, and run - on `RVSSProcessor` and
 * on all four rv5s variants, because the defects live in shared code
 * (`alu.cpp`, `instructions.cpp`) and in per-model control units.
 *
 * Two conventions this file follows rather than invents:
 *
 *   - A single-precision value occupies the LOW 32 BITS of the 64-bit f
 *     register, zero-extended. That is what `Alu::fpexecute` returns
 *     (`alu.cpp`, `static_cast<uint64_t>(result_bits)`) and what it reads
 *     (`memcpy(&a, &ina, sizeof(float))`). Nothing in this simulator
 *     NaN-boxes.
 *   - Dependent instructions are separated by four `nop`s. The nh_nf and
 *     nh_f variants have no hazard detection, so a back-to-back
 *     dependency would read a stale register and the test would be
 *     measuring the pipeline rather than the arithmetic.
 *
 * NaN-producing cases assert quiet-NaN-ness rather than an exact payload:
 * IEEE 754 does not pin the payload of a converted or invalid-operation
 * NaN, and here it comes from whatever the host's SSE does.
 */
#include "assembler/assembler.h"
#include "processor/processor_base.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
enum class Model
{
    Rvss,
    Rv5NhNf,
    Rv5NhF,
    Rv5HNf,
    Rv5HF
};

const std::vector<Model> &allModels()
{
    static const std::vector<Model> models{Model::Rvss, Model::Rv5NhNf, Model::Rv5NhF,
                                           Model::Rv5HNf, Model::Rv5HF};
    return models;
}

const char *name(Model model)
{
    switch (model)
    {
    case Model::Rvss:    return "rvss";
    case Model::Rv5NhNf: return "rv5-nh-nf";
    case Model::Rv5NhF:  return "rv5-nh-f";
    case Model::Rv5HNf:  return "rv5-h-nf";
    case Model::Rv5HF:   return "rv5-h-f";
    }
    return "?";
}

std::unique_ptr<ProcessorBase> makeProcessor(Model model)
{
    switch (model)
    {
    case Model::Rvss:    return std::make_unique<RVSSProcessor>();
    case Model::Rv5NhNf: return std::make_unique<RV5StageProcessorNHNF>();
    case Model::Rv5NhF:  return std::make_unique<RV5StageProcessorNHF>();
    case Model::Rv5HNf:  return std::make_unique<RV5StageProcessorHNF>();
    case Model::Rv5HF:   return std::make_unique<RV5StageProcessorHF>();
    }
    return nullptr;
}

std::string hex(uint64_t value, int digits)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%0*llX", digits, (unsigned long long)value);
    return buffer;
}

/// Four nops: enough for a result to reach writeback before the next
/// instruction decodes, on the variants with no hazard detection.
const char *kGap = "    nop\n    nop\n    nop\n    nop\n";

/**
 * @brief Puts an exact 64-bit pattern in a GPR, one byte at a time.
 *
 * NOT `li`. The assembler expands a 64-bit `li` into a chain of
 * lui/slli/addi with no gaps between them, and the nh_nf and nh_f
 * variants have no hazard detection - measured: `li x5,
 * 0x3FF0000000000000` leaves x5 = 0 on nh-nf while rvss and h-f get it
 * right. That is the variants behaving as designed, not a defect, but it
 * makes `li` useless as a test fixture. This emits the same work with
 * four nops between every dependent pair, so all five models agree.
 */
std::string loadImmediate(int gpr, uint64_t bits)
{
    std::ostringstream out;
    out << "    addi x" << gpr << ", x0, " << ((bits >> 56) & 0xFF) << "\n" << kGap;
    for (int shift = 48; shift >= 0; shift -= 8)
    {
        out << "    slli x" << gpr << ", x" << gpr << ", 8\n"
            << kGap << "    addi x" << gpr << ", x" << gpr << ", "
            << ((bits >> shift) & 0xFF) << "\n"
            << kGap;
    }
    return out.str();
}

/// The whole 64-bit pattern into an f register, exactly.
std::string loadDouble(int fpr, uint64_t bits, int scratch = 5)
{
    std::ostringstream out;
    out << loadImmediate(scratch, bits) << "    fmv.d.x f" << fpr << ", x" << scratch << "\n"
        << kGap;
    return out.str();
}

/// The same for a single: the low 32 bits, zero-extended by fmv.w.x.
std::string loadSingle(int fpr, uint32_t bits, int scratch = 5)
{
    std::ostringstream out;
    out << loadImmediate(scratch, bits) << "    fmv.w.x f" << fpr << ", x" << scratch << "\n"
        << kGap;
    return out.str();
}

struct Outcome
{
    std::unique_ptr<ProcessorBase> processor;
    bool                           assembled = false;
    std::string                    complaint;
    uint64_t fpr(int index) const { return processor->registers_.ReadFpr(index); }
    uint64_t gpr(int index) const { return processor->registers_.ReadGpr(index); }
};

int fileSerial = 0;

/// Assembles `body` as a whole program and runs it to completion.
Outcome assembleAndRun(Model model, const QTemporaryDir &dir, const std::string &body)
{
    Outcome out;
    const std::string path =
        dir.path().toStdString() + "/fc" + std::to_string(fileSerial++) + ".s";
    {
        std::ofstream file(path);
        file << ".text\n" << body;
    }
    setupVmStateDirectory();
    out.processor = makeProcessor(model);
    try
    {
        out.processor->LoadProgram(assemble(path));
    }
    catch (const std::exception &error)
    {
        out.complaint = error.what();
        return out;
    }
    out.assembled = true;
    out.processor->DebugRun();
    return out;
}

bool isQuietNaN64(uint64_t bits)
{
    return ((bits >> 52) & 0x7FF) == 0x7FF && (bits & 0xFFFFFFFFFFFFFULL) != 0 &&
           (bits & (1ULL << 51)) != 0;
}
bool isQuietNaN32(uint64_t bits)
{
    const uint32_t word = static_cast<uint32_t>(bits);
    return ((word >> 23) & 0xFF) == 0xFF && (word & 0x7FFFFF) != 0 && (word & (1u << 22)) != 0;
}

/// A sentinel in the destination, so "wrote zero" and "did not write"
/// stay distinguishable - the distinction known_issues 57 and 61 turn on.
constexpr uint64_t kSentinel = 0x40934800ABCDEF12ULL;
} // namespace

// ───────────────────── the harness itself ──────────────────────────────

TEST(FloatCompleteness, TheBitPatternLoaderIsExact)
{
    // Everything below depends on li + fmv.d.x / fmv.w.x putting an exact
    // pattern in an f register, including values with the top bit set.
    // If this fails, no other failure in this file means anything.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const uint64_t doubles[] = {0x0000000000000000ULL, 0x3FF0000000000000ULL,
                                0x8000000000000000ULL, 0x7FF0000000000000ULL,
                                0x43E0000000000000ULL, 0xFFFFFFFFFFFFFFFFULL, kSentinel};
    const uint32_t singles[] = {0x00000000u, 0x3F800000u, 0x80000000u, 0x7F800000u, 0xFFFFFFFFu};
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (uint64_t bits : doubles)
        {
            const Outcome r = assembleAndRun(model, dir, loadDouble(1, bits));
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(1), bits) << "fmv.d.x of " << hex(bits, 16);
        }
        for (uint32_t bits : singles)
        {
            const Outcome r = assembleAndRun(model, dir, loadSingle(1, bits));
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(1), uint64_t(bits))
                << "fmv.w.x of " << hex(bits, 8) << " must be zero-extended, not sign-extended";
        }
    }
}

// ───────────────────── known_issues 60: fcvt.d.lu ──────────────────────

TEST(FloatCompleteness, FcvtDLuConvertsUnsignedNotSigned)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    struct Case
    {
        const char *what;
        uint64_t    source;
        uint64_t    expected;
    };
    // 2^63 and 2^64-1 are the rows that distinguish fcvt.d.lu from
    // fcvt.d.l: signed would give 0xC3E0... and 0xBFF0... respectively.
    const Case cases[] = {
        {"0", 0ULL, 0x0000000000000000ULL},
        {"1", 1ULL, 0x3FF0000000000000ULL},
        {"965", 965ULL, 0x408E280000000000ULL},
        {"2^63", 0x8000000000000000ULL, 0x43E0000000000000ULL},
        {"2^64-1", 0xFFFFFFFFFFFFFFFFULL, 0x43F0000000000000ULL},
        {"2^53+1 (rounds)", 9007199254740993ULL, 0x4340000000000000ULL},
    };
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (const Case &c : cases)
        {
            std::ostringstream body;
            body << loadImmediate(6, c.source) << loadDouble(1, kSentinel)
                 << "    fcvt.d.lu f1, x6\n"
                 << kGap;
            const Outcome r = assembleAndRun(model, dir, body.str());
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(1), c.expected)
                << "fcvt.d.lu of " << c.what << ": got " << hex(r.fpr(1), 16) << ", expected "
                << hex(c.expected, 16)
                << (r.fpr(1) == 0 ? "   (0 means the ALU had no case for it)" : "");
        }
    }
}

// ───────────────────── known_issues 57: fcvt.s.d ───────────────────────

TEST(FloatCompleteness, FcvtSDRoundsOnceToSingle)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    struct Case
    {
        const char *what;
        uint64_t    source;   // the double
        uint32_t    expected; // the single, zero-extended into the f register
    };
    const Case cases[] = {
        {"1.5, exact", 0x3FF8000000000000ULL, 0x3FC00000u},
        {"0.1, needs rounding", 0x3FB999999999999AULL, 0x3DCCCCCDu},
        {"tie, rounds to even (down)", 0x3FF0000010000000ULL, 0x3F800000u},
        {"tie, rounds to even (up)", 0x3FF0000030000000ULL, 0x3F800002u},
        {"1e300, overflows to +inf", 0x7E37E43C8800759CULL, 0x7F800000u},
        {"-1e300, overflows to -inf", 0xFE37E43C8800759CULL, 0xFF800000u},
        {"1e-40, underflows to a subnormal", 0x37A16C262777579CULL, 0x000116C2u},
        {"1e-60, underflows to +0", 0x3379B604AAACA626ULL, 0x00000000u},
        {"-0.0 keeps its sign", 0x8000000000000000ULL, 0x80000000u},
        {"+inf", 0x7FF0000000000000ULL, 0x7F800000u},
        {"-inf", 0xFFF0000000000000ULL, 0xFF800000u},
    };
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (const Case &c : cases)
        {
            const std::string body =
                loadDouble(1, c.source) + loadDouble(2, kSentinel, 6) + "    fcvt.s.d f2, f1\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(2), uint64_t(c.expected))
                << "fcvt.s.d " << c.what << ": got " << hex(r.fpr(2), 16) << ", expected "
                << hex(c.expected, 8)
                << (r.fpr(2) == 0 && c.expected != 0
                        ? "   (0 means the control unit never mapped it)"
                        : "");
        }
        // NaN in, quiet NaN out - the payload is not pinned.
        const std::string body =
            loadDouble(1, 0x7FF8000000000000ULL) + loadDouble(2, kSentinel, 6) +
            "    fcvt.s.d f2, f1\n" + kGap;
        const Outcome r = assembleAndRun(model, dir, body);
        ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
        EXPECT_TRUE(isQuietNaN32(r.fpr(2)))
            << "fcvt.s.d of a NaN gave " << hex(r.fpr(2), 16) << ", which is not a quiet NaN";
    }
}

// ───────────────────── known_issues 57: fcvt.d.s ───────────────────────

TEST(FloatCompleteness, FcvtDSWidensTheStoredSingle)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    struct Case
    {
        const char *what;
        uint32_t    source;   // the single, in the low 32 bits
        uint64_t    expected; // the double
    };
    const Case cases[] = {
        {"1.5, exact", 0x3FC00000u, 0x3FF8000000000000ULL},
        {"0.1f widens exactly", 0x3DCCCCCDu, 0x3FB99999A0000000ULL},
        {"smallest subnormal", 0x00000001u, 0x36A0000000000000ULL},
        {"largest subnormal", 0x007FFFFFu, 0x380FFFFFC0000000ULL},
        {"+inf", 0x7F800000u, 0x7FF0000000000000ULL},
        {"-0.0f keeps its sign", 0x80000000u, 0x8000000000000000ULL},
    };
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (const Case &c : cases)
        {
            const std::string body =
                loadSingle(1, c.source) + loadDouble(2, kSentinel, 6) + "    fcvt.d.s f2, f1\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(2), c.expected)
                << "fcvt.d.s " << c.what << ": got " << hex(r.fpr(2), 16) << ", expected "
                << hex(c.expected, 16)
                << (r.fpr(2) == 0 && c.expected != 0
                        ? "   (0 means the control unit never mapped it)"
                        : "");
        }
        const std::string body = loadSingle(1, 0x7FC00000u) + loadDouble(2, kSentinel, 6) +
                                 "    fcvt.d.s f2, f1\n" + kGap;
        const Outcome r = assembleAndRun(model, dir, body);
        ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
        EXPECT_TRUE(isQuietNaN64(r.fpr(2)))
            << "fcvt.d.s of a NaN gave " << hex(r.fpr(2), 16) << ", which is not a quiet NaN";
    }
}

// ───────────────────── known_issues 61: the eight FMAs ─────────────────

TEST(FloatCompleteness, FusedMultiplyAddRoundsOnce)
{
    // The whole point of a fused multiply-add: ONE rounding, of the exact
    // product-plus-addend. a*b is exactly 1 - 2^-104, which a double
    // cannot hold; rounded first it becomes 1.0 and the sum is +0, while
    // the fused result is -2^-104 * 2^52 ... = -4.93e-32.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        {
            const std::string body = loadDouble(1, 0x3FF0000000000001ULL) +
                                     loadDouble(2, 0x3FEFFFFFFFFFFFFEULL, 6) +
                                     loadDouble(3, 0xBFF0000000000000ULL, 7) +
                                     loadDouble(4, kSentinel, 8) + "    fmadd.d f4, f1, f2, f3\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(4), 0xB970000000000000ULL)
                << "fmadd.d fused: got " << hex(r.fpr(4), 16)
                << (r.fpr(4) == kSentinel ? "   (the sentinel: nothing was written)"
                    : r.fpr(4) == 0      ? "   (zero: multiplied then added, two roundings)"
                                         : "");
        }
        {
            const std::string body = loadSingle(1, 0x3F800001u) + loadSingle(2, 0x3F7FFFFEu, 6) +
                                     loadSingle(3, 0xBF800000u, 7) + loadDouble(4, kSentinel, 8) +
                                     "    fmadd.s f4, f1, f2, f3\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(4), 0xA8800000ULL)
                << "fmadd.s fused: got " << hex(r.fpr(4), 16)
                << (r.fpr(4) == kSentinel ? "   (the sentinel: nothing was written)"
                    : r.fpr(4) == 0      ? "   (zero: two roundings)"
                                         : "");
        }
    }
}

TEST(FloatCompleteness, TheFourFusedFormsHaveTheRightSigns)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // a=3, b=5, c=7:  fmadd a*b+c = 22, fmsub a*b-c = 8,
    //                 fnmsub -(a*b)+c = -8, fnmadd -(a*b)-c = -22.
    struct Case
    {
        const char *mnemonic;
        uint64_t    expected;
    };
    const Case doubles[] = {{"fmadd.d", 0x4036000000000000ULL},
                            {"fmsub.d", 0x4020000000000000ULL},
                            {"fnmsub.d", 0xC020000000000000ULL},
                            {"fnmadd.d", 0xC036000000000000ULL}};
    const Case singles[] = {{"fmadd.s", 0x41B00000ULL},
                            {"fmsub.s", 0x41000000ULL},
                            {"fnmsub.s", 0xC1000000ULL},
                            {"fnmadd.s", 0xC1B00000ULL}};
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (const Case &c : doubles)
        {
            const std::string body = loadDouble(1, 0x4008000000000000ULL) +
                                     loadDouble(2, 0x4014000000000000ULL, 6) +
                                     loadDouble(3, 0x401C000000000000ULL, 7) +
                                     loadDouble(4, kSentinel, 8) + "    " + c.mnemonic +
                                     " f4, f1, f2, f3\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(4), c.expected)
                << c.mnemonic << " 3,5,7: got " << hex(r.fpr(4), 16) << ", expected "
                << hex(c.expected, 16)
                << (r.fpr(4) == kSentinel ? "   (the sentinel: nothing was written)" : "");
        }
        for (const Case &c : singles)
        {
            const std::string body = loadSingle(1, 0x40400000u) + loadSingle(2, 0x40A00000u, 6) +
                                     loadSingle(3, 0x40E00000u, 7) + loadDouble(4, kSentinel, 8) +
                                     "    " + c.mnemonic + " f4, f1, f2, f3\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(4), c.expected)
                << c.mnemonic << " 3,5,7: got " << hex(r.fpr(4), 16) << ", expected "
                << hex(c.expected, 16)
                << (r.fpr(4) == kSentinel ? "   (the sentinel: nothing was written)" : "");
        }
        // Signed zero, and an invalid operation.
        {
            const std::string body = loadDouble(1, 0xBFF0000000000000ULL) +
                                     loadDouble(2, 0x0000000000000000ULL, 6) +
                                     loadDouble(3, 0x8000000000000000ULL, 7) +
                                     loadDouble(4, kSentinel, 8) + "    fmadd.d f4, f1, f2, f3\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(4), 0x8000000000000000ULL)
                << "fmadd.d(-1.0, +0.0, -0.0) must be -0.0, got " << hex(r.fpr(4), 16);
        }
        {
            const std::string body = loadDouble(1, 0x7FF0000000000000ULL) +
                                     loadDouble(2, 0x0000000000000000ULL, 6) +
                                     loadDouble(3, 0x3FF0000000000000ULL, 7) +
                                     loadDouble(4, kSentinel, 8) + "    fmadd.d f4, f1, f2, f3\n" + kGap;
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_TRUE(isQuietNaN64(r.fpr(4)))
                << "fmadd.d(inf, 0, 1) must be a quiet NaN, got " << hex(r.fpr(4), 16);
        }
    }
}

TEST(FloatCompleteness, FusedMultiplyAddDoesNotTouchIntegerRegisters)
{
    // An FMA's bits 31:25 are rs3<<2 | fmt, NOT a funct7. With rs3 = f20
    // and fmt = .s they read 0b1010000, which is the pattern writeback
    // uses to mean "this result goes to a GPR" (feq.s / fcvt.w.s). rs3 =
    // 24 and 28 collide the same way, and .d collides with the ...0001
    // row. The destination must still be the f register, and x[rd] must
    // be untouched.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    struct Case
    {
        const char *mnemonic;
        int         rs3;      // 20, 24, 28 are the colliding indices
        bool        isDouble;
        uint64_t    expected; // 3*5+7 = 22
    };
    const Case cases[] = {
        {"fmadd.s", 20, false, 0x41B00000ULL}, {"fmadd.s", 24, false, 0x41B00000ULL},
        {"fmadd.s", 28, false, 0x41B00000ULL}, {"fmadd.d", 20, true, 0x4036000000000000ULL},
        {"fmadd.d", 24, true, 0x4036000000000000ULL},
        {"fmadd.d", 28, true, 0x4036000000000000ULL},
    };
    for (Model model : allModels())
    {
        SCOPED_TRACE(name(model));
        for (const Case &c : cases)
        {
            std::ostringstream body;
            if (c.isDouble)
                body << loadDouble(1, 0x4008000000000000ULL) << loadDouble(2, 0x4014000000000000ULL, 6)
                     << loadDouble(c.rs3, 0x401C000000000000ULL, 7);
            else
                body << loadSingle(1, 0x40400000u) << loadSingle(2, 0x40A00000u, 6)
                     << loadSingle(c.rs3, 0x40E00000u, 7);
            body << loadDouble(4, kSentinel, 8) << loadImmediate(4, 1234567)
                 << "    " << c.mnemonic << " f4, f1, f2, f" << c.rs3 << "\n"
                 << kGap;
            const Outcome r = assembleAndRun(model, dir, body.str());
            ASSERT_TRUE(r.assembled) << "did not assemble: " << r.complaint;
            EXPECT_EQ(r.fpr(4), c.expected)
                << c.mnemonic << " with rs3=f" << c.rs3 << ": got " << hex(r.fpr(4), 16);
            EXPECT_EQ(r.gpr(4), 1234567u)
                << c.mnemonic << " with rs3=f" << c.rs3
                << " wrote the INTEGER register x4 (" << hex(r.gpr(4), 16)
                << "): its rs3<<2|fmt field was mistaken for a funct7";
        }
    }
}
