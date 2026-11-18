/**
 * @file li_forwarding_nh_f_test.cpp
 * @brief A multi-instruction `li` is right on rv5-nh-f, and so is its
 *        consumer back to back; the pair nh_f really cannot run is a load
 *        followed by its use.
 *
 * known_issues 77 recorded `li x5, 0x3FF0000000000000` as leaving x5 = 0
 * on rv5-nh-f. Re-measured at HEAD, at 424b41d (where 77 was written) and
 * at 9ce7f61 (before the 76 fix): x5 was right on nh_f at all three. The
 * expansion is an ALU chain - addi/lui, then slli and addi - and every
 * link is an ALU producer at distance 0, which nh_f's EX/MEM forwarding
 * covers (rv5s_processor_nh_f.cpp, the forward_a/forward_b block). What
 * WAS wrong before 76 was an FP consumer (`fmv.d.x f1, x5`) reading x5
 * through the register file - the 76 defect, fixed in 6912b0c.
 *
 * nh_f has forwarding but no hazard detection: the one dependency it
 * cannot satisfy is a LOAD feeding the very next instruction, because the
 * loaded value does not exist until the end of MEM. One nop after the
 * load is enough. That distance-0 case is not asserted here - it is wrong
 * by design, not by defect.
 *
 * rv5-nh-nf is left out: it forwards nothing, and `li` cannot be padded
 * from the source because the assembler emits the chain itself.
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
const std::vector<Model> kForwardingOrInterlocked = {Model::Rvss, Model::Rv5NhF, Model::Rv5HNf,
                                                     Model::Rv5HF};

/// The values, as two's-complement 64-bit patterns. Each needs more than
/// one instruction (Python: not -2048 <= v <= 2047 as a signed 64-bit int).
const uint64_t kConstants[] = {
    0x3FF0000000000000ULL, // the value 77 recorded: addi + slli
    0x123456789ABCDEF0ULL, // lui/addi, then slli+addi steps
    0xFFFFFFFEDCBA9877ULL, // -0x123456789
    0x7FFFFFFF00000001ULL,
    0x0000000012345000ULL, // a bare lui
};
} // namespace

TEST(LiForwardingNhF, MultiInstructionLiAndItsConsumerBackToBack)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (uint64_t value : kConstants)
    {
        SCOPED_TRACE(hex(value, 16));
        const std::string body = "    li x5, " + std::to_string(static_cast<int64_t>(value)) +
                                 "\n    addi x6, x5, 0\n" + kGap;
        for (Model model : kForwardingOrInterlocked)
        {
            const Outcome r = assembleAndRun(model, dir, body);
            ASSERT_TRUE(r.assembled) << name(model) << ": " << r.complaint;
            EXPECT_EQ(r.gpr(5), value) << name(model) << ": x5 = " << hex(r.gpr(5), 16);
            EXPECT_EQ(r.gpr(6), value) << name(model) << ": x6 = " << hex(r.gpr(6), 16);
        }
    }
}

TEST(LiForwardingNhF, LoadUseNeedsOneNopOnNhF)
{
    // One nop between the load and its use: nh_f forwards it from MEM/WB.
    // Without the nop it is wrong by design (no hazard detection) and is
    // not asserted.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string body = std::string("    li x10, 0x10000000\n") + kGap +
                             "    li x7, 1234\n" + kGap + "    sd x7, 0(x10)\n" + kGap +
                             "    ld x5, 0(x10)\n    nop\n    addi x6, x5, 1\n" + kGap;
    for (Model model : kForwardingOrInterlocked)
    {
        const Outcome r = assembleAndRun(model, dir, body);
        ASSERT_TRUE(r.assembled) << name(model) << ": " << r.complaint;
        EXPECT_EQ(r.gpr(6), 1235u) << name(model) << ": x6 = " << hex(r.gpr(6), 16);
    }
}
