/**
 * @file isa_case_support.h
 * @brief The shared shape of the Stage E3 ISA tests: a table of programs,
 *        each with the registers and memory words it must leave, run once
 *        per (case, model) so every model is its own ctest process and its
 *        own failing NAME.
 *
 * The tables themselves are generated out of tree by Python scripts in
 * ~/atlas-verify/e3/ - an oracle or an encoder written from the RISC-V
 * spec - and pasted into each test with a comment naming the script.
 * Nothing in them is read back from rvss.
 */
#ifndef ATLAS_TEST_ISA_CASE_SUPPORT_H
#define ATLAS_TEST_ISA_CASE_SUPPORT_H

#include "float_test_support.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace Atlas::test_support
{
void PrintTo(Model model, std::ostream *os); // defined in ecall_syscall_test.cpp

/// One program and what it must leave behind.
struct IsaCase
{
    const char *name;
    const char *program;                                ///< whole file, .data and .text
    std::vector<std::pair<int, uint64_t>>      x;       ///< x register -> value
    std::vector<std::pair<uint64_t, uint64_t>> dwords;  ///< address -> 64-bit word
    std::vector<std::pair<int, uint64_t>>      f = {};  ///< f register -> value
};

/// A (case index, model) pair, the test parameter.
struct IsaParam
{
    std::size_t index;
    Model       model;
    const char *label; ///< the case's name, for test output
};

/// Readable in gtest output and in the ctest names gtest_discover_tests builds.
inline void PrintTo(const IsaParam &param, std::ostream *os)
{
    *os << param.label << "/" << name(param.model);
}

/// Assembles `text` and runs it to completion with DebugRun().
inline std::unique_ptr<ProcessorBase> runIsaProgram(Model model, const QTemporaryDir &dir,
                                                    const std::string &text)
{
    const std::string path =
        dir.path().toStdString() + "/isa" + std::to_string(fileSerial++) + ".s";
    {
        std::ofstream file(path);
        file << text;
    }
    setupVmStateDirectory();
    auto processor = makeProcessor(model);
    processor->LoadProgram(assemble(path));
    processor->DebugRun();
    return processor;
}

/// Assembles `text` and returns the encoded instruction words.
inline std::vector<uint32_t> encode(const QTemporaryDir &dir, const std::string &text)
{
    const std::string path =
        dir.path().toStdString() + "/enc" + std::to_string(fileSerial++) + ".s";
    {
        std::ofstream file(path);
        file << text;
    }
    return assemble(path).text_buffer;
}

/// Every check an IsaCase carries, on one model.
inline void expectIsaCase(const IsaCase &c, Model model, const QTemporaryDir &dir)
{
    std::unique_ptr<ProcessorBase> got;
    ASSERT_NO_THROW(got = runIsaProgram(model, dir, c.program)) << "did not assemble";
    for (const auto &[reg, value] : c.x)
    {
        const uint64_t actual = got->registers_.ReadGpr(reg);
        EXPECT_EQ(actual, value) << name(model) << " x" << reg << " = " << hex(actual, 16)
                                 << ", the oracle says " << hex(value, 16);
    }
    for (const auto &[reg, value] : c.f)
    {
        const uint64_t actual = got->registers_.ReadFpr(reg);
        EXPECT_EQ(actual, value) << name(model) << " f" << reg << " = " << hex(actual, 16)
                                 << ", the oracle says " << hex(value, 16);
    }
    for (const auto &[address, value] : c.dwords)
    {
        const uint64_t actual = got->memory_controller_.readDoubleWord_d(address);
        EXPECT_EQ(actual, value) << name(model) << " [" << hex(address, 8)
                                 << "] = " << hex(actual, 16) << ", the oracle says "
                                 << hex(value, 16);
    }
}

/// gtest's name for a parameter: "<case>_<model>", '-' folded to '_'.
template <std::size_t N>
std::string isaParamName(const IsaCase (&cases)[N], const IsaParam &param)
{
    std::string label = std::string(cases[param.index].name) + "_" + name(param.model);
    for (char &c : label)
        if (c == '-' || c == '.')
            c = '_';
    return label;
}

/// Every (case, model) pair for the given models.
template <std::size_t N>
std::vector<IsaParam> isaParams(const IsaCase (&cases)[N], const std::vector<Model> &models)
{
    std::vector<IsaParam> out;
    for (std::size_t i = 0; i < N; ++i)
        for (Model model : models)
            out.push_back({i, model, cases[i].name});
    return out;
}
} // namespace Atlas::test_support

#endif // ATLAS_TEST_ISA_CASE_SUPPORT_H
