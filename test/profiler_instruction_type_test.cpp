/**
 * @file profiler_instruction_type_test.cpp
 * @brief Every instruction the assembler accepts has a profiler type.
 *
 * Pressing Run aborted the app with a libstdc++ bounds assertion inside
 * `Profiler::setLineNumberToInstructionTypeMapping`. That function counts
 * each assembled instruction into a `std::array` with one slot per
 * `InstructionType` below `INSTRUCTION_TYPE_COUNT`
 * (`src/profiler/profiler.h:16`, `src/common/instruction_types.h:22-34`).
 * But `instr::getInstructionType` returns `UNKNOWN` (one past
 * `INSTRUCTION_TYPE_COUNT`) for anything it does not match, and its
 * CSR branches were commented out (`src/common/instructions.cpp:1317-1321`).
 * So `csrrs`, `csrrwi` and the rest index the array out of bounds.
 * `ProfilerEditor::updateLineInstructionTypeSlot` reads
 * `instructionTypeNames` with the same index, from the display side.
 *
 * This test runs one case per mnemonic. The list is
 * `instruction_syntax_map` ∪ `instruction_string_map`, so it grows
 * whenever the assembler does, and the pseudos are already in it. The
 * test name is the mnemonic. Each case assembles a minimal program and
 * checks three things for every resulting instruction:
 *   - it has a defined type, not UNKNOWN;
 *   - its type indexes inside the count array and the name table;
 *   - the Profiler's per-type counts add up to the number of
 *     instructions.
 * A separate test checks that a second load does not double the counts
 * (known_issues 85).
 *
 * The test build has no `_GLIBCXX_ASSERTIONS` (no such define in
 * build-tests/build.ninja, and CMAKE_CXX_FLAGS is empty). So the index is
 * compared explicitly, before the Profiler is asked to count. An
 * unchecked out-of-bounds increment would otherwise be silent heap
 * corruption, and the test could pass by luck.
 */
#include "assembler/assembler.h"
#include "common/instruction_types.h"
#include "common/instructions.h"
#include "profiler/profiler.h"
#include "utils/to_index.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <algorithm>
#include <fstream>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Kites;
namespace instr = instruction_set;

namespace
{
std::vector<std::string> allMnemonics()
{
    std::set<std::string> names;
    for (const auto &entry : instr::instruction_syntax_map)
        names.insert(entry.first);
    for (const auto &entry : instr::instruction_string_map)
        names.insert(entry.first);
    return {names.begin(), names.end()};
}

/// Operands for a pseudo, written out by hand. The syntax map only
/// says PSEUDO for these, so there is nothing to derive the operands from.
/// A pseudo that is missing here fails the case loudly, as "no template".
const std::map<std::string, std::string> &pseudoOperands()
{
    static const std::map<std::string, std::string> m = {
        {"nop", ""},           {"li", "t0, 1"},       {"la", "t0, D"},
        {"mv", "t0, t1"},      {"not", "t0, t1"},     {"neg", "t0, t1"},
        {"negw", "t0, t1"},    {"sext.w", "t0, t1"},  {"seqz", "t0, t1"},
        {"snez", "t0, t1"},    {"sltz", "t0, t1"},    {"sgtz", "t0, t1"},
        {"beqz", "t0, L"},     {"bnez", "t0, L"},     {"blez", "t0, L"},
        {"bgez", "t0, L"},     {"bltz", "t0, L"},     {"bgtz", "t0, L"},
        {"bgt", "t0, t1, L"},  {"ble", "t0, t1, L"},  {"bgtu", "t0, t1, L"},
        {"bleu", "t0, t1, L"}, {"j", "L"},            {"jr", "t0"},
        {"ret", ""},           {"call", "L"},         {"tail", "L"},
        {"fence", ""},         {"fence_i", ""},       {"csrr", "t0, fflags"},
        {"csrw", "fflags, t0"}, {"csrs", "fflags, t0"}, {"csrc", "fflags, t0"},
        {"csrwi", "fflags, 1"}, {"csrsi", "fflags, 1"}, {"csrci", "fflags, 1"},
        // Old names for fmv.x.w and fmv.w.x. They are in
        // instruction_string_map only, with no syntax entry.
        {"fmv.x.s", "t0, f1"}, {"fmv.s.x", "f1, t0"},
    };
    return m;
}

/// Operands for a real instruction, from the first syntax listed for it.
bool operandsFor(const std::string &mnemonic, std::string &out)
{
    using S = instr::SyntaxType;
    const auto it = instr::instruction_syntax_map.find(mnemonic);
    if (it == instr::instruction_syntax_map.end() || it->second.empty() ||
        it->second.front() == S::PSEUDO)
    {
        const auto p = pseudoOperands().find(mnemonic);
        if (p == pseudoOperands().end())
            return false;
        out = p->second;
        return true;
    }
    // The label form when there is one: an immediate branch or jump
    // offset must be even and in range, and a label always is.
    S syntax = it->second.front();
    for (S candidate : it->second)
        if (candidate == S::O_GPR_C_GPR_C_IL || candidate == S::O_GPR_C_IL)
            syntax = candidate;
    switch (syntax)
    {
    case S::O: out = ""; return true;
    case S::O_GPR_C_GPR_C_GPR: out = "t0, t1, t2"; return true;
    case S::O_GPR_C_GPR_C_I: out = "t0, t1, 1"; return true;
    case S::O_GPR_C_I: out = "t0, 1"; return true;
    case S::O_GPR_C_GPR_C_IL: out = "t0, t1, L"; return true;
    case S::O_GPR_C_GPR_C_DL: out = "t0, t1, D"; return true;
    case S::O_GPR_C_IL: out = "t0, L"; return true;
    case S::O_GPR_C_DL: out = "t0, D"; return true;
    case S::O_GPR_C_I_LP_GPR_RP: out = "t0, 0(t1)"; return true;
    case S::O_GPR_C_CSR_C_GPR: out = "t0, fflags, t1"; return true;
    case S::O_GPR_C_CSR_C_I: out = "t0, fflags, 1"; return true;
    case S::O_FPR_C_FPR_C_FPR_C_FPR: out = "f1, f2, f3, f4"; return true;
    case S::O_FPR_C_FPR_C_FPR_C_FPR_C_RM: out = "f1, f2, f3, f4, rne"; return true;
    case S::O_FPR_C_FPR_C_FPR: out = "f1, f2, f3"; return true;
    case S::O_FPR_C_FPR_C_FPR_C_RM: out = "f1, f2, f3, rne"; return true;
    case S::O_FPR_C_FPR: out = "f1, f2"; return true;
    case S::O_FPR_C_FPR_C_RM: out = "f1, f2, rne"; return true;
    case S::O_FPR_C_GPR: out = "f1, t0"; return true;
    case S::O_FPR_C_GPR_C_RM: out = "f1, t0, rne"; return true;
    case S::O_GPR_C_FPR: out = "t0, f1"; return true;
    case S::O_GPR_C_FPR_C_RM: out = "t0, f1, rne"; return true;
    case S::O_GPR_C_FPR_C_FPR: out = "t0, f1, f2"; return true;
    case S::O_FPR_C_I_LP_GPR_RP: out = "f1, 0(t1)"; return true;
    default: return false;
    }
}

std::string programFor(const std::string &mnemonic, const std::string &operands)
{
    // L is defined before the line under test so that a branch or a jump
    // has a target either way. D gives `la` and the data-label forms a
    // symbol.
    return ".data\nD: .word 0\n.text\nL:\n    addi x0, x0, 0\n    " + mnemonic +
           (operands.empty() ? "" : " " + operands) + "\n";
}

/// The file overload: the stream one opens a globals path for some
/// instructions (branches, jal) and throws "Could not open the file"
/// before any of them reach the profiler.
AssembledProgram assembleText(const std::string &text)
{
    static QTemporaryDir dir;
    static int           serial = 0;
    const std::string    path = dir.path().toStdString() + "/p" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << text;
    }
    setupVmStateDirectory();
    return assemble(path);
}

std::string testName(const ::testing::TestParamInfo<std::string> &info)
{
    std::string name = info.param;
    std::replace(name.begin(), name.end(), '.', '_');
    return name;
}
} // namespace

class ProfilerInstructionType : public ::testing::TestWithParam<std::string>
{
};

TEST_P(ProfilerInstructionType, HasAProfilerType)
{
    const std::string mnemonic = GetParam();
    std::string operands;
    ASSERT_TRUE(operandsFor(mnemonic, operands)) << "no operand template for " << mnemonic;

    // Listed in the assembler's tables but rejected by its parser
    // (known_issues 141), so they never reach the profiler. Pinned here: if
    // one starts to assemble, this fails, and it has to earn a type below.
    static const std::set<std::string> kListedButRejected = {"fence", "fence_i", "fmv.s.x",
                                                             "fmv.x.s"};
    if (kListedButRejected.count(mnemonic))
    {
        EXPECT_ANY_THROW(assembleText(programFor(mnemonic, operands)))
            << mnemonic << " assembles now; remove it from kListedButRejected";
        return;
    }

    AssembledProgram program;
    try
    {
        program = assembleText(programFor(mnemonic, operands));
    }
    catch (const std::exception &e)
    {
        FAIL() << mnemonic << " did not assemble: " << e.what() << "\n"
               << programFor(mnemonic, operands);
    }

    size_t instructions = 0;
    for (const auto &[icUnit, _] : program.intermediate_code)
    {
        const std::string opcode = icUnit.getOpcode();
        if (opcode.empty())
            continue;
        ++instructions;
        const instr::InstructionType type  = instr::getInstructionType(opcode);
        const size_t                 index = toIndex(type);
        // Every instruction has a defined type, not a catch-all.
        EXPECT_NE(type, instr::InstructionType::UNKNOWN)
            << mnemonic << " assembles to " << opcode << ", which is classified UNKNOWN";
        // The explicit form of the bounds assertion the app aborted on.
        ASSERT_LT(index, toIndex(instr::InstructionType::INSTRUCTION_TYPE_COUNT))
            << mnemonic << " assembles to " << opcode << ", which has no profiler type";
        ASSERT_LT(index, instr::instructionTypeNames.size())
            << mnemonic << " -> " << opcode << " has no type name";
    }
    ASSERT_GT(instructions, 0u) << mnemonic << " assembled to nothing";

    // Only reached once every index is known to be in bounds.
    Profiler profiler;
    profiler.setLineNumberToInstructionTypeMapping(program);
    const auto &counts = profiler.getInstructionTypeCounts();
    EXPECT_EQ(std::accumulate(counts.begin(), counts.end(), size_t{0}), instructions);
}

// Kept out of the per-mnemonic case on purpose. It fails for every
// program, so inside that case it would hide which mnemonics have no type.
TEST(ProfilerInstructionTypeCounts, ASecondLoadReplacesTheFirst)
{
    const AssembledProgram program =
        assembleText(".text\n    addi t0, x0, 1\n    add t1, t0, t0\n");
    Profiler profiler;
    profiler.setLineNumberToInstructionTypeMapping(program);
    profiler.setLineNumberToInstructionTypeMapping(program);
    const auto &counts = profiler.getInstructionTypeCounts();
    EXPECT_EQ(std::accumulate(counts.begin(), counts.end(), size_t{0}), 2u)
        << "a second load of the same program counted it twice (known_issues 85)";
}

INSTANTIATE_TEST_SUITE_P(EveryMnemonic, ProfilerInstructionType,
                         ::testing::ValuesIn(allMnemonics()), testName);
