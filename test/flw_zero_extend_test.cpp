/**
 * @file flw_zero_extend_test.cpp
 * @brief `flw` puts a single in an f register zero-extended, on every model.
 *
 * The convention (known_issues 71): a single occupies the low 32 bits of
 * the 64-bit f register and the upper 32 are zero - not NaN-boxed. rvss
 * does that for `flw`; the four rv5s variants used to sign-extend the
 * loaded word instead (known_issues 74), so a word with bit 31 set - any
 * negative single, any negative NaN - came out as 0xFFFFFFFF_xxxxxxxx and
 * a program's result depended on which model a student picked.
 *
 * Observed through the whole register, not through another single-width
 * instruction (which would read only the low half and hide it): `fsd`
 * stores all 64 bits and `ld` reads them back into an x register.
 *
 * `fmv.x.w` is the other direction and is NOT the same rule: the
 * architecture defines it to sign-extend the single's 32 bits into the
 * integer register. That is pinned here too, so a fix to the f-register
 * side cannot drag the integer side along with it.
 *
 * Every dependent pair is padded with four nops (float_test_support.h),
 * so all five models owe the same answer. Expected values come from
 * Python's struct module, not from rvss.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using namespace Atlas;
using namespace Atlas::test_support;

namespace
{
/// The data section's default base (config.h). Loaded with loadImmediate,
/// not `la`: `la` expands to auipc, which the rv5s variants compute wrong
/// (a separate known_issues entry), and this file is about flw.
constexpr uint64_t kDataBase = 0x10000000ULL;

struct Word
{
    const char *what;
    uint32_t    bits;
    uint64_t    zeroExtended; ///< what flw must leave in the f register
    uint64_t    signExtended; ///< what fmv.x.w must leave in the x register
};

// struct.unpack('<I', struct.pack('<f', -1.0))[0] == 0xBF800000, and so on.
const Word kWords[] = {
    {"-1.0", 0xBF800000u, 0x00000000BF800000ULL, 0xFFFFFFFFBF800000ULL},
    {"-2.5", 0xC0200000u, 0x00000000C0200000ULL, 0xFFFFFFFFC0200000ULL},
    {"negative qNaN", 0xFFC00000u, 0x00000000FFC00000ULL, 0xFFFFFFFFFFC00000ULL},
};

/// sw the word, flw it, fsd the f register, ld it back, and fmv.x.w the
/// loaded single into an x register.
std::string flwRoundTrip(uint32_t bits)
{
    return loadImmediate(10, kDataBase) + loadImmediate(5, bits) +
           "    sw x5, 0(x10)\n" + kGap +
           "    flw f2, 0(x10)\n" + kGap +
           "    fsd f2, 8(x10)\n" + kGap +
           "    ld x6, 8(x10)\n" + kGap +
           "    fmv.x.w x8, f2\n" + kGap;
}
} // namespace

TEST(FlwZeroExtend, LoadedSingleIsZeroExtendedOnEveryModel)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const Word &word : kWords)
    {
        SCOPED_TRACE(word.what);
        for (Model model : allModels())
        {
            const Outcome r = assembleAndRun(model, dir, flwRoundTrip(word.bits));
            ASSERT_TRUE(r.assembled) << name(model) << ": " << r.complaint;
            EXPECT_EQ(r.fpr(2), word.zeroExtended)
                << name(model) << ": f2 after flw = " << hex(r.fpr(2), 16);
            EXPECT_EQ(r.gpr(6), word.zeroExtended)
                << name(model) << ": fsd/ld of f2 = " << hex(r.gpr(6), 16);
        }
    }
}

TEST(FlwZeroExtend, FmvXWStillSignExtendsIntoTheIntegerRegister)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const Word &word : kWords)
    {
        SCOPED_TRACE(word.what);
        for (Model model : allModels())
        {
            // From a register written by fmv.w.x ...
            const Outcome moved = assembleAndRun(
                model, dir, loadSingle(3, word.bits) + "    fmv.x.w x7, f3\n" + kGap);
            ASSERT_TRUE(moved.assembled) << name(model) << ": " << moved.complaint;
            EXPECT_EQ(moved.gpr(7), word.signExtended)
                << name(model) << ": fmv.x.w after fmv.w.x = " << hex(moved.gpr(7), 16);

            // ... and from one written by flw.
            const Outcome loaded = assembleAndRun(model, dir, flwRoundTrip(word.bits));
            ASSERT_TRUE(loaded.assembled) << name(model) << ": " << loaded.complaint;
            EXPECT_EQ(loaded.gpr(8), word.signExtended)
                << name(model) << ": fmv.x.w after flw = " << hex(loaded.gpr(8), 16);
        }
    }
}
