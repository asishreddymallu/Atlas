#pragma once
// Refuses to run unless XDG_CONFIG_HOME, XDG_DATA_HOME and XDG_CACHE_HOME all
// point inside a temporary directory.
//
// Why this exists. Every probe here can reach code that drives settings, and both
// ThemeManager::setTheme() and FontManager::setSize() write through to
// QSettings. A harness run without run_hermetic.sh therefore edits the
// developer's ~/.config/Atlas/Atlas.conf. That has now happened twice: a
// font sweep left fontSize=3 and made the test suite fail, and a four-theme
// screenshot sweep left the theme on the light theme. Both were caught by a
// md5, which is after the fact.
//
// The wrapper is still the thing that CREATES the sandbox. This only refuses
// to start without one, so a forgotten wrapper is a loud exit 2 rather than a
// quiet edit of a file nobody was looking at. test/test_main.cpp takes the
// other approach - it builds its own sandbox - because ctest cannot be asked
// to run through a wrapper.
//
//   int main(int argc, char **argv)
//   {
//       if (const int bad = Atlas::Harness::requireHermeticEnvironment()) return bad;
//       ...
//   }

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Atlas
{
namespace Harness
{

/// True when `path` is a non-empty absolute path under a temp root.
inline bool underTempRoot(const char *path)
{
    if (!path || !*path)
        return false;
    const std::string p(path);
    if (p.front() != '/')
        return false;
    // The roots a sandbox can legitimately live under. TMPDIR first, so a
    // machine that moves it is still covered.
    const char *tmpdir = std::getenv("TMPDIR");
    if (tmpdir && *tmpdir == '/')
    {
        std::string root(tmpdir);
        while (root.size() > 1 && root.back() == '/')
            root.pop_back();
        if (p.compare(0, root.size() + 1, root + "/") == 0)
            return true;
    }
    for (const char *root : {"/tmp/", "/var/tmp/", "/dev/shm/"})
        if (p.compare(0, std::strlen(root), root) == 0)
            return true;
    return false;
}

/// 0 when the environment is hermetic, 2 when it is not (and says why).
inline int requireHermeticEnvironment()
{
    const char *names[] = {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME"};
    bool        ok      = true;
    for (const char *name : names)
    {
        const char *value = std::getenv(name);
        if (underTempRoot(value))
            continue;
        ok = false;
        std::fprintf(stderr, "  %s = %s\n", name,
                     (value && *value) ? value : "(unset)");
    }
    if (ok)
        return 0;
    std::fprintf(stderr,
                 "refusing to run: this harness writes QSettings through to the\n"
                 "configuration directory, and the three XDG variables above do\n"
                 "not point into a temporary directory. Without them it would\n"
                 "edit the real ~/.config/Atlas/Atlas.conf - which has happened\n"
                 "twice. Run it through:\n"
                 "    verification/lib/run_hermetic.sh <this binary> [args]\n");
    return 2;
}

} // namespace Harness
} // namespace Atlas
