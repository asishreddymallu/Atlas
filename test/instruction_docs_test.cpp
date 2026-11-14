/**
 * @file instruction_docs_test.cpp
 * @brief Proves resources/instructions/instruction_docs.json against the real assembler.
 *
 * No mocks and no fixtures: every example is run through the real `Lexer`
 * and `Parser`, and the pseudo-expansion check reads the intermediate
 * code the parser actually produced. The assembler is read, never
 * modified, and no example is edited to make a test pass — a failure is
 * reported with the name, the line and the parser's own message.
 *
 * The file is read from the FILESYSTEM, not from `:/instructions/...`.
 * `qt6_add_resources()` puts the compiled resources in `APP_RESOURCES`,
 * which is given to the `Kites` executable; `kites_core` does not get
 * them and `kites_test` links `kites_core`, so no `:/` path resolves in
 * this binary. `ResourcePathIsNotReachableFromTests` pins that, so the
 * day it changes, this comment is caught rather than quietly wrong.
 */
#include "assembler/code_generator.h"
#include "assembler/lexer.h"
#include "assembler/parser.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>
#include <set>

using namespace Kites;

namespace
{
constexpr int kSummaryCap = 140;

/// Where the two JSON files live, relative to test/ (CTest's working dir).
const char *kDocsPath  = "../resources/instructions/instruction_docs.json";
const char *kNamesPath = "../resources/instructions/instructions.json";

QString readFile(const char *path)
{
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

QJsonObject docsRoot()
{
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(readFile(kDocsPath).toUtf8(), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError)
        << "instruction_docs.json is not valid JSON: "
        << error.errorString().toStdString();
    return document.object();
}

QJsonObject entries() { return docsRoot().value("entries").toObject(); }

/// Every mnemonic instructions.json lists, from every category.
QStringList instructionNames()
{
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(readFile(kNamesPath).toUtf8(), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError);
    QStringList names;
    const QJsonObject root = document.object();
    for (auto it = root.begin(); it != root.end(); ++it)
        for (const QJsonValue &value : it.value().toArray())
            names << value.toString();
    return names;
}

/// What the parser said about one program, and what it produced.
struct Assembled
{
    QStringList          complaints;
    QStringList          mnemonics;   ///< the opcode of each intermediate unit
    std::vector<uint32_t> words;      ///< machine code, when it got that far
};

/// The wrapper the brief specifies, so a branch target and a data label
/// always exist.
QString wrap(const QString &example)
{
    return QStringLiteral(".data\nlabel_d: .word 0\n.text\nlabel_t:\n%1\n").arg(example);
}

Assembled assembleText(const QString &text, const QString &dir, const QString &name)
{
    const QString path = QStringLiteral("%1/%2.s").arg(dir, name);
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            Assembled bad;
            bad.complaints << QStringLiteral("could not write %1").arg(path);
            return bad;
        }
        QTextStream(&f) << text;
    }
    Assembled out;
    try
    {
        Lexer  lexer(path.toStdString());
        auto   tokens = lexer.getTokenList();
        Parser parser(path.toStdString(), tokens);
        try { parser.parse(); }
        catch (const std::exception &e)
        { out.complaints << QStringLiteral("threw: %1").arg(e.what()); }

        for (const ParseError &error : parser.getErrors())
            out.complaints << QStringLiteral("line %1: %2")
                                  .arg(error.line)
                                  .arg(QString::fromStdString(error.message));

        for (const auto &[unit, valid] : parser.getIntermediateCode())
        {
            Q_UNUSED(valid);
            out.mnemonics << QString::fromLatin1(unit.opcode.data());
        }
        if (out.complaints.isEmpty())
        {
            try { out.words = generateMachineCode(parser.getIntermediateCode()); }
            catch (const std::exception &e)
            { out.complaints << QStringLiteral("codegen threw: %1").arg(e.what()); }
        }
    }
    catch (const std::exception &e)
    { out.complaints << QStringLiteral("lexer threw: %1").arg(e.what()); }
    return out;
}

/// The operands of one assembly line: everything after the mnemonic, split
/// on commas, with a `imm(reg)` form counted as ONE operand because that
/// is how both the format strings and the parser treat it.
QStringList operandsOf(const QString &line)
{
    QString body = line.trimmed();
    const int hash = body.indexOf(QLatin1Char('#'));
    if (hash >= 0)
        body = body.left(hash).trimmed();
    const int space = body.indexOf(QRegularExpression(QStringLiteral("\\s")));
    if (space < 0)
        return {};
    QStringList parts = body.mid(space).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString &part : parts)
        part = part.trimmed();
    parts.removeAll(QString());
    return parts;
}

QString mnemonicOf(const QString &line)
{
    const QString body = line.trimmed();
    const int space = body.indexOf(QRegularExpression(QStringLiteral("\\s")));
    return space < 0 ? body : body.left(space);
}
} // namespace

// ═══════════════════════ the file itself ════════════════════════════════

TEST(InstructionDocs, TheFileIsValidJsonWithTheExpectedShape)
{
    ASSERT_FALSE(readFile(kDocsPath).isEmpty())
        << "instruction_docs.json could not be read from " << kDocsPath;
    const QJsonObject root = docsRoot();
    EXPECT_EQ(root.value("schema").toInt(), 1);
    EXPECT_TRUE(root.contains("entries"));
    EXPECT_FALSE(entries().isEmpty());
}

TEST(InstructionDocs, ResourcePathIsNotReachableFromTests)
{
    // Not a wish, a fact being pinned: qt6_add_resources() gives the
    // compiled resources to the Kites executable, kites_core does not get
    // them, and kites_test links kites_core. If this ever starts
    // resolving, the comment at the top of this file is out of date.
    QFile viaResource(QStringLiteral(":/instructions/instruction_docs.json"));
    EXPECT_FALSE(viaResource.open(QIODevice::ReadOnly))
        << "the resource system now reaches kites_test; update the file comment";
}

// ═══════════════════ (a) coverage, by name, both ways ═══════════════════

TEST(InstructionDocs, EveryInstructionHasAnEntryAndEveryEntryIsAnInstruction)
{
    const QStringList names = instructionNames();
    ASSERT_FALSE(names.isEmpty()) << "instructions.json listed nothing";
    const QJsonObject docs = entries();

    QStringList missing, extra;
    const std::set<QString> documented = [&]
    {
        std::set<QString> s;
        for (const QString &key : docs.keys())
            s.insert(key);
        return s;
    }();
    std::set<QString> listed;
    for (const QString &name : names)
    {
        listed.insert(name);
        if (!documented.count(name))
            missing << name;
    }
    for (const QString &key : docs.keys())
        if (!listed.count(key))
            extra << key;

    missing.removeDuplicates();
    missing.sort();
    extra.sort();
    EXPECT_TRUE(missing.isEmpty())
        << "in instructions.json with no entry (" << missing.size()
        << "): " << missing.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(extra.isEmpty())
        << "entries naming nothing in instructions.json (" << extra.size()
        << "): " << extra.join(QLatin1Char(' ')).toStdString();
}

// ═════════════ (b) every non-pending example really assembles ═══════════

TEST(InstructionDocs, EveryNonPendingExampleAssembles)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QJsonObject docs = entries();

    // Eleven examples do not assemble. NONE of them is a defect in the
    // JSON, and no example and no assembler source was edited to make
    // this pass - each is a gap in the assembler, reported and pinned:
    //
    //   - the nine RV64 '*w' instructions are listed in
    //     instructions.json and have encodings in instructions.cpp, but
    //     have NO entry in instruction_syntax_map, so the parser has no
    //     syntax for them at all. The empty "Expected: " in the message
    //     is operator[] on a missing key default-constructing an empty
    //     vector of syntaxes.
    //   - `fence` and `fclass.s` parse fine and then throw out of the
    //     CODE GENERATOR with "Invalid instruction type": there is no
    //     encoding path for either.
    //
    // The list is checked BOTH ways below: a name not on it that fails is
    // a regression, and a name on it that starts passing means the
    // assembler gained the instruction and the list is stale.
    const QStringList knownFailures = {
        QStringLiteral("addiw"), QStringLiteral("addw"),
        QStringLiteral("slliw"), QStringLiteral("sllw"),
        QStringLiteral("sraiw"), QStringLiteral("sraw"),
        QStringLiteral("srliw"), QStringLiteral("srlw"),
        QStringLiteral("subw"),
        QStringLiteral("fclass.s"), QStringLiteral("fence"),
    };

    int checked = 0, skipped = 0;
    QStringList failed, stillBroken, unexpectedlyFixed;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        if (entry.value("status").toString() == QLatin1String("pending"))
        { ++skipped; continue; }
        const QString example = entry.value("example").toString();
        ASSERT_FALSE(example.isEmpty()) << name.toStdString() << " has no example";
        const Assembled result =
            assembleText(wrap(example), dir.path(), QStringLiteral("ex_%1").arg(checked));
        ++checked;

        const bool broke = !result.complaints.isEmpty();
        const bool known = knownFailures.contains(name);
        if (broke && !known)
            failed << QStringLiteral("%1\n      example: %2\n      parser : %3")
                          .arg(name, example, result.complaints.join(QStringLiteral(" | ")));
        if (broke && known)
            stillBroken << QStringLiteral("%1 (%2)")
                               .arg(name, result.complaints.first().left(60));
        if (!broke && known)
            unexpectedlyFixed << name;
    }
    std::cerr << "    [ info ] examples assembled " << checked
              << ", skipped (pending) " << skipped
              << ", known gaps still failing " << stillBroken.size()
              << ", NEW failures " << failed.size() << "\n";
    for (const QString &k : stillBroken)
        std::cerr << "    [ known ] " << k.toStdString() << "\n";
    for (const QString &f : failed)
        std::cerr << "    [ FAIL  ] " << f.toStdString() << "\n";
    for (const QString &u : unexpectedlyFixed)
        std::cerr << "    [ stale ] " << u.toStdString()
                  << " now assembles; take it off the known-failures list\n";

    EXPECT_GT(checked, 0) << "no example was assembled at all";
    EXPECT_TRUE(failed.isEmpty())
        << failed.size() << " example(s) did not assemble; names above";
    EXPECT_TRUE(unexpectedlyFixed.isEmpty())
        << "the known-failures list is stale: "
        << unexpectedlyFixed.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(stillBroken.size(), knownFailures.size())
        << "the known-failures list and what actually fails have drifted apart";
}

// ════════════════════ (c) format matches example ════════════════════════

TEST(InstructionDocs, FormatAndExampleAgreeOnTheMnemonicAndOperandCount)
{
    const QJsonObject docs = entries();
    QStringList wrong;
    int checked = 0;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry  = docs.value(name).toObject();
        const QString    format  = entry.value("format").toString();
        const QString    example = entry.value("example").toString();
        if (format.isEmpty() || example.isEmpty())
            continue;
        ++checked;

        if (mnemonicOf(format) != name)
            wrong << QStringLiteral("%1: format starts with '%2'").arg(name, mnemonicOf(format));
        if (mnemonicOf(example) != name)
            wrong << QStringLiteral("%1: example starts with '%2'").arg(name, mnemonicOf(example));

        const int formatOperands  = int(operandsOf(format).size());
        const int exampleOperands = int(operandsOf(example).size());
        if (formatOperands != exampleOperands)
            wrong << QStringLiteral("%1: format has %2 operand(s), example has %3  "
                                    "[format: %4] [example: %5]")
                         .arg(name)
                         .arg(formatOperands)
                         .arg(exampleOperands)
                         .arg(format, example);
    }
    std::cerr << "    [ info ] format/example pairs checked " << checked << "\n";
    for (const QString &w : wrong)
        std::cerr << "    [ FAIL  ] " << w.toStdString() << "\n";
    EXPECT_GT(checked, 0);
    EXPECT_TRUE(wrong.isEmpty()) << wrong.size() << " mismatch(es); names above";
}

// ═══════════════ (d) summaries, and no empty fields ═════════════════════

TEST(InstructionDocs, SummariesAreShortAndNoFieldIsEmpty)
{
    const QJsonObject docs = entries();
    QStringList tooLong, empty;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        for (const QString &key : entry.keys())
        {
            const QJsonValue value = entry.value(key);
            if (value.isNull())
                continue; // aliasOfProposed is legitimately null
            if (value.isString() && value.toString().trimmed().isEmpty())
                empty << QStringLiteral("%1.%2").arg(name, key);
            if (value.isArray() && value.toArray().isEmpty())
                empty << QStringLiteral("%1.%2").arg(name, key);
        }
        const QString summary = entry.value("summary").toString();
        if (summary.size() > kSummaryCap)
            tooLong << QStringLiteral("%1 (%2 chars)").arg(name).arg(summary.size());
    }
    for (const QString &t : tooLong)
        std::cerr << "    [ FAIL  ] summary over " << kSummaryCap << ": " << t.toStdString() << "\n";
    for (const QString &e : empty)
        std::cerr << "    [ FAIL  ] empty field: " << e.toStdString() << "\n";
    EXPECT_TRUE(tooLong.isEmpty());
    EXPECT_TRUE(empty.isEmpty());
}

// ═════════ (e) pseudo expansions, reported and not enforced ═════════════

TEST(InstructionDocs, PseudoExpansionsAreComparedWithTheAssembler)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QJsonObject docs = entries();

    int compared = 0;
    QStringList agree, differ, unassembled;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        if (!entry.contains("expandsTo"))
            continue;
        if (entry.value("status").toString() == QLatin1String("pending"))
            continue;

        // `expandsTo` is a STRING, with ';' between the steps of a
        // multi-instruction expansion: "auipc rd, hi; addi rd, rd, lo".
        // An earlier version read it as an array and got an empty list for
        // all 25, which made every comparison "differ" for the same wrong
        // reason - the sort of uniform result that is a bug, not a finding.
        QStringList claimed;
        for (const QString &step :
             entry.value("expandsTo").toString().split(QLatin1Char(';'), Qt::SkipEmptyParts))
            claimed << mnemonicOf(step.trimmed());

        const Assembled result = assembleText(
            wrap(entry.value("example").toString()), dir.path(),
            QStringLiteral("ps_%1").arg(compared));
        ++compared;
        if (!result.complaints.isEmpty())
        { unassembled << name; continue; }

        if (result.mnemonics == claimed)
            agree << name;
        else
            differ << QStringLiteral("%1: documented [%2], assembler produced [%3]")
                          .arg(name, claimed.join(QLatin1Char(' ')),
                               result.mnemonics.join(QLatin1Char(' ')));
    }
    std::cerr << "    [ info ] expansions compared " << compared
              << ", agree " << agree.size()
              << ", differ " << differ.size()
              << ", not assembled " << unassembled.size() << "\n";
    for (const QString &d : differ)
        std::cerr << "    [ differ ] " << d.toStdString() << "\n";
    for (const QString &u : unassembled)
        std::cerr << "    [ nobuild] " << u.toStdString() << "\n";

    // REPORTED, not enforced: the brief asks for mismatches to be told,
    // not fixed. The only hard requirement is that the comparison ran.
    EXPECT_GT(compared, 0) << "no expandsTo entry was compared";
}
