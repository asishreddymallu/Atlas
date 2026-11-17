/**
 * @file float_literal_test.cpp
 * @brief Pins `.float`/`.double` literals to their correctly-rounded IEEE bits.
 *
 * known_issues 58. Every expected bit pattern in this file was computed
 * OUTSIDE the code under test, in Python, two independent ways that agree:
 * `struct.pack`, and an exact Fraction/Decimal round-half-to-even
 * converter. The two disagree on exactly one literal here -
 * `6748.838134765625000000000000001` - and that is the point of
 * RoundingToFloatComesFromTheDecimalNotFromADouble: `struct.pack("<f")`
 * narrows a double and double-rounds, the exact converter does not.
 *
 * The path under test is the real one: `assemble(path)` - Lexer, Parser,
 * `getDataBuffer()` - the same call test/rv5s_f_extension_test.cpp uses,
 * then the bits are read out of `AssembledProgram::data_buffer`, which
 * `ProcessorBase::LoadProgram` memcpys into memory unchanged.
 */
#include "assembler/assembler.h"
#include "common/assembled_program.h"
#include "config/config.h"
#include "processor/rvss/rvss_processor.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include <clocale>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
struct DoubleCase
{
    const char *literal;
    uint64_t    bits;
};
struct FloatCase
{
    const char *literal;
    uint32_t    bits;
};

/// Hex, so a failure message is readable as a bit pattern.
std::string hex(uint64_t value, int digits)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "0x%0*llX", digits, (unsigned long long)value);
    return buffer;
}

/// Assembles one `.double`/`.float` directive and returns the whole program.
/// Writes a real file because assemble(path) is what the app calls, and the
/// stream overload reports parse errors as "Could not open the file".
AssembledProgram assembleDirective(const QTemporaryDir &dir, const char *directive,
                                   const std::string &literal, int serial)
{
    const std::string path =
        dir.path().toStdString() + "/fl" + std::to_string(serial) + ".s";
    {
        std::ofstream out(path);
        out << ".data\nvalue: ." << directive << " " << literal << "\n.text\nnop\n";
    }
    setupVmStateDirectory();
    return assemble(path);
}

/// The first floating-point entry in the data buffer, as raw bits.
/// `found` stays false when the directive produced no data at all.
uint64_t firstFloatingBits(const AssembledProgram &program, bool *found)
{
    *found = false;
    for (const auto &entry : program.data_buffer)
    {
        if (std::holds_alternative<float>(entry))
        {
            const float value = std::get<float>(entry);
            uint32_t    bits  = 0;
            std::memcpy(&bits, &value, sizeof bits);
            *found = true;
            return bits;
        }
        if (std::holds_alternative<double>(entry))
        {
            const double value = std::get<double>(entry);
            uint64_t     bits  = 0;
            std::memcpy(&bits, &value, sizeof bits);
            *found = true;
            return bits;
        }
    }
    return 0;
}

// ── the expected bits ──────────────────────────────────────────────────
//
// 0.1 and 0.5 are the two issue 58 names; 1e-7 and 1e-30 are the ones it
// does not - they come out as ZERO today, because %f with six decimals is
// "0.000000". 5e-324 and 1e-320 are subnormal doubles. -0.0 pins the sign.

const DoubleCase kDoubleCases[] = {
    {"0.1", 0x3FB999999999999AULL},
    {"0.5", 0x3FE0000000000000ULL},
    {"1e0", 0x3FF0000000000000ULL},
    {"-0.0", 0x8000000000000000ULL},
    {"3.14159265358979", 0x400921FB54442D11ULL},
    {"2.718281828459045", 0x4005BF0A8B145769ULL},
    {"1e-7", 0x3E7AD7F29ABCAF48ULL},
    {"1e-30", 0x39B4484BFEEBC2A0ULL},
    {"1e300", 0x7E37E43C8800759CULL},
    {"123456789.123456789", 0x419D6F34547E6B75ULL},
    {"5e-324", 0x0000000000000001ULL},
    {"1e-320", 0x00000000000007E8ULL},
    {"6748.838134765625000000000000001", 0x40BA5CD690000000ULL},
};

const FloatCase kFloatCases[] = {
    {"0.1", 0x3DCCCCCDU},
    {"0.5", 0x3F000000U},
    {"1e0", 0x3F800000U},
    {"-0.0", 0x80000000U},
    {"3.14159265358979", 0x40490FDBU},
    {"2.718281828459045", 0x402DF854U},
    {"1e-7", 0x33D6BF95U},
    {"1e-30", 0x0DA24260U},
    {"123456789.123456789", 0x4CEB79A3U},
    {"1.1754943508222875e-38", 0x00800000U}, // the smallest normal float
    {"1.4e-45", 0x00000001U},                // the smallest subnormal float
};
} // namespace

TEST(FloatLiteral, DoubleDirectiveStoresTheCorrectlyRoundedIeeeBits)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    int serial = 0;
    for (const DoubleCase &c : kDoubleCases)
    {
        AssembledProgram program;
        try
        {
            program = assembleDirective(dir, "double", c.literal, serial++);
        }
        catch (const std::exception &e)
        {
            ADD_FAILURE() << ".double " << c.literal << " did not assemble: " << e.what()
                          << "  (expected " << hex(c.bits, 16) << ")";
            continue;
        }
        bool           found = false;
        const uint64_t bits  = firstFloatingBits(program, &found);
        if (!found)
        {
            ADD_FAILURE() << ".double " << c.literal << " emitted no floating-point data";
            continue;
        }
        EXPECT_EQ(bits, c.bits) << ".double " << c.literal << ": stored " << hex(bits, 16)
                                << ", correctly rounded is " << hex(c.bits, 16);
    }
}

TEST(FloatLiteral, FloatDirectiveStoresTheCorrectlyRoundedIeeeBits)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    int serial = 0;
    for (const FloatCase &c : kFloatCases)
    {
        AssembledProgram program;
        try
        {
            program = assembleDirective(dir, "float", c.literal, serial++);
        }
        catch (const std::exception &e)
        {
            ADD_FAILURE() << ".float " << c.literal << " did not assemble: " << e.what()
                          << "  (expected " << hex(c.bits, 8) << ")";
            continue;
        }
        bool           found = false;
        const uint64_t bits  = firstFloatingBits(program, &found);
        if (!found)
        {
            ADD_FAILURE() << ".float " << c.literal << " emitted no floating-point data";
            continue;
        }
        EXPECT_EQ(uint32_t(bits), c.bits)
            << ".float " << c.literal << ": stored " << hex(bits, 8) << ", correctly rounded is "
            << hex(c.bits, 8);
    }
}

TEST(FloatLiteral, RoundingToFloatComesFromTheDecimalNotFromADouble)
{
    // 6748.838134765625 is exactly halfway between two floats, and this
    // literal is a hair ABOVE it. Rounded once, from the decimal, it goes
    // up: 0x45D2E6B5. Rounded to a double first - which swallows the hair -
    // and then narrowed, it ties to even and goes DOWN: 0x45D2E6B4. Python's
    // struct.pack("<f", float(s)) produces the second one; the exact
    // Fraction converter produces the first. strtof agrees with the first.
    //
    // This one is GREEN before the fix as well as after - the broken path
    // reaches 0x45D2E6B5 by luck, through "6748.838135". It is here to
    // guard the fix's own implementation choice: implementing the .float
    // arm as static_cast<float>(from_chars<double>) turns it red, which is
    // control 4 in the report.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const char *literal = "6748.838134765625000000000000001";

    AssembledProgram program;
    ASSERT_NO_THROW(program = assembleDirective(dir, "float", literal, 0));
    bool           found = false;
    const uint64_t bits  = firstFloatingBits(program, &found);
    ASSERT_TRUE(found) << ".float " << literal << " emitted no floating-point data";
    EXPECT_EQ(uint32_t(bits), 0x45D2E6B5U)
        << ".float " << literal << ": stored " << hex(bits, 8)
        << ". 0x45D2E6B4 means the decimal was rounded to a double first and then "
           "narrowed - two roundings where there must be one";
}

TEST(FloatLiteral, ParsingDoesNotDependOnTheCLocale)
{
    // QApplication's constructor calls setlocale(LC_ALL, ""), so the C
    // locale in a running Kites is the USER's, not "C". Under a
    // comma-decimal locale a printf/scanf-based conversion reads 0.1 as 0.
    const char *const previous = std::setlocale(LC_NUMERIC, nullptr);
    const std::string saved    = previous ? previous : "C";
    const char *const applied  = std::setlocale(LC_NUMERIC, "de_DE.utf8");
    if (applied == nullptr)
    {
        std::cerr << "    [ info ] de_DE.utf8 is not installed; locale check skipped\n";
        GTEST_SKIP() << "de_DE.utf8 not available";
    }
    std::cerr << "    [ info ] LC_NUMERIC for this test: " << applied << "\n";

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    uint64_t doubleBits = 0, floatBits = 0;
    bool     haveDouble = false, haveFloat = false;
    try
    {
        doubleBits = firstFloatingBits(assembleDirective(dir, "double", "0.1", 0), &haveDouble);
        floatBits  = firstFloatingBits(assembleDirective(dir, "float", "0.1", 1), &haveFloat);
    }
    catch (const std::exception &e)
    {
        ADD_FAILURE() << "0.1 did not assemble under a comma-decimal locale: " << e.what();
    }
    std::setlocale(LC_NUMERIC, saved.c_str());

    EXPECT_TRUE(haveDouble) << ".double 0.1 emitted no data under de_DE.utf8";
    EXPECT_TRUE(haveFloat) << ".float 0.1 emitted no data under de_DE.utf8";
    EXPECT_EQ(doubleBits, 0x3FB999999999999AULL)
        << ".double 0.1 under de_DE.utf8 stored " << hex(doubleBits, 16)
        << "; 0x0000000000000000 means the conversion followed the locale's comma";
    EXPECT_EQ(uint32_t(floatBits), 0x3DCCCCCDU)
        << ".float 0.1 under de_DE.utf8 stored " << hex(floatBits, 8);
}

TEST(FloatLiteral, AnOutOfRangeLiteralIsReportedNotThrown)
{
    // std::stod throws std::out_of_range, and the GUI turns that into a
    // message box whose entire text is "stod". An out-of-range literal is a
    // mistake in the program and must be reported as one: assembly fails,
    // with a located diagnostic, through the normal error path.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::pair<const char *, const char *> cases[] = {
        {"float", "1e300"},   // finite, but far outside a 32-bit float
        {"float", "5e-324"},  // a valid double subnormal, not a float
        {"double", "1e400"},  // outside a 64-bit double
        {"double", "1e-400"}, // underflows past the smallest subnormal
    };
    for (const auto &[directive, literal] : cases)
    {
        try
        {
            assembleDirective(dir, directive, literal, 0);
            ADD_FAILURE() << "." << directive << " " << literal
                          << " assembled; an out-of-range literal must be rejected";
        }
        catch (const std::out_of_range &e)
        {
            ADD_FAILURE() << "." << directive << " " << literal
                          << " threw std::out_of_range(\"" << e.what()
                          << "\") out of assemble(); it must be a reported parse error";
        }
        catch (const std::runtime_error &)
        {
            SUCCEED(); // "Failed to parse file": the normal error path
        }
    }
}

TEST(FloatLiteral, TheBitsReachProcessorMemoryUnchanged)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::string path = dir.path().toStdString() + "/mem.s";
    {
        std::ofstream out(path);
        // Deliberately two literals the six-decimal round trip gets
        // wrong: 1e-7 becomes 0.000000 and so stores zero, and pi loses
        // its last ulp. 0.1 would prove nothing here - it survives the
        // broken path, because "0.100000" and "0.1" are the same decimal.
        out << ".data\nd: .double 1e-7\nf: .float 3.14159265358979\n.text\nnop\n";
    }
    setupVmStateDirectory();
    AssembledProgram program;
    ASSERT_NO_THROW(program = assemble(path));

    RVSSProcessor processor;
    processor.LoadProgram(program);
    const uint64_t base = vm_config::config.getDataSectionStart();
    EXPECT_EQ(processor.memory_controller_.readDoubleWord_d(base), 0x3E7AD7F29ABCAF48ULL)
        << ".double 1e-7 in memory at the data section start";
    EXPECT_EQ(processor.memory_controller_.readWord_d(base + 8), 0x40490FDBU)
        << ".float 3.14159265358979 in memory at the data section start + 8";
}
