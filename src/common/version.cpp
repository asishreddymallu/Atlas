#include "common/version.h"

// Given to this file only by CMakeLists.txt, so a new commit rebuilds one file.
#ifndef ATLAS_VERSION
#define ATLAS_VERSION "unknown"
#endif

namespace Atlas
{
std::string atlasVersion()
{
    return ATLAS_VERSION;
}
} // namespace Atlas
