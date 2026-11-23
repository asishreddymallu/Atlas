/**
 * @file basic_block_scheduler.cpp
 * @brief Basic-block splitting, dependency graphs and list scheduling.
 *
 * Original author: Nandan Nihal (CS25BTECH11014)
 */
#include "scheduler/basic_block_scheduler.h"

#include "common/instructions.h"
#include <algorithm>
#include <limits>
#include <set>
#include <string>

namespace Atlas
{
namespace
{
using RegisterSet = std::set<std::string>;

struct InstructionInfo
{
    RegisterSet reads;
    RegisterSet writes;
    bool memoryAccess{};
    bool controlTransfer{};
    bool barrier{};
    bool load{};
};

bool isZeroRegister(const std::string &reg)
{
    return reg == "x0" || reg == "zero";
}

void addRead(RegisterSet &registers, const std::string &reg)
{
    if (!reg.empty() && !isZeroRegister(reg)) registers.insert(reg);
}

void addWrite(RegisterSet &registers, const std::string &reg)
{
    // Writes to x0 have no architectural effect and therefore create no dependency.
    if (!reg.empty() && !isZeroRegister(reg)) registers.insert(reg);
}

bool intersects(const RegisterSet &left, const RegisterSet &right)
{
    return std::any_of(left.begin(), left.end(),
                       [&right](const std::string &reg) { return right.contains(reg); });
}

bool isLoadOpcode(const std::string &opcode)
{
    return opcode == "lb" || opcode == "lh" || opcode == "lw" || opcode == "ld" ||
           opcode == "lbu" || opcode == "lhu" || opcode == "lwu";
}

InstructionInfo describe(const ICUnit &instruction)
{
    const std::string opcode = instruction.getOpcode();
    InstructionInfo info;

    if (instruction_set::isValidRTypeInstruction(opcode))
    {
        addRead(info.reads, instruction.getRs1());
        addRead(info.reads, instruction.getRs2());
        addWrite(info.writes, instruction.getRd());
    }
    else if (instruction_set::isValidI1TypeInstruction(opcode) ||
             instruction_set::isValidI2TypeInstruction(opcode))
    {
        addRead(info.reads, instruction.getRs1());
        addWrite(info.writes, instruction.getRd());
        info.load = isLoadOpcode(opcode);
        info.memoryAccess = info.load;
        info.controlTransfer = opcode == "jalr";
    }
    else if (instruction_set::isValidSTypeInstruction(opcode))
    {
        addRead(info.reads, instruction.getRs1()); // Address base.
        addRead(info.reads, instruction.getRs2()); // Stored value.
        info.memoryAccess = true;
    }
    else if (instruction_set::isValidBTypeInstruction(opcode))
    {
        addRead(info.reads, instruction.getRs1());
        addRead(info.reads, instruction.getRs2());
        info.controlTransfer = true;
    }
    else if (instruction_set::isValidUTypeInstruction(opcode))
    {
        addWrite(info.writes, instruction.getRd());
    }
    else if (instruction_set::isValidJTypeInstruction(opcode))
    {
        addWrite(info.writes, instruction.getRd());
        info.controlTransfer = true;
    }
    else
    {
        // Floating point, CSR, fences, syscalls, and unknown instructions are
        // intentionally not scheduled in Phase 1.  Treating them as barriers is
        // conservative and prevents an unsafe move around side effects.
        info.barrier = true;
    }

    info.barrier = info.barrier || info.controlTransfer;
    return info;
}

unsigned int rawLatency(const InstructionInfo &producer, const SchedulingOptions &options)
{
    // With forwarding, an ALU result can feed the next EX stage, but a load's
    // value arrives one cycle later.  Without forwarding, consumers wait until
    // writeback.  These values model issue-cycle distance, not total pipeline fill.
    if (options.forwardingEnabled) return producer.load ? 2U : 1U;
    return 3U;
}

bool isBlockBoundary(const ICUnit &instruction)
{
    const InstructionInfo info = describe(instruction);
    return info.controlTransfer || info.barrier;
}

std::set<size_t> labelInstructionIndices(const AssembledProgram &program)
{
    std::set<size_t> indices;
    for (const auto &[_, symbol] : program.symbol_table)
    {
        if (!symbol.isData) indices.insert(static_cast<size_t>(symbol.address / 4));
    }
    return indices;
}

std::vector<std::vector<size_t>> splitBasicBlocks(const AssembledProgram &program)
{
    const auto labels = labelInstructionIndices(program);
    std::vector<std::vector<size_t>> blocks;
    std::vector<size_t> current;
    const auto &code = program.intermediate_code;

    for (size_t index = 0; index < code.size(); ++index)
    {
        const size_t instructionIndex = code[index].first.getInstructionIndex();
        if (!current.empty() && labels.contains(instructionIndex))
        {
            blocks.push_back(std::move(current));
            current.clear();
        }

        current.push_back(index);
        if (isBlockBoundary(code[index].first))
        {
            blocks.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) blocks.push_back(std::move(current));
    return blocks;
}

void addEdge(std::vector<DependencyEdge> &edges, size_t producer, size_t consumer,
             DependencyKind kind, unsigned int latency)
{
    const auto duplicate = std::find_if(edges.begin(), edges.end(),
                                        [producer, consumer](const DependencyEdge &edge)
                                        { return edge.producer == producer && edge.consumer == consumer; });
    if (duplicate == edges.end()) edges.push_back({producer, consumer, kind, latency});
}

std::vector<DependencyEdge> buildDependencies(const AssembledProgram &program,
                                              const std::vector<size_t> &block,
                                              const SchedulingOptions &options)
{
    std::vector<InstructionInfo> instructions;
    instructions.reserve(block.size());
    for (size_t programIndex : block) instructions.push_back(describe(program.intermediate_code[programIndex].first));

    std::vector<DependencyEdge> edges;
    for (size_t earlier = 0; earlier < instructions.size(); ++earlier)
    {
        for (size_t later = earlier + 1; later < instructions.size(); ++later)
        {
            const auto &left = instructions[earlier];
            const auto &right = instructions[later];
            if (intersects(left.writes, right.reads))
                addEdge(edges, earlier, later, DependencyKind::ReadAfterWrite,
                        rawLatency(left, options));
            if (intersects(left.reads, right.writes))
                addEdge(edges, earlier, later, DependencyKind::WriteAfterRead, 1);
            if (intersects(left.writes, right.writes))
                addEdge(edges, earlier, later, DependencyKind::WriteAfterWrite, 1);
            if (left.memoryAccess && right.memoryAccess)
                addEdge(edges, earlier, later, DependencyKind::MemoryOrder, 1);

            // A barrier stays in program order with every instruction in its block.
            if (left.barrier || right.barrier)
                addEdge(edges, earlier, later, DependencyKind::Barrier, 1);
        }
    }
    return edges;
}

unsigned int criticalPath(size_t node, const std::vector<DependencyEdge> &edges,
                          std::vector<unsigned int> &memo)
{
    if (memo[node] != 0) return memo[node];
    unsigned int longest = 1;
    for (const auto &edge : edges)
    {
        if (edge.producer == node)
            longest = std::max(longest, edge.latency + criticalPath(edge.consumer, edges, memo));
    }
    memo[node] = longest;
    return longest;
}

BasicBlockSchedule scheduleBlock(const AssembledProgram &program, const std::vector<size_t> &block,
                                 const SchedulingOptions &options)
{
    BasicBlockSchedule result;
    result.originalInstructionIndices = block;
    result.dependencies = buildDependencies(program, block, options);
    const size_t count = block.size();
    if (count == 0) return result;

    std::vector<unsigned int> originalIssue(count);
    for (size_t node = 1; node < count; ++node)
    {
        unsigned int issue = originalIssue[node - 1] + 1;
        for (const auto &edge : result.dependencies)
            if (edge.consumer == node) issue = std::max(issue, originalIssue[edge.producer] + edge.latency);
        originalIssue[node] = issue;
    }
    result.originalCycleCount = originalIssue.back() + 1;

    std::vector<unsigned int> pathMemo(count);
    for (size_t node = 0; node < count; ++node) criticalPath(node, result.dependencies, pathMemo);

    std::vector<bool> scheduled(count, false);
    std::vector<unsigned int> issueCycle(count, std::numeric_limits<unsigned int>::max());
    unsigned int cycle = 0;
    for (size_t scheduledCount = 0; scheduledCount < count;)
    {
        size_t selected = count;
        for (size_t candidate = 0; candidate < count; ++candidate)
        {
            if (scheduled[candidate]) continue;

            // Heuristic 1 - legality: a candidate is eligible only after every
            // predecessor has issued.  This preserves the dependency DAG.
            bool ready = true;
            for (const auto &edge : result.dependencies)
            {
                if (edge.consumer == candidate &&
                    (!scheduled[edge.producer] || issueCycle[edge.producer] + edge.latency > cycle))
                {
                    ready = false;
                    break;
                }
            }
            if (!ready) continue;

            // Heuristic 2 - stall avoidance: checking producer latency against
            // the current issue cycle means that an instruction is selected only
            // if it can issue now.  If a load consumer is still waiting, an
            // independent ready instruction is therefore chosen to fill its gap.
            //
            // Heuristic 3 - priority: when several candidates can issue now,
            // prefer the one on the longest remaining dependency chain.  Original
            // order is the deterministic tie-breaker, making suggestions stable.
            if (selected == count || pathMemo[candidate] > pathMemo[selected] ||
                (pathMemo[candidate] == pathMemo[selected] && candidate < selected))
                selected = candidate;
        }

        if (selected == count)
        {
            ++cycle; // A required latency bubble; no instruction is safe to issue now.
            continue;
        }

        scheduled[selected] = true;
        issueCycle[selected] = cycle;
        result.scheduledInstructionIndices.push_back(block[selected]);
        ++scheduledCount;
        ++cycle;
    }
    result.scheduledCycleCount = cycle;
    return result;
}
} // namespace

SchedulingResult BasicBlockScheduler::schedule(const AssembledProgram &program,
                                                const SchedulingOptions &options) const
{
    SchedulingResult result;
    for (const auto &block : splitBasicBlocks(program))
    {
        auto scheduled = scheduleBlock(program, block, options);
        result.originalCycleCount += scheduled.originalCycleCount;
        result.scheduledCycleCount += scheduled.scheduledCycleCount;
        result.blocks.push_back(std::move(scheduled));
    }
    return result;
}
} // namespace Atlas
