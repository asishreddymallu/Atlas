// atlas_verify_cacheprobe: the L1 cache sweep's runner (cache/run_all.py). usage:
//   atlas_verify_cacheprobe <model> <abs .s> <sets,ways,line,policy,wb,wa> [lua script]
// policy: LRU|FIFO|Custom. I$ and L2 are fixed (see below). Prints one line per level:
//   <level> hits misses evictions(counter or -1) evictions(tag-diff) writebacks
// and a final 'end' line with x8, steps.
#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"
#include "hermetic_guard.h"
#include <QApplication>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
using namespace Atlas;

// cache.h gains getEvictionCount() in E8; detect it so the probe also builds at the base.
template <class C> auto evc(C *c, int) -> decltype((long long)c->getEvictionCount())
{ return (long long)c->getEvictionCount(); }
template <class C> long long evc(C *, long) { return -1; }

struct Snap { std::vector<std::pair<bool, uint64_t>> w; };
static Snap snap(Cache *c)
{
    Snap s;
    for (size_t i = 0; i < c->getSetCount(); ++i)
        for (size_t j = 0; j < c->getWayCount(); ++j)
        {
            auto &l = c->getCacheLine(i, j);
            s.w.push_back({l.valid, l.tag});
        }
    return s;
}
static long long diffEv(const Snap &a, const Snap &b)
{
    long long n = 0;
    for (size_t i = 0; i < a.w.size(); ++i)
        if (a.w[i].first && (!b.w[i].first || b.w[i].second != a.w[i].second))
            ++n;
    return n;
}

static CacheConfig parse(const std::string &spec)
{
    std::stringstream ss(spec);
    std::string t;
    std::vector<std::string> f;
    while (std::getline(ss, t, ','))
        f.push_back(t);
    CacheConfig c{};
    c.lineCount = std::stoul(f[0]); // NB: holds the SET count (known_issues, E8)
    c.wayCount = std::stoul(f[1]);
    c.lineSizeInBytes = std::stoul(f[2]);
    c.replacementPolicy = f[3] == "LRU" ? ReplacementPolicy::LRU
                        : f[3] == "FIFO" ? ReplacementPolicy::FIFO : ReplacementPolicy::Custom;
    c.writePolicy = f[4] == "1" ? WritePolicy::WriteBack : WritePolicy::WriteThrough;
    c.allocationPolicy = f[5] == "1" ? AllocationPolicy::WriteAllocate : AllocationPolicy::NoWriteAllocate;
    return c;
}

int main(int argc, char **argv)
{
    if (const int notHermetic = Atlas::Harness::requireHermeticEnvironment())
        return notHermetic;
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::string m = argv[1];
    setupVmStateDirectory();
    auto prog = assemble(argv[2]);
    CacheConfig l1 = parse(argv[3]);
    std::string lua = argc > 4 ? argv[4] : "";
    std::unique_ptr<ProcessorBase> p;
    if (m == "rvss") p = std::make_unique<RVSSProcessor>();
    else if (m == "nh-nf") p = std::make_unique<RV5StageProcessorNHNF>();
    else if (m == "nh-f") p = std::make_unique<RV5StageProcessorNHF>();
    else if (m == "h-nf") p = std::make_unique<RV5StageProcessorHNF>();
    else if (m == "h-f") p = std::make_unique<RV5StageProcessorHF>();
    else return 2;
    auto &mc = p->memory_controller_;
    // Fixed: I$ 64 sets x 4 ways x 16 B LRU; L2 32 sets x 4 ways x 64 B LRU (WT, NWA defaults).
    mc.getInstructionCache()->reconfigure({64, 16, 4, WritePolicy::WriteThrough,
                                           AllocationPolicy::NoWriteAllocate, ReplacementPolicy::LRU});
    mc.getL2Cache()->reconfigure({32, 64, 4, WritePolicy::WriteThrough,
                                  AllocationPolicy::NoWriteAllocate, ReplacementPolicy::LRU});
    if (!lua.empty())
        mc.getL1Cache()->loadCustomPolicyScript(lua);
    mc.getL1Cache()->reconfigure(l1);
    p->LoadProgram(prog);
    p->SetBreakpoints({});
    Cache *cs[3] = {mc.getL1Cache(), mc.getInstructionCache(), mc.getL2Cache()};
    const char *names[3] = {"L1D", "I", "L2"};
    long long diff[3] = {0, 0, 0};
    long long steps = 0;
    for (;;)
    {
        Snap before[3] = {snap(cs[0]), snap(cs[1]), snap(cs[2])};
        bool more = p->StepAndNotify();
        ++steps;
        for (int i = 0; i < 3; ++i)
            diff[i] += diffEv(before[i], snap(cs[i]));
        if (!more || steps > 2000000)
            break;
    }
    for (int i = 0; i < 3; ++i)
        std::printf("%s %zu %zu %lld %lld %zu\n", names[i], cs[i]->getHitCount(), cs[i]->getMissCount(),
                    evc(cs[i], 0), diff[i], cs[i]->getWriteBackCount());
    std::printf("end x8=%lld steps=%lld pol=%d\n", (long long)p->registers_.ReadGpr(8), steps,
                (int)cs[0]->getConfig().replacementPolicy);
    return 0;
}
