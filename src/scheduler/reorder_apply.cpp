/**
 * @file reorder_apply.cpp
 * @brief Source-line application of a SchedulingResult; see reorder_apply.h.
 */
#include "scheduler/reorder_apply.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace Atlas
{
namespace
{
struct Lines
{
    std::vector<std::string> lines; ///< Without the '\n'; a CRLF line keeps its '\r'.
    bool trailingNewline{false};
};

Lines splitLines(const std::string &text)
{
    Lines out;
    size_t start = 0;
    while (start < text.size())
    {
        const size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            out.lines.push_back(text.substr(start));
            return out;
        }
        out.lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    out.trailingNewline = !text.empty();
    return out;
}

std::string joinLines(const Lines &in)
{
    std::string out;
    for (size_t i = 0; i < in.lines.size(); ++i)
    {
        out += in.lines[i];
        if (i + 1 < in.lines.size() || in.trailingNewline)
            out += '\n';
    }
    return out;
}

bool isBlank(const std::string &line)
{
    return std::all_of(line.begin(), line.end(),
                       [](char c) { return c == ' ' || c == '\t' || c == '\r'; });
}

std::string describe(const ICUnit &unit)
{
    std::ostringstream out;
    out << unit;
    return out.str();
}
} // namespace

ApplyResult applySchedule(const std::string &source, const AssembledProgram &program,
                          const SchedulingResult &result)
{
    Lines original = splitLines(source);
    Lines rewritten = original;

    // How many instructions each source line (1-based) assembled to, and
    // which lines define a label.
    std::map<unsigned int, unsigned int> instructionsOnLine;
    for (const auto &[unit, _] : program.intermediate_code)
        ++instructionsOnLine[unit.getLineNumber()];
    std::set<uint64_t> labelLines;
    for (const auto &[_, symbol] : program.symbol_table)
        labelLines.insert(symbol.line_number);

    const auto lineOf = [&program](size_t programIndex)
    { return program.intermediate_code[programIndex].first.getLineNumber(); };
    const auto movable = [&](unsigned int line)
    {
        const auto it = instructionsOnLine.find(line);
        return line >= 1 && line <= original.lines.size() && it != instructionsOnLine.end() &&
               it->second == 1 && !labelLines.contains(line);
    };
    const auto onlyBlankBetween = [&](unsigned int from, unsigned int to)
    {
        for (unsigned int line = from + 1; line < to; ++line)
            if (!isBlank(original.lines[line - 1]))
                return false;
        return true;
    };

    ApplyResult out;
    for (const BasicBlockSchedule &block : result.blocks)
    {
        // A schedule that is not a permutation of its block is not applied.
        std::vector<size_t> a = block.originalInstructionIndices;
        std::vector<size_t> b = block.scheduledInstructionIndices;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        if (a != b)
            continue;

        std::map<size_t, size_t> scheduledPosition;
        for (size_t i = 0; i < block.scheduledInstructionIndices.size(); ++i)
            scheduledPosition[block.scheduledInstructionIndices[i]] = i;

        std::vector<std::vector<size_t>> segments(1);
        for (size_t programIndex : block.originalInstructionIndices)
        {
            const unsigned int line = lineOf(programIndex);
            std::vector<size_t> &current = segments.back();
            if (!movable(line))
            {
                if (!current.empty())
                    segments.emplace_back();
                continue;
            }
            if (!current.empty())
            {
                const unsigned int previous = lineOf(current.back());
                if (line <= previous || !onlyBlankBetween(previous, line))
                    segments.emplace_back();
            }
            segments.back().push_back(programIndex);
        }

        for (const std::vector<size_t> &segment : segments)
        {
            if (segment.size() < 2)
                continue;
            std::vector<size_t> order = segment;
            std::sort(order.begin(), order.end(), [&](size_t x, size_t y)
                      { return scheduledPosition.at(x) < scheduledPosition.at(y); });
            for (size_t slot = 0; slot < segment.size(); ++slot)
            {
                const unsigned int target = lineOf(segment[slot]);
                const unsigned int from   = lineOf(order[slot]);
                rewritten.lines[target - 1] = original.lines[from - 1];
                if (target != from)
                    ++out.movedLines;
            }
        }
    }

    out.text    = joinLines(rewritten);
    out.changed = out.movedLines > 0 && out.text != source;
    if (!out.changed)
    {
        out.text       = source;
        out.movedLines = 0;
    }
    return out;
}

bool sameInstructionMultiset(const AssembledProgram &a, const AssembledProgram &b)
{
    const auto collect = [](const AssembledProgram &program)
    {
        std::multiset<std::string> out;
        for (const auto &[unit, _] : program.intermediate_code)
            out.insert(describe(unit));
        return out;
    };
    return collect(a) == collect(b);
}
} // namespace Atlas
