/**
 * @file fp_int_source_hazard_test.cpp
 * @brief An FP instruction whose SOURCE is an integer register must see a
 *        value produced by the instructions just before it.
 *
 * `fcvt.{s,d}.{w,wu,l,lu}`, `fmv.{w,d}.x` and the base register of
 * `flw`/`fld`/`fsw`/`fsd` all read an x register. On the forwarding
 * variants that read was taken straight from the architectural register
 * file in the execute stage, after the forwarding mux and in spite of
 * it, so a producer still in flight was not seen:
 *
 *     li t0, 3
 *     fcvt.d.l f1, t0        -> f1 = 0 on rv5-h-f and rv5-nh-f
 *
 * Each case is a producer, then `distance` nops, then the consumer.
 * Distance 0 is back to back, 3 is far enough that every model is clear
 * of the hazard whatever it does.
 *
 * ── what each model owes ───────────────────────────────────────────────
 *
 *   rvss     no pipeline, always right.
 *   h_nf     hazard detection, no forwarding: it stalls, always right.
 *   h_f      hazard detection and forwarding: right, and the subject of
 *            this file.
 *   nh_f     forwarding but no hazard detection: right EXCEPT when the
 *            producer is a load, because the loaded value does not exist
 *            until MEM and no forwarding path can conjure it. Those
 *            cases assert nothing for nh_f - they are not a defect, they
 *            are what "no hazard detection" means.
 *   nh_nf    neither: only the padded distance is asserted.
 *
 * Expected bit patterns were computed in Python, not read back from
 * rvss; every case additionally asserts that the models that owe the
 * reference agree with each other.
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
/// 3.0, 6.0, 7.0, 22.0 and 12.0 in both widths - Python, struct.pack.
constexpr uint64_t kThreeD = 0x4008000000000000ULL, kThreeS = 0x40400000ULL;
constexpr uint64_t kSixD   = 0x4018000000000000ULL;
constexpr uint64_t kSevenD = 0x401C000000000000ULL;
constexpr uint64_t kTwentyTwoD = 0x4036000000000000ULL, kTwentyTwoS = 0x41B00000ULL;
constexpr uint64_t kTwelveD = 0x4028000000000000ULL;

/// Which models owe the reference for a given case.
enum class Owe
{
    All,          ///< every model but nh_nf, which is asserted padded only
    NotNhFOnLoad, ///< nh_f excused: the producer is a load
};

std::string nops(int count)
{
    std::string out;
    for (int i = 0; i < count; ++i)
        out += "    nop\n";
    return out;
}

/// A producer, `distance` nops, a consumer, then four nops to drain.
std::string program(const std::string &setup, const std::string &producer, int distance,
                    const std::string &consumer)
{
    return setup + "    " + producer + "\n" + nops(distance) + "    " + consumer + "\n" + kGap;
}

struct Reading
{
    uint64_t value;
    bool     ok;
};

Reading readOne(Model model, const QTemporaryDir &dir, const std::string &body, bool gpr, int index)
{
    const Outcome r = assembleAndRun(model, dir, body);
    if (!r.assembled)
        return {0, false};
    return {gpr ? r.gpr(index) : r.fpr(index), true};
}

/// Runs one case on all five models and applies each model's contract.
void checkCase(const QTemporaryDir &dir, const std::string &what, const std::string &body,
               bool gpr, int index, uint64_t expected, Owe owe, int distance)
{
    SCOPED_TRACE(what + "  (producer distance " + std::to_string(distance) + ")");
    std::vector<uint64_t> agreeing;
    for (Model model : allModels())
    {
        const Reading got = readOne(model, dir, body, gpr, index);
        ASSERT_TRUE(got.ok) << name(model) << ": the program did not assemble";

        const bool excusedLoad = (owe == Owe::NotNhFOnLoad && model == Model::Rv5NhF);
        const bool noHazardUnit = (model == Model::Rv5NhNf);

        if (noHazardUnit || excusedLoad)
        {
            // Asserted only where the distance makes the hazard moot.
            // nh_nf has neither detection nor forwarding, and nh_f cannot
            // forward a value a load has not produced yet.
            if (distance >= 3)
                EXPECT_EQ(got.value, expected)
                    << name(model) << " at distance 3 must be clear of the hazard: got "
                    << hex(got.value, 16);
            continue;
        }
        EXPECT_EQ(got.value, expected)
            << name(model) << ": got " << hex(got.value, 16) << ", expected "
            << hex(expected, 16);
        agreeing.push_back(got.value);
    }
    for (std::size_t i = 1; i < agreeing.size(); ++i)
        EXPECT_EQ(agreeing[i], agreeing[0])
            << "the models that owe the reference disagree with each other";
}

const int kDistances[] = {0, 1, 2, 3};
} // namespace

// ───────── (a) an integer producer feeding an FP integer-source op ─────

TEST(FpIntSourceHazard, IntegerProducerIntoConversionsAndMoves)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    struct Case
    {
        const char *consumer;
        uint64_t    expected;
    };
    const Case cases[] = {
        {"fcvt.d.l f1, t0", kThreeD},
        {"fcvt.s.w f1, t0", kThreeS},
        {"fcvt.d.wu f1, t0", kThreeD},
        {"fmv.d.x f1, t0", 3},
        {"fmv.w.x f1, t0", 3},
    };
    for (const Case &c : cases)
        for (int d : kDistances)
            checkCase(dir, std::string("addi t0,x0,3 -> ") + c.consumer,
                      program("", "addi t0, x0, 3", d, c.consumer), false, 1, c.expected,
                      Owe::All, d);
}

// ───────── (b) the base register of a float load or store ──────────────

TEST(FpIntSourceHazard, IntegerProducerIntoALoadOrStoreBase)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // f1 = 3.0, parked well before anything under test.
    const std::string setup = loadDouble(1, kThreeD, 5) + loadSingle(2, uint32_t(kThreeS), 6);

    for (int d : kDistances)
    {
        // fsd's base: store through a just-computed address, then read it
        // back through a settled one.
        checkCase(dir, "addi a0,x0,1024 -> fsd f1, 0(a0)",
                  setup + "    addi a0, x0, 1024\n" + nops(d) + "    fsd f1, 0(a0)\n" + kGap +
                      "    addi a1, x0, 1024\n" + kGap + "    fld f3, 0(a1)\n" + kGap,
                  false, 3, kThreeD, Owe::All, d);
        // fld's base: store through a settled address, read through a
        // just-computed one.
        checkCase(dir, "addi a1,x0,1088 -> fld f3, 0(a1)",
                  setup + "    addi a0, x0, 1088\n" + kGap + "    fsd f1, 0(a0)\n" + kGap +
                      "    addi a1, x0, 1088\n" + nops(d) + "    fld f3, 0(a1)\n" + kGap,
                  false, 3, kThreeD, Owe::All, d);
        checkCase(dir, "addi a1,x0,1152 -> flw f3, 0(a1)",
                  setup + "    addi a0, x0, 1152\n" + kGap + "    fsw f2, 0(a0)\n" + kGap +
                      "    addi a1, x0, 1152\n" + nops(d) + "    flw f3, 0(a1)\n" + kGap,
                  false, 3, kThreeS, Owe::All, d);
    }
}

// ───────── (c) the producer is a load ──────────────────────────────────

TEST(FpIntSourceHazard, LoadProducerIntoAnIntegerSource)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // Put 3 in memory at 1216, then load it and convert.
    const std::string setup = "    addi a2, x0, 1216\n" + kGap + "    addi t1, x0, 3\n" + kGap +
                              "    sd t1, 0(a2)\n" + kGap;
    for (int d : kDistances)
    {
        checkCase(dir, "ld t0,0(a2) -> fcvt.d.l f1, t0",
                  setup + "    ld t0, 0(a2)\n" + nops(d) + "    fcvt.d.l f1, t0\n" + kGap, false,
                  1, kThreeD, Owe::NotNhFOnLoad, d);
        // a load producing the BASE of a float load
        checkCase(dir, "ld a3,0(a4) -> fld f3, 0(a3)",
                  loadDouble(1, kThreeD, 5) + "    addi a4, x0, 1280\n" + kGap +
                      "    addi t2, x0, 1344\n" + kGap + "    sd t2, 0(a4)\n" + kGap +
                      "    fsd f1, 0(t2)\n" + kGap + "    ld a3, 0(a4)\n" + nops(d) +
                      "    fld f3, 0(a3)\n" + kGap,
                  false, 3, kThreeD, Owe::NotNhFOnLoad, d);
    }
}

// ───────── (d) the producer is an FP op that writes a GPR ──────────────

TEST(FpIntSourceHazard, FpProducerWritingAGprIntoAnIntegerSource)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string setup = loadDouble(1, kThreeD, 5);
    for (int d : kDistances)
    {
        checkCase(dir, "fcvt.l.d t0,f1 -> fcvt.d.l f2, t0",
                  setup + "    fcvt.l.d t0, f1\n" + nops(d) + "    fcvt.d.l f2, t0\n" + kGap,
                  false, 2, kThreeD, Owe::All, d);
        checkCase(dir, "fmv.x.d t0,f1 -> fmv.d.x f2, t0",
                  setup + "    fmv.x.d t0, f1\n" + nops(d) + "    fmv.d.x f2, t0\n" + kGap, false,
                  2, kThreeD, Owe::All, d);
        checkCase(dir, "feq.d t0,f1,f1 -> fcvt.d.l f2, t0",
                  setup + "    feq.d t0, f1, f1\n" + nops(d) + "    fcvt.d.l f2, t0\n" + kGap,
                  false, 2, 0x3FF0000000000000ULL, Owe::All, d);
    }
}

// ───────── (e) FPR chains: regression, these already worked ────────────

TEST(FpIntSourceHazard, FprChainsStayCorrect)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string three = loadDouble(1, kThreeD, 5);
    const std::string five  = loadDouble(2, 0x4014000000000000ULL, 6);
    const std::string seven = loadDouble(3, kSevenD, 7);
    for (int d : kDistances)
    {
        // fcvt -> fmadd rs1
        checkCase(dir, "fcvt.d.l f4,t0 -> fmadd.d f5,f4,f2,f3",
                  five + seven + "    addi t0, x0, 3\n" + kGap + "    fcvt.d.l f4, t0\n" +
                      nops(d) + "    fmadd.d f5, f4, f2, f3\n" + kGap,
                  false, 5, kTwentyTwoD, Owe::All, d);
        // fmadd -> fcvt.l.d (an FPR result into an FP op writing a GPR)
        checkCase(dir, "fmadd.d f5,f1,f2,f3 -> fcvt.l.d t1, f5",
                  three + five + seven + "    fmadd.d f5, f1, f2, f3\n" + nops(d) +
                      "    fcvt.l.d t1, f5\n" + kGap,
                  true, 6, 22, Owe::All, d);
        // fmadd -> fsd store data
        checkCase(dir, "fmadd.d f5,f1,f2,f3 -> fsd f5, 0(a0)",
                  three + five + seven + "    addi a0, x0, 1408\n" + kGap +
                      "    fmadd.d f5, f1, f2, f3\n" + nops(d) + "    fsd f5, 0(a0)\n" + kGap +
                      "    fld f6, 0(a0)\n" + kGap,
                  false, 6, kTwentyTwoD, Owe::All, d);
        // fld -> fmadd rs3 (load-use on an FPR)
        checkCase(dir, "fld f3,0(a0) -> fmadd.d f5,f1,f2,f3",
                  three + five + seven + "    addi a0, x0, 1472\n" + kGap +
                      "    fsd f3, 0(a0)\n" + kGap + "    fld f3, 0(a0)\n" + nops(d) +
                      "    fmadd.d f5, f1, f2, f3\n" + kGap,
                  false, 5, kTwentyTwoD, Owe::NotNhFOnLoad, d);
        // WAW on an f register: the second write must win
        checkCase(dir, "fcvt.d.l f4,t0 ; fcvt.d.l f4,t1 (WAW)",
                  "    addi t0, x0, 3\n" + kGap + "    addi t1, x0, 7\n" + kGap +
                      "    fcvt.d.l f4, t0\n" + nops(d) + "    fcvt.d.l f4, t1\n" + kGap,
                  false, 4, kSevenD, Owe::All, d);
    }
}

// ───────── (f) the alias matrix: an index is not a register class ──────

TEST(FpIntSourceHazard, AnIndexIsNotARegisterClass)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (int d : kDistances)
    {
        // A producer writing f5 must not reach a consumer reading x5.
        // x5 = t0 = 3 throughout; f5 is set to 7.0 right before.
        checkCase(dir, "writes f5 -> consumer reads x5: must NOT forward",
                  "    addi t0, x0, 3\n" + kGap + "    addi t1, x0, 7\n" + kGap +
                      "    fcvt.d.l f5, t1\n" + nops(d) + "    fcvt.d.l f1, t0\n" + kGap,
                  false, 1, kThreeD, Owe::All, d);
        // A producer writing x5 must not reach a consumer reading f5.
        // f5 = 3.0 throughout; x5 = t0 is set to 7 right before.
        checkCase(dir, "writes x5 -> consumer reads f5: must NOT forward",
                  loadDouble(5, kThreeD, 6) + "    addi t0, x0, 7\n" + nops(d) +
                      "    fadd.d f1, f5, f5\n" + kGap,
                  false, 1, kSixD, Owe::All, d);
        // The same, with the sharper producer: fcvt.l.d writes a GPR but
        // is an F/D instruction, so freg_write is set and frd carries the
        // x index. Nothing may forward that into f5.
        checkCase(dir, "fcvt.l.d t0,f6 (writes x5) -> consumer reads f5",
                  loadDouble(5, kThreeD, 7) + loadDouble(6, kSevenD, 8) +
                      "    fcvt.l.d t0, f6\n" + nops(d) + "    fadd.d f1, f5, f5\n" + kGap,
                  false, 1, kSixD, Owe::All, d);
        // x0 is hardwired: a write to it forwards nothing.
        checkCase(dir, "addi x0,x0,5 -> fcvt.d.l f1, x0 must read 0",
                  program("", "addi x0, x0, 5", d, "fcvt.d.l f1, x0"), false, 1, 0, Owe::All, d);
    }
    // An R4-type's bits 31:25 are rs3<<2 | fmt, not a funct7. rs3 = f26
    // reads 0b1101000 / 0b1101001 and rs3 = f30 reads 0b1111000 /
    // 0b1111001 - the four patterns that mean "this instruction's rs1 is
    // an integer register". The FMA's rs1 is an F register and must stay
    // one. 3*5+7 = 22 whichever rs3 holds the 7.
    for (int rs3 : {26, 30})
    {
        for (const char *width : {"s", "d"})
        {
            const bool isDouble = (*width == 'd');
            std::string body = isDouble ? loadDouble(1, kThreeD, 5) + loadDouble(2, 0x4014000000000000ULL, 6) +
                                              loadDouble(rs3, kSevenD, 7)
                                        : loadSingle(1, 0x40400000u, 5) + loadSingle(2, 0x40A00000u, 6) +
                                              loadSingle(rs3, 0x40E00000u, 7);
            body += "    fmadd." + std::string(width) + " f4, f1, f2, f" + std::to_string(rs3) +
                    "\n" + kGap;
            checkCase(dir, "fmadd." + std::string(width) + " with rs3=f" + std::to_string(rs3) +
                               ": rs1 is an F register, not x[rs1]",
                      body, false, 4, isDouble ? kTwentyTwoD : kTwentyTwoS, Owe::All, 3);
        }
    }
}

// ───────── the other direction: a PLAIN INTEGER consumer ──────────────
//
// Everything above has an FP-class consumer. The integer forwarding
// block has the mirror-image hole: ex_mem_reg_.alu_result is latched for
// EVERY instruction, `reg_write` is set for every OP-FP instruction, and
// `rd` carries the same five bits as `frd` - so an FP producer writing
// f5 can reach an ordinary `addi` reading x5.
//
// t0 is x5 and a0 is x10, so a producer writing f5 or f10 collides with
// a consumer reading t0 or a0 respectively.

namespace
{
/// f1 = 3.0, f2 = 3.0, f6 = 7.0, and a sentinel in the integer register
/// the consumer will read.
std::string integerConsumerSetup(int gpr, uint64_t sentinel)
{
    return loadDouble(1, kThreeD, 11) + loadDouble(2, kThreeD, 12) + loadDouble(6, kSevenD, 13) +
           loadImmediate(gpr, sentinel);
}
} // namespace

TEST(FpIntSourceHazard, FpProducerDoesNotReachAPlainIntegerConsumer)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    struct Producer
    {
        const char *what;
        const char *text;
        int         index; // the f register it writes
    };
    const Producer producers[] = {
        {"fadd.d f5", "fadd.d f5, f1, f2", 5},
        {"fadd.d f10", "fadd.d f10, f1, f2", 10},
        {"fcvt.d.l f5", "fcvt.d.l f5, t3", 5},
        {"fmadd.d f10", "fmadd.d f10, f1, f2, f6", 10},
        {"fld f10", "fld f10, 0(t4)", 10},
        {"flw f5", "flw f5, 0(t4)", 5},
    };
    // t3 = 7 for fcvt.d.l, t4 = 1600 holding 3.0 for the loads.
    const std::string common = "    addi t3, x0, 7\n" + kGap + "    addi t4, x0, 1600\n" + kGap +
                               loadDouble(3, kThreeD, 14) + "    fsd f3, 0(t4)\n" + kGap;

    for (const Producer &p : producers)
    {
        const char *reg = (p.index == 5) ? "t0" : "a0";
        for (int d : {0, 1})
        {
            // rs1 of an addi
            checkCase(dir, std::string(p.what) + " -> addi s0, " + reg + ", 1",
                      common + integerConsumerSetup(p.index, 90) + "    " + p.text + "\n" +
                          nops(d) + "    addi s0, " + reg + ", 1\n" + kGap,
                      true, 8, 91, Owe::All, d);
            // rs2 of an add
            checkCase(dir, std::string(p.what) + " -> add s0, t5, " + reg,
                      common + integerConsumerSetup(p.index, 90) + "    addi t5, x0, 5\n" + kGap +
                          "    " + p.text + "\n" + nops(d) + "    add s0, t5, " + reg + "\n" +
                          kGap,
                      true, 8, 95, Owe::All, d);
            // store DATA: rs2 of an sd, read back through a settled base
            checkCase(dir, std::string(p.what) + " -> sd " + reg + ", 0(t6)",
                      common + integerConsumerSetup(p.index, 90) + "    addi t6, x0, 1664\n" +
                          kGap + "    " + p.text + "\n" + nops(d) + "    sd " + reg +
                          ", 0(t6)\n" + kGap + "    ld s0, 0(t6)\n" + kGap,
                      true, 8, 90, Owe::All, d);
            // BASE of a load: the sentinel is an address holding 1234
            checkCase(dir, std::string(p.what) + " -> ld s0, 0(" + reg + ")",
                      common + integerConsumerSetup(p.index, 1728) +
                          "    addi s1, x0, 1234\n" + kGap + "    sd s1, 0(" + reg + ")\n" +
                          kGap + "    " + p.text + "\n" + nops(d) + "    ld s0, 0(" + reg +
                          ")\n" + kGap,
                      true, 8, 1234, Owe::All, d);
            // a branch operand: s0 stays 0 only if the comparison sees 90
            checkCase(dir, std::string(p.what) + " -> beq " + reg + ", t5, taken",
                      common + integerConsumerSetup(p.index, 90) + "    addi t5, x0, 90\n" +
                          kGap + "    addi s0, x0, 7\n" + kGap + "    " + p.text + "\n" +
                          nops(d) + "    beq " + reg + ", t5, skip\n" + kGap +
                          "    addi s0, x0, 99\n" + kGap + "skip:\n" + kGap,
                      true, 8, 7, Owe::All, d);
        }
    }
}

TEST(FpIntSourceHazard, FpProducersThatWriteAGprStillForward)
{
    // The guard must not be too strict. These three FP instructions do
    // write an integer register, and their results must reach an
    // ordinary integer consumer exactly as any other producer's would.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string setup = loadDouble(1, kThreeD, 11) + loadDouble(2, kThreeD, 12) +
                              loadDouble(6, kSevenD, 13);
    for (int d : {0, 1})
    {
        checkCase(dir, "feq.d t0,f1,f2 -> addi t1, t0, 1",
                  setup + "    feq.d t0, f1, f2\n" + nops(d) + "    addi t1, t0, 1\n" + kGap,
                  true, 6, 2, Owe::All, d);
        checkCase(dir, "fcvt.l.d t0,f6 -> add t1, t0, t0",
                  setup + "    fcvt.l.d t0, f6\n" + nops(d) + "    add t1, t0, t0\n" + kGap,
                  true, 6, 14, Owe::All, d);
        checkCase(dir, "fmv.x.d t0,f1 -> sd t0, 0(t6)",
                  setup + "    addi t6, x0, 1792\n" + kGap + "    fmv.x.d t0, f1\n" + nops(d) +
                      "    sd t0, 0(t6)\n" + kGap + "    ld s0, 0(t6)\n" + kGap,
                  true, 8, kThreeD, Owe::All, d);
    }
}

// ───────── (g) the program from the report ─────────────────────────────

TEST(FpIntSourceHazard, TheReportedProgram)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (int d : {0, 4})
    {
        checkCase(dir, "li t0,3 ; fcvt.d.l f1,t0 ; fmadd.d f4,f1,f1,f1 - f1",
                  "    li t0, 3\n" + nops(d) + "    fcvt.d.l f1, t0\n" + nops(d) +
                      "    fmadd.d f4, f1, f1, f1\n" + kGap,
                  false, 1, kThreeD, Owe::All, d);
        checkCase(dir, "li t0,3 ; fcvt.d.l f1,t0 ; fmadd.d f4,f1,f1,f1 - f4",
                  "    li t0, 3\n" + nops(d) + "    fcvt.d.l f1, t0\n" + nops(d) +
                      "    fmadd.d f4, f1, f1, f1\n" + kGap,
                  false, 4, kTwelveD, Owe::All, d);
    }
}
