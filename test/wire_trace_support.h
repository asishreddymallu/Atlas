/**
 * @file wire_trace_support.h
 * @brief The per-cycle wire oracle shared by wire_timing_test.cpp and
 *        stage_tag_test.cpp.
 *
 * Extracted from wire_timing_test.cpp when stage_tag_test.cpp needed the
 * same table: one copy, so the two tests cannot drift. It is known_issues
 * 95's audit written down a second time - which stage's instruction each
 * conditional h_f wire is drawn for - plus a literal table of what the
 * control unit decodes for seven instructions. A test built on it proves
 * the code follows that reading; it cannot prove the reading is the
 * textbook's.
 */
#ifndef KITES_TEST_WIRE_TRACE_SUPPORT_H
#define KITES_TEST_WIRE_TRACE_SUPPORT_H

#include <QSet>
#include <QString>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Kites::test_support::wire_trace
{
/// What the control unit decodes for each traced instruction, written out
/// literally (rv5s_control_unit.cpp). `taken` and `jump` are what the
/// program makes the instruction do.
struct Flags
{
    bool aluSrc = false, memRead = false, memWrite = false, regWrite = false;
    bool branch = false, taken = false, jump = false;
};

enum Stage
{
    IF = 0,
    ID,
    EX,
    MEM,
    WB
};

/// The h_f rule table: the conditional wires an instruction with these
/// flags lights when it is labelled with this stage. The default PC+4 wire
/// is handled separately because it belongs to the cycle, not to one
/// instruction.
inline QSet<QString> hfWiresFor(Stage stage, const Flags &f)
{
    QSet<QString> w;
    switch (stage)
    {
    case ID:
        if (f.aluSrc)
            w << "Imm_to_P2";
        break;
    case EX:
        if (f.aluSrc)
            w << "P2Imm_to_ALU2_down";
        if (f.branch)
            w << "ALU_zerores_to_P3";
        break;
    case MEM:
        if (f.memRead)
            w << "P3_to_DM_Memread" << "DMMux_to_DM";
        if (f.memWrite)
            w << "P3_TO_DM_control_memwrite";
        if (f.branch && f.taken)
            w << "ANDGate_lower_entry" << "ANDGATE_to_PCMUX" << "P3_to_PCMux";
        break;
    case WB:
        if (f.regWrite)
            w << "P4_wbcontrol_to_WBmux" << "WBMux_to_RF";
        break;
    case IF:
        break;
    }
    return w;
}

/// The PC mux takes PC+4 unless a taken branch is in MEM or a jump in EX.
inline bool pcPlusFourSelected(const std::map<Stage, bool> &in, const Flags &f)
{
    if (in.at(MEM) && f.branch && f.taken)
        return false;
    if (in.at(EX) && f.jump)
        return false;
    return true;
}

struct Traced
{
    const char *what;
    std::string body; // the instruction and anything after it
    Flags       flags;
};

/// Four nops, so the instruction sits at pc 16 - never 0, which is also
/// what an empty stage's pc reads as.
inline const std::string kLead = "    nop\n    nop\n    nop\n    nop\n";
inline const std::string kTail = "    nop\n    nop\n    nop\n    nop\n    nop\n    nop\n";
inline constexpr uint64_t kTracedPc = 16;

inline std::vector<Traced> traced()
{
    std::vector<Traced> t;
    Flags               f;

    f = {};
    f.regWrite = true;
    t.push_back({"add", "    add t2, t0, t1\n" + kTail, f});

    f = {};
    f.aluSrc = f.memRead = f.regWrite = true;
    t.push_back({"lw", "    lw t2, 256(x0)\n" + kTail, f});

    f = {};
    f.aluSrc = f.memWrite = true;
    t.push_back({"sw", "    sw t1, 256(x0)\n" + kTail, f});

    f = {};
    f.branch = f.taken = true;
    t.push_back({"beq taken",
                 "    beq x0, x0, target\n    nop\n    nop\n    nop\ntarget:\n" + kTail, f});

    f = {};
    f.branch = true;
    t.push_back({"bne not taken", "    bne x0, x0, target\n" + kTail + "target:\n    nop\n", f});

    f = {};
    f.regWrite = f.jump = true;
    t.push_back({"jal", "    jal ra, target\n    nop\n    nop\n    nop\ntarget:\n" + kTail, f});

    // The control unit sets reg_write for every OP-FP instruction
    // (rv5s_control_unit.cpp, case 0b1010011), so the WB wires light for an
    // FP write too. That is what the code does, recorded, not endorsed.
    f = {};
    f.regWrite = true;
    t.push_back({"fadd.d", "    fadd.d f3, f1, f2\n" + kTail, f});

    return t;
}
} // namespace Kites::test_support::wire_trace

#endif // KITES_TEST_WIRE_TRACE_SUPPORT_H
