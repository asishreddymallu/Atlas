/**
 * @file float_test_support.h
 * @brief The shared fixture for the floating-point tests.
 *
 * Extracted from float_completeness_test.cpp when canonical_nan_test.cpp
 * needed the same thing. It is one fixture, not two copies, because the
 * byte-at-a-time loader below is subtle enough that two drifting copies
 * would be a real hazard: every float assertion in the suite rests on it
 * putting an exact pattern in a register on all five models.
 *
 * Two conventions this fixture follows rather than invents:
 *
 *   - A single-precision value occupies the LOW 32 BITS of the 64-bit f
 *     register, zero-extended. That is what `Alu::fpexecute` returns
 *     (`static_cast<uint64_t>(result_bits)`) and what it reads
 *     (`memcpy(&a, &ina, sizeof(float))`). It is NOT what RISC-V
 *     specifies - the spec NaN-boxes a single - see the NaN-boxing entry
 *     in docs/audit/known_issues.md. The tests pin what this simulator
 *     does today, and say where that differs from the spec.
 *   - Dependent instructions are separated by four `nop`s, because the
 *     nh_nf and nh_f variants have no hazard detection.
 */
#ifndef KITES_TEST_FLOAT_TEST_SUPPORT_H
#define KITES_TEST_FLOAT_TEST_SUPPORT_H

#include "assembler/assembler.h"
#include "processor/processor_base.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"

#include <QTemporaryDir>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace Kites::test_support
{
enum class Model
{
    Rvss,
    Rv5NhNf,
    Rv5NhF,
    Rv5HNf,
    Rv5HF
};

inline const std::vector<Model> &allModels()
{
    static const std::vector<Model> models{Model::Rvss, Model::Rv5NhNf, Model::Rv5NhF,
                                           Model::Rv5HNf, Model::Rv5HF};
    return models;
}

inline const char *name(Model model)
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

inline std::unique_ptr<ProcessorBase> makeProcessor(Model model)
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

inline std::string hex(uint64_t value, int digits)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%0*llX", digits, (unsigned long long)value);
    return buffer;
}

/// Four nops: enough for a result to reach writeback before the next
/// instruction decodes, on the variants with no hazard detection.
inline const char *kGap = "    nop\n    nop\n    nop\n    nop\n";

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
inline std::string loadImmediate(int gpr, uint64_t bits)
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
inline std::string loadDouble(int fpr, uint64_t bits, int scratch = 5)
{
    std::ostringstream out;
    out << loadImmediate(scratch, bits) << "    fmv.d.x f" << fpr << ", x" << scratch << "\n"
        << kGap;
    return out.str();
}

/// The same for a single: the low 32 bits, zero-extended by fmv.w.x.
inline std::string loadSingle(int fpr, uint32_t bits, int scratch = 5)
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

inline int fileSerial = 0;

/// Assembles `body` as a whole program and runs it to completion.
inline Outcome assembleAndRun(Model model, const QTemporaryDir &dir, const std::string &body)
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

inline bool isQuietNaN64(uint64_t bits)
{
    return ((bits >> 52) & 0x7FF) == 0x7FF && (bits & 0xFFFFFFFFFFFFFULL) != 0 &&
           (bits & (1ULL << 51)) != 0;
}
inline bool isQuietNaN32(uint64_t bits)
{
    const uint32_t word = static_cast<uint32_t>(bits);
    return ((word >> 23) & 0xFF) == 0xFF && (word & 0x7FFFFF) != 0 && (word & (1u << 22)) != 0;
}

/// A sentinel in the destination, so "wrote zero" and "did not write"
/// stay distinguishable - the distinction known_issues 57 and 61 turn on.
inline constexpr uint64_t kSentinel = 0x40934800ABCDEF12ULL;

/// The canonical NaN, as the RISC-V F extension defines it: positive,
/// quiet bit set, payload zero.
inline constexpr uint64_t kCanonicalNaN32 = 0x7FC00000ULL;
inline constexpr uint64_t kCanonicalNaN64 = 0x7FF8000000000000ULL;

} // namespace Kites::test_support

#endif // KITES_TEST_FLOAT_TEST_SUPPORT_H
