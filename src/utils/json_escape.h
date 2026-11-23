/**
 * @file json_escape.h
 * @brief The repo's JSON string escaping, Qt-free. Used by the vm_state
 *        dumps (utils.cpp) and the cache lab record (cache_lab/lab_record.cpp).
 */
#pragma once

#include <string>

namespace Atlas
{
/// Escapes a string for embedding in a JSON string literal: backslash, double
/// quote, the named control escapes, and anything else below 0x20 as \uXXXX.
std::string escapeJsonString(const std::string &text);
} // namespace Atlas
