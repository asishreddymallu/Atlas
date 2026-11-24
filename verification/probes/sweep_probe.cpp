// atlas_verify_sweep: the runner of the ISA and hazard sweeps (isa/sweep.py,
// hazard/hazgen.py) and of tables/li_audit.py.
//
//   atlas_verify_sweep list
//       every mnemonic in instruction_set::instruction_syntax_map, with its
//       SyntaxType values - the sweep's instruction list comes from the
//       assembler's own table, not from a hand-written one.
//
//   atlas_verify_sweep run <model> <batch-file> <out-file>
//       <batch-file> holds programs, each introduced by a line "### <id>".
//       Each is assembled and run on a fresh processor of <model> through
//       StepAndNotify() until it reports the end or 20000 steps pass. One
//       line per program is written to <out-file>:
//         <id> OK pc=<hex> steps=<n> cycles=<n> x0..x31 f0..f31 fcsr mem[0..31]
//         <id> ERR <message>
//       mem is the 32 dwords from kScratch, read without cache effects.
//
// Run it under verification/lib/run_hermetic.sh (it refuses otherwise).
#include "assembler/assembler.h"
#include "common/instructions.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"
#include "hermetic_guard.h"
#include <QApplication>
#include <QTemporaryDir>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
using namespace Atlas;

constexpr uint64_t kScratch = 0x10000000ULL + 0x400;

static std::unique_ptr<ProcessorBase> make(const std::string &m)
{
    if (m == "rvss") return std::make_unique<RVSSProcessor>();
    if (m == "nh-nf") return std::make_unique<RV5StageProcessorNHNF>();
    if (m == "nh-f") return std::make_unique<RV5StageProcessorNHF>();
    if (m == "h-nf") return std::make_unique<RV5StageProcessorHNF>();
    if (m == "h-f") return std::make_unique<RV5StageProcessorHF>();
    return nullptr;
}

int main(int argc, char **argv)
{
    if (const int notHermetic = Atlas::Harness::requireHermeticEnvironment())
        return notHermetic;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "list")
    {
        std::map<std::string, std::vector<instruction_set::SyntaxType>> sorted(
            instruction_set::instruction_syntax_map.begin(),
            instruction_set::instruction_syntax_map.end());
        for (const auto &[name, syntaxes] : sorted)
        {
            std::printf("%s", name.c_str());
            for (auto s : syntaxes)
                std::printf(" %d", static_cast<int>(s));
            std::printf("\n");
        }
        return 0;
    }
    if (mode == "names")
    {
        // the encoder's own table, for the mnemonics the syntax map misses
        std::map<std::string, int> sorted;
        for (const auto &[name, ins] : instruction_set::instruction_string_map)
            sorted[name] = static_cast<int>(ins);
        for (const auto &[name, ins] : sorted)
            std::printf("%s\n", name.c_str());
        return 0;
    }
    if (mode == "words" && argc >= 4)
    {
        // each program's encoded instruction words, for an emulator elsewhere
        std::ifstream in(argv[2]);
        std::ofstream out(argv[3]);
        QTemporaryDir dir;
        std::string line, id, text;
        auto flush = [&]()
        {
            if (id.empty())
                return;
            const std::string path = dir.path().toStdString() + "/w.s";
            {
                std::ofstream f(path);
                f << text;
            }
            try
            {
                const auto words = assemble(path).text_buffer;
                out << id << " OK";
                for (uint32_t w : words)
                {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, " %08x", w);
                    out << buf;
                }
                out << "\n";
            }
            catch (const std::exception &)
            {
                out << id << " ERR\n";
            }
            id.clear();
            text.clear();
        };
        while (std::getline(in, line))
        {
            if (line.rfind("### ", 0) == 0)
            {
                flush();
                id = line.substr(4);
                continue;
            }
            text += line + "\n";
        }
        flush();
        return 0;
    }
    if (mode != "run" || argc < 5)
    {
        std::fprintf(stderr, "usage: atlas_verify_sweep list | names | words <batch> <out> | run <model> <batch> <out>\n");
        return 2;
    }
    const std::string model = argv[2];
    std::ifstream in(argv[3]);
    std::ofstream out(argv[4]);
    QTemporaryDir dir;
    setupVmStateDirectory();
    std::ostringstream sink; // DebugRun/Step traces would swamp stdout

    std::string line, id, text;
    auto flush = [&]()
    {
        if (id.empty())
            return;
        const std::string path = dir.path().toStdString() + "/p.s";
        {
            std::ofstream f(path);
            f << text;
        }
        auto p = make(model);
        std::streambuf *old = std::cout.rdbuf(sink.rdbuf());
        std::streambuf *olderr = std::cerr.rdbuf(sink.rdbuf());
        std::string err;
        int steps = 0;
        try
        {
            p->LoadProgram(assemble(path));
            p->SetBreakpoints({});
            while (steps < 20000 && p->StepAndNotify())
                ++steps;
        }
        catch (const std::exception &e)
        {
            err = e.what();
        }
        std::cout.rdbuf(old);
        std::cerr.rdbuf(olderr);
        sink.str("");
        if (!err.empty())
        {
            for (char &c : err)
                if (c == '\n')
                    c = ' ';
            out << id << " ERR " << err << "\n";
        }
        else
        {
            char buf[64];
            out << id << " OK pc=" << std::hex << p->program_counter_ << std::dec
                << " steps=" << steps << " cycles=" << p->cycle_s_;
            for (int i = 0; i < 32; ++i)
            {
                std::snprintf(buf, sizeof buf, " %llx", (unsigned long long)p->registers_.ReadGpr(i));
                out << buf;
            }
            for (int i = 0; i < 32; ++i)
            {
                std::snprintf(buf, sizeof buf, " %llx", (unsigned long long)p->registers_.ReadFpr(i));
                out << buf;
            }
            std::snprintf(buf, sizeof buf, " %llx", (unsigned long long)p->registers_.ReadCsr(0x003));
            out << buf;
            for (int i = 0; i < 32; ++i)
            {
                std::snprintf(buf, sizeof buf, " %llx",
                              (unsigned long long)p->memory_controller_.readDoubleWord_d(kScratch + 8 * i));
                out << buf;
            }
            out << "\n";
        }
        id.clear();
        text.clear();
    };
    while (std::getline(in, line))
    {
        if (line.rfind("### ", 0) == 0)
        {
            flush();
            id = line.substr(4);
            continue;
        }
        text += line + "\n";
    }
    flush();
    return 0;
}
