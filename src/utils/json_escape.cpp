#include "utils/json_escape.h"

#include <cstdio>

namespace Atlas
{
/// Escapes a string for embedding in a JSON string literal: backslash, double
/// quote, the named control escapes, and anything else below 0x20 as \uXXXX.
/// Without this a message containing a quote produced invalid JSON that the
/// editor then silently failed to read.
std::string escapeJsonString(const std::string &text)
{
    std::string escaped;
    escaped.reserve(text.size());
    for (const unsigned char c : text)
    {
        switch (c)
        {
        case '\\': escaped += "\\\\"; break;
        case '"':  escaped += "\\\""; break;
        case '\b': escaped += "\\b";  break;
        case '\f': escaped += "\\f";  break;
        case '\n': escaped += "\\n";  break;
        case '\r': escaped += "\\r";  break;
        case '\t': escaped += "\\t";  break;
        default:
            if (c < 0x20)
            {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                escaped += buffer;
            }
            else
            {
                escaped += static_cast<char>(c);
            }
            break;
        }
    }
    return escaped;
}
} // namespace Atlas
