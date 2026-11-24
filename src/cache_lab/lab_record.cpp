#include "cache_lab/lab_record.h"

#include "utils/json_escape.h"

#include <iomanip>
#include <sstream>

namespace Atlas
{
namespace
{
std::string quoted(const std::string &text)
{
    return "\"" + escapeJsonString(text) + "\"";
}

std::string hitRate(uint64_t hits, uint64_t accesses)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(6)
        << (accesses == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(accesses));
    return out.str();
}
} // namespace

std::string buildLabRecordJson(const LabRecordInput &in)
{
    std::ostringstream j;
    j << "{\n";
    j << "  \"schema\": " << quoted(std::string(kLabRecordSchema)) << ",\n";
    j << "  \"atlasVersion\": " << quoted(in.atlasVersion) << ",\n";
    j << "  \"timestamp\": " << quoted(in.timestamp) << ",\n";
    j << "  \"program\": {\n";
    j << "    \"name\": " << quoted(in.programName) << ",\n";
    j << "    \"sha256\": " << quoted(in.programSha256) << "\n";
    j << "  },\n";
    j << "  \"processor\": " << quoted(in.processor) << ",\n";
    j << "  \"preset\": " << (in.preset ? quoted(*in.preset) : std::string("null")) << ",\n";
    j << "  \"caches\": [";
    for (size_t i = 0; i < in.caches.size(); ++i)
    {
        const LabRecordCache &c   = in.caches[i];
        const uint64_t accesses   = c.hits + c.misses;
        j << (i == 0 ? "\n" : ",\n");
        j << "    {\n";
        j << "      \"level\": " << quoted(c.level) << ",\n";
        j << "      \"geometry\": {\n";
        j << "        \"setsExponent\": " << c.setsExponent << ",\n";
        j << "        \"waysExponent\": " << c.waysExponent << ",\n";
        j << "        \"wordsPerLineExponent\": " << c.wordsExponent << ",\n";
        j << "        \"sets\": " << c.sets << ",\n";
        j << "        \"ways\": " << c.ways << ",\n";
        j << "        \"lineBytes\": " << c.lineBytes << ",\n";
        j << "        \"totalBytes\": " << c.totalBytes << "\n";
        j << "      },\n";
        j << "      \"policies\": {\n";
        j << "        \"writeHit\": " << quoted(c.writeHit) << ",\n";
        j << "        \"writeMiss\": " << quoted(c.writeMiss) << ",\n";
        j << "        \"replacement\": " << quoted(c.replacement) << "\n";
        j << "      },\n";
        j << "      \"results\": {\n";
        j << "        \"accesses\": " << accesses << ",\n";
        j << "        \"hits\": " << c.hits << ",\n";
        j << "        \"misses\": " << c.misses << ",\n";
        j << "        \"evictions\": " << c.evictions << ",\n";
        j << "        \"writeBacks\": " << c.writeBacks << ",\n";
        j << "        \"hitRate\": " << hitRate(c.hits, accesses) << "\n";
        j << "      }\n";
        j << "    }";
    }
    j << (in.caches.empty() ? "],\n" : "\n  ],\n");
    j << "  \"cycles\": " << in.cycles << ",\n";
    j << "  \"instructionsRetired\": " << in.instructionsRetired << ",\n";
    j << "  \"completed\": " << (in.completed ? "true" : "false") << "\n";
    j << "}\n";
    return j.str();
}
} // namespace Atlas
