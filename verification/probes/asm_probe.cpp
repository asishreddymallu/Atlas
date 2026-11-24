// atlas_verify_asm: the assembler corpus check's runner (asm/dump_all.sh).
//
//   atlas_verify_asm code <file>     machine words, data items and symbols
//   atlas_verify_asm tokens <file>   the lexer's tokens: type, value, line, column
//   atlas_verify_asm errors <file>   the parser's errors: line, columns, message
//
// The output is compared byte for byte with an earlier run, so a change to
// the assembler that alters any program in the corpus is noticed.
// Run it under verification/lib/run_hermetic.sh (it refuses otherwise).
#include "assembler/assembler.h"
#include "assembler/lexer.h"
#include "assembler/parser.h"
#include "assembler/tokens.h"
#include "common/assembled_program.h"
#include "hermetic_guard.h"

#include <QCoreApplication>

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
int dumpCode(const char *file)
{
    try
    {
        const Atlas::AssembledProgram p = Atlas::assemble(file);
        std::cout << "TEXT " << p.text_buffer.size() << "\n";
        for (const uint32_t w : p.text_buffer)
            std::cout << "  " << std::hex << std::setw(8) << std::setfill('0') << w << "\n";
        std::cout << std::dec << "DATA " << p.data_buffer.size() << "\n";
        for (const auto &d : p.data_buffer)
        {
            std::ostringstream os;
            std::visit([&os](auto &&v) { os << v; }, d);
            std::cout << "  " << os.str() << "\n";
        }
        std::cout << "SYMBOLS " << p.symbol_table.size() << "\n";
        for (const auto &[n, s] : p.symbol_table)
            std::cout << "  " << n << " = " << s.address << "\n";
    }
    catch (const std::exception &e)
    {
        std::cout << "ASSEMBLY FAILED: " << e.what() << "\n";
    }
    return 0;
}

int dumpTokens(const char *file)
{
    try
    {
        Atlas::Lexer l(file);
        for (const auto &t : l.getTokenList())
            std::cout << static_cast<int>(t.type) << '\t' << t.value << '\t' << t.line_number << '\t'
                      << t.column_number << '\n';
    }
    catch (const std::exception &e)
    {
        std::cout << "LEX FAILED: " << e.what() << '\n';
    }
    return 0;
}

int dumpErrors(const char *file)
{
    Atlas::Lexer  l(file);
    auto          tokens = l.getTokenList();
    Atlas::Parser p(file, tokens);
    try
    {
        p.parse();
    }
    catch (const std::exception &)
    {
    }
    for (const auto &e : p.getErrors())
        std::cout << "line=" << e.line << " col=" << e.column << " end=" << e.end_column << " msg=\""
                  << e.message << "\"\n";
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    if (const int notHermetic = Atlas::Harness::requireHermeticEnvironment())
        return notHermetic;
    QCoreApplication app(argc, argv);
    const std::string mode = argc > 2 ? argv[1] : "";
    if (mode == "code")
        return dumpCode(argv[2]);
    if (mode == "tokens")
        return dumpTokens(argv[2]);
    if (mode == "errors")
        return dumpErrors(argv[2]);
    std::fprintf(stderr, "usage: atlas_verify_asm code|tokens|errors <file>\n");
    return 2;
}
