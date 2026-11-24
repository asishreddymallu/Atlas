/**
 * @file version.h
 * @brief The Atlas version string. Qt-free.
 */
#pragma once

#include <string>

namespace Atlas
{
/// "1.0.0+<short commit>" when the build was configured in a git checkout,
/// otherwise "1.0.0". The number is CMake's project(VERSION); the commit is
/// `git rev-parse --short HEAD` at configure time. Tags are never read.
[[nodiscard]] std::string atlasVersion();
} // namespace Atlas
