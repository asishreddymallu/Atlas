#include "profiler_metrics/instruction_mix.h"

#include "common/instructions.h"

#include <algorithm>
#include <set>

namespace Atlas
{
MetricsProgram MetricsProgram::from(const AssembledProgram &program)
{
    MetricsProgram out;
    const size_t n = program.text_buffer.size();
    out.mnemonic.resize(n);
    out.line.assign(n, -1);
    out.word = program.text_buffer;
    for (size_t i = 0; i < n; ++i)
    {
        // The assembler emits one intermediate unit per machine word, in
        // order (generateMachineCode); its opcode is the machine mnemonic,
        // e.g. "addi" for a padding "addi x0, x0, 0" that the disassembler
        // would print as "nop".
        if (i < program.intermediate_code.size())
            out.mnemonic[i] = program.intermediate_code[i].first.getOpcode();
        if (out.mnemonic[i].empty())
        {
            const std::string text = instruction_set::disassemble(program.text_buffer[i]);
            out.mnemonic[i]        = text.substr(0, text.find(' '));
        }
        const auto it = program.instruction_number_line_number_mapping.find(static_cast<unsigned>(i));
        if (it != program.instruction_number_line_number_mapping.end())
            out.line[i] = static_cast<int>(it->second);
    }
    return out;
}

std::string MetricsProgram::mnemonicAt(uint64_t pc) const
{
    const uint64_t index = pc / 4;
    return index < mnemonic.size() ? mnemonic[index] : std::string();
}

bool isLoadMnemonic(const std::string &m)
{
    static const std::set<std::string> loads = {"lb", "lbu", "lh", "lhu", "lw",
                                                "lwu", "ld", "flw", "fld"};
    return loads.count(m) > 0;
}

bool isStoreMnemonic(const std::string &m)
{
    static const std::set<std::string> stores = {"sb", "sh", "sw", "sd", "fsw", "fsd"};
    return stores.count(m) > 0;
}

InstructionMix computeInstructionMix(const std::map<uint64_t, uint64_t> &retiredByPc,
                                     const MetricsProgram &program)
{
    InstructionMix                   mix;
    std::map<std::string, uint64_t> perMnemonic;
    for (const auto &[pc, count] : retiredByPc)
    {
        std::string m = program.mnemonicAt(pc);
        if (m.empty())
            m = "?";
        mix.retired += count;
        perMnemonic[m] += count;
        const size_t type = toIndex(instruction_set::getInstructionType(m));
        mix.byType[type < mix.byType.size() ? type
                                            : toIndex(instruction_set::InstructionType::UNKNOWN)] +=
            count;
        if (isLoadMnemonic(m))
            mix.loads += count;
        if (isStoreMnemonic(m))
            mix.stores += count;
    }
    for (const auto &[m, count] : perMnemonic)
        mix.byMnemonic.push_back({m, count});
    std::stable_sort(mix.byMnemonic.begin(), mix.byMnemonic.end(),
                     [](const MnemonicCount &a, const MnemonicCount &b)
                     { return a.count != b.count ? a.count > b.count : a.mnemonic < b.mnemonic; });
    return mix;
}

std::optional<double> shareOf(uint64_t part, uint64_t whole)
{
    if (whole == 0)
        return std::nullopt;
    return static_cast<double>(part) / static_cast<double>(whole);
}
} // namespace Atlas
