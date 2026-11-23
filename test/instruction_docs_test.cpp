/**
 * @file instruction_docs_test.cpp
 * @brief Proves resources/instructions/instruction_docs.json against the real assembler.
 *
 * No mocks and no fixtures. Every example runs through the real `Lexer`,
 * `Parser` and code generator, and the pseudo-expansion check reads the
 * intermediate code the parser actually produced. The assembler is read,
 * never modified, and no example is edited to make a test pass.
 *
 * There is no hard-coded list of expected failures. The JSON's own
 * `support` flag is the contract, and it is checked BOTH ways: an entry
 * marked `unsupported` must really fail, and one not so marked must
 * really work. An instruction that starts working fails the test with
 * "now supported, update the JSON" rather than passing quietly.
 *
 * The file is read from the FILESYSTEM, not from `:/instructions/...`.
 * `qt6_add_resources()` gives the compiled resources to the `Kites`
 * executable; `kites_core` does not get them and `kites_test` links
 * `kites_core`, so no `:/` path resolves here.
 * `ResourcePathIsNotReachableFromTests` pins that.
 */
#include "assembler/code_generator.h"
#include "assembler/lexer.h"
#include "assembler/parser.h"
#include "common/instructions.h"
#include "processor/registers.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtGlobal>

#include <iostream>
#include <set>

using namespace Atlas;
using instruction_set::SyntaxType;

namespace
{
constexpr int kSummaryCap = 140;

const char *kDocsPathDefault = "../resources/instructions/instruction_docs.json";
const char *kNamesPath = "../resources/instructions/instructions.json";

QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}
QString readFile(const char *path) { return readFile(QString::fromUtf8(path)); }

/**
 * @brief Where instruction_docs.json is read from.
 *
 * The default is the repository's own file, so an ordinary run is
 * unaffected. `ATLAS_DOCS_JSON` overrides it, and exists for one reason:
 * the negative controls have to point this test at a MUTATED COPY, and
 * the repository's JSON must not be touched to do that. When the
 * override is set the path is printed by every test that reads it, so a
 * run against a copy can never be mistaken for a run against the real
 * file.
 */
QString docsPath()
{
    static const QString path = []
    {
        const QByteArray fromEnv = qgetenv("ATLAS_DOCS_JSON");
        return fromEnv.isEmpty() ? QString::fromUtf8(kDocsPathDefault)
                                 : QString::fromUtf8(fromEnv);
    }();
    return path;
}

/// Prints the docs path when, and only when, it is not the real one.
void announceDocsPathIfOverridden()
{
    if (docsPath() != QString::fromUtf8(kDocsPathDefault))
        std::cerr << "    [ info ] ATLAS_DOCS_JSON overrides the docs path: "
                  << docsPath().toStdString() << "\n";
}

QJsonObject docsRoot()
{
    QJsonParseError error{};
    const QJsonDocument document =
        QJsonDocument::fromJson(readFile(docsPath()).toUtf8(), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError)
        << "instruction_docs.json is not valid JSON: "
        << error.errorString().toStdString();
    return document.object();
}

QJsonObject entries() { return docsRoot().value("entries").toObject(); }

/// Every mnemonic instructions.json lists, from every category array.
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

/**
 * @brief The UNIVERSE: every name that could legitimately be documented.
 *
 * instructions.json (what the UI offers) plus every name the live lexer
 * accepts. `valid_instructions` is not exported, so the lexer-accepted
 * half is found by asking isValidInstruction() about every name in the
 * exported tables - the same construction atlas-verify/harness/isaprobe
 * uses. A name in valid_instructions and in none of those tables would
 * be invisible here, and would also be unusable, having neither an
 * encoding nor a syntax.
 */
std::set<QString> universe()
{
    std::set<QString> all;
    for (const QString &n : instructionNames())
        all.insert(n);
    for (const auto &pair : instruction_set::instruction_string_map)
        if (instruction_set::isValidInstruction(pair.first))
            all.insert(QString::fromStdString(pair.first));
    for (const auto &pair : instruction_set::instruction_syntax_map)
        if (instruction_set::isValidInstruction(pair.first))
            all.insert(QString::fromStdString(pair.first));
    return all;
}

struct Assembled
{
    QStringList           complaints;
    QStringList           mnemonics;
    QStringList           operands;   ///< the operand text of each unit
    std::vector<uint32_t> words;
    bool ok() const { return complaints.isEmpty(); }
};

/// The wrapper, so a branch target and a data label always exist.
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

        for (const auto &unit : parser.getIntermediateCode())
        {
            out.mnemonics << QString::fromLatin1(unit.first.opcode.data());
            QStringList ops;
            if (unit.first.rd[0] != '\0')
                ops << QString::fromLatin1(unit.first.rd.data());
            // A CSR instruction's csr field sits between rd and rs1 in the
            // source (csrrw rd, csr, rs1); the Zicsr pseudos' expansions name it.
            if (unit.first.csr != 0)
                for (const auto &[csrName, address] : csr_to_address)
                    if (static_cast<uint32_t>(address) == unit.first.csr)
                        ops << QString::fromStdString(csrName);
            for (const auto &field : {unit.first.rs1, unit.first.rs2})
                if (field[0] != '\0')
                    ops << QString::fromLatin1(field.data());
            if (unit.first.imm[0] != '\0')
                ops << QString::fromLatin1(unit.first.imm.data());
            out.operands << ops.join(QLatin1Char(','));
        }
        if (out.complaints.isEmpty())
        {
            try { out.words = generateMachineCode(parser.getIntermediateCode()); }
            catch (const std::exception &e)
            { out.complaints << QStringLiteral("codegen: %1").arg(e.what()); }
        }
    }
    catch (const std::exception &e)
    { out.complaints << QStringLiteral("lexer threw: %1").arg(e.what()); }
    return out;
}

/// Counter so each temporary file is distinct.
int fileCounter = 0;

Assembled assembleExample(const QString &example, const QString &dir)
{
    return assembleText(wrap(example), dir, QStringLiteral("ex%1").arg(fileCounter++));
}

/// A line satisfying one declared syntax.
QString lineFor(const QString &name, SyntaxType syntax)
{
    switch (syntax)
    {
    case SyntaxType::O_GPR_C_GPR_C_GPR:   return name + " x5, x6, x7";
    case SyntaxType::O_GPR_C_GPR_C_I:     return name + " x5, x6, 1";
    case SyntaxType::O_GPR_C_I:           return name + " x5, 1";
    case SyntaxType::O_GPR_C_GPR_C_IL:    return name + " x5, x6, label_t";
    case SyntaxType::O_GPR_C_GPR_C_DL:    return name + " x5, x6, label_d";
    case SyntaxType::O_GPR_C_IL:          return name + " x5, label_t";
    case SyntaxType::O_GPR_C_DL:          return name + " x5, label_d";
    case SyntaxType::O_GPR_C_I_LP_GPR_RP: return name + " x5, 0(x6)";
    case SyntaxType::O:                   return name;
    case SyntaxType::PSEUDO:              return name;
    case SyntaxType::O_GPR_C_CSR_C_GPR:   return name + " x5, fcsr, x6";
    case SyntaxType::O_GPR_C_CSR_C_I:     return name + " x5, fcsr, 1";
    case SyntaxType::O_FPR_C_FPR_C_FPR_C_FPR:      return name + " f1, f2, f3, f4";
    case SyntaxType::O_FPR_C_FPR_C_FPR_C_FPR_C_RM: return name + " f1, f2, f3, f4, rne";
    case SyntaxType::O_FPR_C_FPR_C_FPR:            return name + " f1, f2, f3";
    case SyntaxType::O_FPR_C_FPR_C_FPR_C_RM:       return name + " f1, f2, f3, rne";
    case SyntaxType::O_FPR_C_FPR:                  return name + " f1, f2";
    case SyntaxType::O_FPR_C_FPR_C_RM:             return name + " f1, f2, rne";
    case SyntaxType::O_FPR_C_GPR:                  return name + " f1, x5";
    case SyntaxType::O_FPR_C_GPR_C_RM:             return name + " f1, x5, rne";
    case SyntaxType::O_GPR_C_FPR:                  return name + " x5, f1";
    case SyntaxType::O_GPR_C_FPR_C_RM:             return name + " x5, f1, rne";
    case SyntaxType::O_GPR_C_FPR_C_FPR:            return name + " x5, f1, f2";
    case SyntaxType::O_FPR_C_I_LP_GPR_RP:          return name + " f1, 0(x5)";
    }
    return name;
}

/// Assembles `name` using its own documented example when it has one, and
/// otherwise every form its declared syntax allows. Returns the first that
/// got all the way through, with `formUsed` set.
Assembled assembleByName(const QString &name, const QJsonObject &docs,
                         const QString &dir, QString *formUsed)
{
    QStringList candidates;
    const QJsonObject entry = docs.value(name).toObject();
    const QString own = entry.value("example").toString();
    if (!own.isEmpty())
        candidates << own;

    const auto it = instruction_set::instruction_syntax_map.find(name.toStdString());
    if (it != instruction_set::instruction_syntax_map.end())
        for (SyntaxType s : it->second)
            candidates << lineFor(name, s);
    candidates << name << name + " label_t" << name + " x5, x6"
               << name + " x5, f1" << name + " f1, x5" << name + " f1, f2"
               << name + " x5, f1, f2" << name + " f1, f2, f3";

    Assembled first;
    bool haveFirst = false;
    for (const QString &line : candidates)
    {
        const Assembled a = assembleExample(line, dir);
        if (a.ok())
        {
            if (formUsed) *formUsed = line;
            return a;
        }
        if (!haveFirst) { first = a; haveFirst = true; if (formUsed) *formUsed = line; }
    }
    return first;
}

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

bool isUnsupported(const QJsonObject &entry)
{
    return entry.value("support").toString() == QLatin1String("unsupported");
}
} // namespace

// ═══════════════════════ the file itself ════════════════════════════════

TEST(InstructionDocs, TheFileIsValidJsonWithTheExpectedShape)
{
    ASSERT_FALSE(readFile(docsPath()).isEmpty())
        << "instruction_docs.json could not be read from " << docsPath().toStdString();
    const QJsonObject root = docsRoot();
    EXPECT_EQ(root.value("schema").toInt(), 2);
    EXPECT_TRUE(root.contains("entries"));
    EXPECT_FALSE(entries().isEmpty());
}

TEST(InstructionDocs, ResourcePathIsNotReachableFromTests)
{
    QFile viaResource(QStringLiteral(":/instructions/instruction_docs.json"));
    EXPECT_FALSE(viaResource.open(QIODevice::ReadOnly))
        << "the resource system now reaches kites_test; update the file comment";
}

// ═══════════ (a) the universe is covered, in both directions ════════════

TEST(InstructionDocs, EveryNameInTheUniverseHasAnEntryAndEveryEntryIsInTheUniverse)
{
    const std::set<QString> all = universe();
    ASSERT_FALSE(all.empty());
    const QJsonObject docs = entries();

    std::set<QString> documented;
    for (const QString &key : docs.keys())
        documented.insert(key);

    QStringList missing, unexplained;
    int outsideButMarked = 0;
    for (const QString &n : all)
        if (!documented.count(n))
            missing << n;
    for (const QString &key : docs.keys())
    {
        if (all.count(key))
            continue;
        // An entry outside the universe is allowed, but only if it SAYS
        // it does not work. That is how the 36 names the lexer rejects
        // stay documented after instructions.json stopped offering them:
        // documenting something that does not assemble is useful, and
        // claiming it does would not be.
        if (isUnsupported(docs.value(key).toObject()))
            ++outsideButMarked;
        else
            unexplained << key;
    }
    missing.sort();
    unexplained.sort();

    std::cerr << "    [ info ] universe " << all.size()
              << ", entries " << documented.size()
              << ", documented-but-unsupported " << outsideButMarked << "\n";
    EXPECT_TRUE(missing.isEmpty())
        << "in instructions.json or accepted by the lexer, but undocumented ("
        << missing.size() << "): " << missing.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(unexplained.isEmpty())
        << "documented, neither offered nor accepted, and NOT marked unsupported ("
        << unexplained.size() << "): "
        << unexplained.join(QLatin1Char(' ')).toStdString();
    EXPECT_GT(outsideButMarked, 0)
        << "no entry is documented-but-unsupported; the flag has stopped being used";
}

// ═══════ (b) the support flag is the contract, checked both ways ════════

TEST(InstructionDocs, TheSupportFlagMatchesWhatTheAssemblerDoes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QJsonObject docs = entries();

    int supportedChecked = 0, unsupportedChecked = 0;
    QStringList brokenButUnmarked, nowSupported;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry   = docs.value(name).toObject();
        const QString     example = entry.value("example").toString();
        if (example.isEmpty())
            continue; // (d) covers these
        const Assembled result = assembleExample(example, dir.path());

        if (isUnsupported(entry))
        {
            ++unsupportedChecked;
            if (result.ok())
                nowSupported << QStringLiteral("%1 (`%2`)").arg(name, example);
        }
        else
        {
            ++supportedChecked;
            if (!result.ok())
                brokenButUnmarked
                    << QStringLiteral("%1\n      example: %2\n      parser : %3")
                           .arg(name, example,
                                result.complaints.join(QStringLiteral(" | ")));
        }
    }
    std::cerr << "    [ info ] supported examples " << supportedChecked
              << ", unsupported examples " << unsupportedChecked << "\n";
    for (const QString &b : brokenButUnmarked)
        std::cerr << "    [ FAIL  ] " << b.toStdString() << "\n";
    for (const QString &n : nowSupported)
        std::cerr << "    [ FAIL  ] " << n.toStdString() << "\n";

    EXPECT_GT(supportedChecked, 0);
    EXPECT_GT(unsupportedChecked, 0);
    EXPECT_TRUE(brokenButUnmarked.isEmpty())
        << brokenButUnmarked.size()
        << " entry/entries are not marked unsupported but do not assemble; names above";
    EXPECT_TRUE(nowSupported.isEmpty())
        << nowSupported.size() << " entry/entries now assemble: "
        << "NOW SUPPORTED, UPDATE THE JSON -> "
        << nowSupported.join(QLatin1Char(' ')).toStdString();
}

// ══════════════════ (c) useInstead really is a way out ══════════════════

TEST(InstructionDocs, EveryUseInsteadTargetWorksAndTheAliasDoesNot)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QJsonObject docs = entries();

    int withTarget = 0, nullTargets = 0;
    QStringList aliasAccepted, targetBroken;
    QStringList nullNames;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        if (!entry.contains("useInstead"))
            continue;
        // A JSON null means "there is no replacement" - a legitimate
        // value, not a target to assemble.
        if (entry.value("useInstead").isNull())
        { ++nullTargets; nullNames << name; continue; }
        ++withTarget;

        // The alias's own name must be rejected by the lexer, or there
        // would be no reason to point elsewhere.
        if (instruction_set::isValidInstruction(name.toStdString()))
            aliasAccepted << name;

        const QString target = entry.value("useInstead").toString();
        QString form;
        const Assembled result = assembleByName(target, docs, dir.path(), &form);
        if (!result.ok())
            targetBroken << QStringLiteral("%1 -> %2 (tried `%3`): %4")
                                .arg(name, target, form,
                                     result.complaints.join(QStringLiteral(" | ")));
    }
    nullNames.sort();
    std::cerr << "    [ info ] useInstead with a target " << withTarget
              << ", null " << nullTargets << " (" << nullNames.join(QLatin1Char(' ')).toStdString()
              << ")\n";
    for (const QString &a : aliasAccepted)
        std::cerr << "    [ FAIL  ] " << a.toStdString()
                  << " has a useInstead but the lexer accepts it\n";
    for (const QString &t : targetBroken)
        std::cerr << "    [ FAIL  ] " << t.toStdString() << "\n";

    EXPECT_GT(withTarget, 0);
    EXPECT_TRUE(aliasAccepted.isEmpty())
        << aliasAccepted.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(targetBroken.isEmpty())
        << targetBroken.size() << " useInstead target(s) do not assemble; names above";
}

// ═════════ (d) unsupported entries with no example: report only ═════════

TEST(InstructionDocs, UnsupportedEntriesWithNoExampleAreRejectedByTheLexer)
{
    const QJsonObject docs = entries();
    QStringList rejected, acceptedAnyway;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        if (!isUnsupported(entry) || !entry.value("example").toString().isEmpty())
            continue;
        if (instruction_set::isValidInstruction(name.toStdString()))
            acceptedAnyway << name;
        else
            rejected << name;
    }
    rejected.sort();
    acceptedAnyway.sort();
    std::cerr << "    [ info ] unsupported with no example: " << rejected.size()
              << " rejected by the lexer, " << acceptedAnyway.size()
              << " accepted\n";
    for (const QString &a : acceptedAnyway)
        std::cerr << "    [ report ] " << a.toStdString()
                  << ": the lexer ACCEPTS it, and there is no example to test with\n";

    EXPECT_GT(rejected.size(), 0);
    // No example is invented for the accepted ones; they are reported.
    EXPECT_TRUE(acceptedAnyway.isEmpty())
        << acceptedAnyway.size()
        << " unsupported entry/entries are accepted by the lexer and carry no example: "
        << acceptedAnyway.join(QLatin1Char(' ')).toStdString();
}

// ════════════════════ (e) shape, length, expansions ═════════════════════

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
                         .arg(name).arg(formatOperands).arg(exampleOperands)
                         .arg(format, example);
    }
    std::cerr << "    [ info ] format/example pairs checked " << checked << "\n";
    for (const QString &w : wrong)
        std::cerr << "    [ FAIL  ] " << w.toStdString() << "\n";
    EXPECT_GT(checked, 0);
    EXPECT_TRUE(wrong.isEmpty()) << wrong.size() << " mismatch(es); names above";
}

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
                continue; // a null useInstead is legitimate
            if (value.isString() && value.toString().trimmed().isEmpty())
                empty << QStringLiteral("%1.%2").arg(name, key);
            if (value.isArray() && value.toArray().isEmpty())
                empty << QStringLiteral("%1.%2").arg(name, key);
        }
        const QString summary = entry.value("summary").toString();
        if (summary.size() > kSummaryCap)
            tooLong << QStringLiteral("%1 (%2 chars)").arg(name).arg(summary.size());
        if (isUnsupported(entry) && entry.value("reason").toString().trimmed().isEmpty())
            empty << QStringLiteral("%1.reason (unsupported with no reason)").arg(name);
    }
    for (const QString &t : tooLong)
        std::cerr << "    [ FAIL  ] summary over " << kSummaryCap << ": " << t.toStdString() << "\n";
    for (const QString &e : empty)
        std::cerr << "    [ FAIL  ] empty field: " << e.toStdString() << "\n";
    EXPECT_TRUE(tooLong.isEmpty());
    EXPECT_TRUE(empty.isEmpty());
}

TEST(InstructionDocs, PseudoExpansionsMatchTheAssemblerIncludingFixedOperands)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QJsonObject docs = entries();

    int compared = 0;
    QStringList wrongMnemonics, wrongOperands;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        if (!entry.contains("expandsTo") || isUnsupported(entry))
            continue;
        const QString example = entry.value("example").toString();
        if (example.isEmpty())
            continue;

        // `expandsTo` is a STRING; ';' separates the steps of a
        // multi-instruction expansion.
        QStringList claimedSteps =
            entry.value("expandsTo").toString().split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QStringList claimedMnemonics;
        for (const QString &step : claimedSteps)
            claimedMnemonics << mnemonicOf(step.trimmed());

        const Assembled result = assembleExample(example, dir.path());
        ++compared;
        if (!result.ok())
        {
            wrongMnemonics << QStringLiteral("%1: did not assemble (%2)")
                                  .arg(name, result.complaints.join(QStringLiteral(" | ")));
            continue;
        }
        if (result.mnemonics != claimedMnemonics)
        {
            wrongMnemonics << QStringLiteral("%1: documented [%2], assembler produced [%3]")
                                  .arg(name, claimedMnemonics.join(QLatin1Char(' ')),
                                       result.mnemonics.join(QLatin1Char(' ')));
            continue;
        }

        // Fixed operands: anything in the expansion that is a literal
        // register or number rather than a placeholder - x0, 0, ra, rs -
        // must appear in the operands the assembler produced for that step.
        for (int i = 0; i < claimedSteps.size() && i < result.operands.size(); ++i)
        {
            const QStringList fixed = operandsOf(claimedSteps.at(i).trimmed());
            for (const QString &operand : fixed)
            {
                const QString token = operand.trimmed();
                const bool literalRegister =
                    QRegularExpression(QStringLiteral("^(x\\d+|ra|sp|gp|tp|fp)$")).match(token).hasMatch();
                const bool literalNumber =
                    QRegularExpression(QStringLiteral("^-?\\d+$")).match(token).hasMatch();
                // `0(ra)` and `0(rs)` carry a fixed 0 and sometimes a
                // fixed register; both halves are checked.
                const auto paren =
                    QRegularExpression(QStringLiteral("^(-?\\d+)\\((x\\d+|ra|sp|rs)\\)$")).match(token);
                QStringList mustAppear;
                if (literalRegister || literalNumber)
                    mustAppear << token;
                else if (paren.hasMatch())
                {
                    mustAppear << paren.captured(1);
                    if (paren.captured(2) != QLatin1String("rs"))
                        mustAppear << paren.captured(2);
                }
                for (const QString &needle : mustAppear)
                {
                    const QString produced = result.operands.at(i);
                    const QStringList parts = produced.split(QLatin1Char(','));
                    // ── Exactly which alias resolutions this accepts ──
                    //
                    // The assembler normalises ABI aliases to xN before the
                    // operands reach here (control 2b saw `ret` produce
                    // "x0,x1,0" for a documented `jalr x0, 0(ra)`), so a
                    // documented alias needs its number accepted beside it.
                    // The whole table is three rows and nothing else:
                    //
                    //     ra -> also accepts x1
                    //     sp -> also accepts x2
                    //     fp -> also accepts x8
                    //
                    // Two things about that table are wrong and are left
                    // alone deliberately, because fixing either changes what
                    // the test accepts and no current expansion exercises it:
                    //
                    //   - `fp` is a DEAD row. src/common/register_names.h
                    //     kGprAliases has `s0` at index 8; this assembler has
                    //     no `fp` alias at all, so a documented `fp` could
                    //     never assemble in the first place.
                    //   - `gp` (x3) and `tp` (x4) are admitted by the literal
                    //     register regex above but have NO row here, so an
                    //     expansion documented with either would be checked
                    //     against a produced `x3`/`x4` and would fail WRONGLY.
                    //
                    // Other leniency in this loop, all measured:
                    //
                    //   - Position is NOT checked. `parts.contains()` asks
                    //     only whether the fixed operand appears somewhere in
                    //     that step. Rewriting sltz's expansion to
                    //     `slt rd, x0, rs` - which is sgtz, the opposite
                    //     comparison - was measured to pass 12/12.
                    //   - Multiplicity is not checked either: nop's
                    //     `addi x0, x0, 0` needs x0 present, not present twice.
                    //   - Anything that is not xN / ra / sp / gp / tp / fp,
                    //     not an integer, and not N(reg) is skipped as a
                    //     placeholder. That silently includes the real aliases
                    //     `zero`, `t0`-`t6`, `s0`-`s11` and `a0`-`a7`: a
                    //     documented `add rd, rs, zero` is never checked.
                    //   - `rs` inside `N(rs)` is skipped by design; only the
                    //     fixed N is checked.
                    //   - Entries marked unsupported, or with no example, are
                    //     not compared at all.
                    QStringList accept{needle};
                    if (needle == QLatin1String("ra")) accept << QStringLiteral("x1");
                    if (needle == QLatin1String("sp")) accept << QStringLiteral("x2");
                    if (needle == QLatin1String("fp")) accept << QStringLiteral("x8");
                    bool found = false;
                    for (const QString &a : accept)
                        if (parts.contains(a))
                            found = true;
                    if (!found)
                        wrongOperands
                            << QStringLiteral("%1 step %2: documented fixed operand '%3' "
                                              "is not in what the assembler produced [%4]")
                                   .arg(name).arg(i).arg(needle, produced);
                }
            }
        }
    }
    std::cerr << "    [ info ] expansions compared " << compared << "\n";
    for (const QString &w : wrongMnemonics)
        std::cerr << "    [ FAIL  ] " << w.toStdString() << "\n";
    for (const QString &w : wrongOperands)
        std::cerr << "    [ FAIL  ] " << w.toStdString() << "\n";

    EXPECT_GT(compared, 0) << "no expandsTo entry was compared";
    EXPECT_TRUE(wrongMnemonics.isEmpty())
        << wrongMnemonics.size() << " expansion(s) differ; names above";
    EXPECT_TRUE(wrongOperands.isEmpty())
        << wrongOperands.size() << " fixed operand(s) differ; names above";
}

// ══════════ expandsTo, every operand, in order ══════════════════════════
//
// The test above compares mnemonics strictly, but its operand check is a
// MEMBERSHIP test: `parts.contains(needle)`. Its own comment records what
// that lets through, measured - rewriting sltz's expansion to
// `slt rd, x0, rs`, which is sgtz, the opposite comparison, passed 12 of
// 12. Aliases are skipped there as placeholders, position is not checked,
// and multiplicity is not checked.
//
// This one closes that. For every supported entry with a non-empty
// `expandsTo` it binds the entry's placeholders by aligning `format` with
// `example` positionally, substitutes them into each clause, resolves
// aliases through the assembler's own table, and compares EVERY operand
// POSITIONALLY against what the assembler really emitted.
//
// What "really emitted" means here is the same mechanism the test above
// uses: assembleExample(), i.e. Lexer -> Parser ->
// parser.getIntermediateCode(). Operands arrive in ICUnit field order -
// rd, rs1, rs2, then imm - with registers already normalised to xN/fN by
// the parser and labels already back-patched to a numeric offset.
//
// Nothing here is silent. Every entry is either compared or reported as a
// skip with its reason, and the two counts are printed.

namespace
{
/// `imm(rs1)`: the offset is capture 1, the base register capture 2.
const QRegularExpression &memoryOperandPattern()
{
    static const QRegularExpression re(QStringLiteral("^([^()\\s]+)\\(([^()\\s]+)\\)$"));
    return re;
}

/**
 * @brief A trailing prose note, as in
 *        `addi rd, x0, imm (or lui + addi for large values)`.
 *
 * Deliberately narrow: the parenthesis must open with "or", so that a
 * real memory operand - `jalr x0, 0(ra)` also ends in `)` - can never be
 * mistaken for prose and silently truncated. Every strip is counted and
 * printed.
 */
const QRegularExpression &proseNotePattern()
{
    static const QRegularExpression re(QStringLiteral("\\s*\\(\\s*or\\b[^()]*\\)\\s*$"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

// ── alias resolution: THE assembler's table, not a copy of it ───────────
//
// `reg_alias_to_name` (src/processor/registers.h) is what every
// src/assembler/parse_formats/*.cpp file resolves its operands through -
// `reg_alias_to_name.at(peekToken(1).value)`, ~90 call sites. It is
// therefore the table, not an equivalent of it. src/common/register_names.h
// is NOT used here: nothing in src/assembler/ includes it, and it has no
// `fp` row, which this one does (fp -> x8).

bool isRegisterToken(const QString &token)
{
    return reg_alias_to_name.find(token.toStdString()) != reg_alias_to_name.end();
}

QString canonicalRegister(const QString &token)
{
    const auto it = reg_alias_to_name.find(token.toStdString());
    return it == reg_alias_to_name.end() ? token : QString::fromStdString(it->second);
}

/// Base 0, so `42`, `-1` and `0x10` all parse and compare numerically.
bool asInteger(const QString &token, qlonglong *value)
{
    bool ok = false;
    const qlonglong parsed = token.toLongLong(&ok, 0);
    if (ok && value)
        *value = parsed;
    return ok;
}
bool isIntegerToken(const QString &token) { return asInteger(token, nullptr); }

/// Neither a register nor a number: a label, once it has been bound.
bool isLabelToken(const QString &token)
{
    return !isRegisterToken(token) && !isIntegerToken(token);
}

bool sameOperand(const QString &a, const QString &b)
{
    qlonglong x = 0, y = 0;
    if (asInteger(a, &x) && asInteger(b, &y))
        return x == y;
    return a == b;
}

/**
 * @brief Stores are the one shape where source order and field order differ.
 *
 * `sw rs2, imm(rs1)` writes the SOURCE register first, but the
 * intermediate code carries rs1 (the base) before rs2. Every other shape
 * in the instruction set writes its operands in field order. Rather than
 * silently compare a store in the wrong order, a clause with one of these
 * mnemonics is skipped and said so. No expansion in the file is a store
 * today; the count is printed so that stays visible if one appears.
 */
bool isStoreMnemonic(const QString &mnemonic)
{
    static const QSet<QString> stores{QStringLiteral("sb"),  QStringLiteral("sh"),
                                      QStringLiteral("sw"),  QStringLiteral("sd"),
                                      QStringLiteral("fsw"), QStringLiteral("fsd")};
    return stores.contains(mnemonic);
}

/// One clause of an expansion, both ways round.
struct ClauseShape
{
    QString     mnemonic;
    QStringList fieldOrder;  ///< as the intermediate code orders them
    QStringList sourceOrder; ///< as written, so the clause can be re-assembled
    QString     problem;     ///< non-empty: do not compare this entry
};

ClauseShape shapeOf(const QString &rawClause, bool *proseStripped)
{
    ClauseShape shape;
    QString clause = rawClause.trimmed();
    if (proseNotePattern().match(clause).hasMatch())
    {
        clause.remove(proseNotePattern());
        if (proseStripped)
            *proseStripped = true;
    }
    shape.mnemonic    = mnemonicOf(clause);
    shape.sourceOrder = operandsOf(clause);

    QString pendingOffset;
    for (const QString &token : shape.sourceOrder)
    {
        const auto memory = memoryOperandPattern().match(token);
        if (!memory.hasMatch())
        {
            shape.fieldOrder << token;
            continue;
        }
        if (!pendingOffset.isEmpty())
        {
            shape.problem = QStringLiteral("clause '%1' has two memory operands").arg(clause);
            return shape;
        }
        // The base goes where the operand stood; the offset is the imm,
        // and the imm is last in the intermediate code.
        shape.fieldOrder << memory.captured(2);
        pendingOffset = memory.captured(1);
    }
    if (!pendingOffset.isEmpty())
        shape.fieldOrder << pendingOffset;
    if (isStoreMnemonic(shape.mnemonic))
        shape.problem = QStringLiteral("clause '%1' is a store: its source order is "
                                       "rs2 before rs1, the opposite of the field order")
                            .arg(clause);
    return shape;
}

/// One operand atom (no parentheses) with the entry's bindings applied.
QString substituteAtom(const QString &atom, const QMap<QString, QString> &binding,
                       QStringList *unbound)
{
    const auto it = binding.constFind(atom);
    if (it != binding.constEnd())
        return it.value();
    if (!isRegisterToken(atom) && !isIntegerToken(atom) && unbound)
        *unbound << atom;
    return atom;
}

/// The same, for a whole operand, `imm(rs1)` included.
QString substituteOperand(const QString &token, const QMap<QString, QString> &binding,
                          QStringList *unbound)
{
    const auto memory = memoryOperandPattern().match(token);
    if (!memory.hasMatch())
        return substituteAtom(token, binding, unbound);
    return QStringLiteral("%1(%2)")
        .arg(substituteAtom(memory.captured(1), binding, unbound),
             substituteAtom(memory.captured(2), binding, unbound));
}
} // namespace

TEST(InstructionDocs, ExpansionOperandsMatchTheAssemblerPositionally)
{
    announceDocsPathIfOverridden();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QJsonObject docs = entries();

    int inScope = 0, checked = 0, labelOperandsChecked = 0, proseStrips = 0;
    QStringList skipped, failures;

    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        const QString expansion = entry.value("expandsTo").toString().trimmed();
        if (expansion.isEmpty() || isUnsupported(entry))
            continue; // out of scope: no expansion documented, or not supported
        ++inScope;

        const QString format  = entry.value("format").toString().trimmed();
        const QString example = entry.value("example").toString().trimmed();
        if (format.isEmpty() || example.isEmpty())
        {
            skipped << QStringLiteral("%1: the entry has no %2")
                           .arg(name, format.isEmpty() ? QStringLiteral("format")
                                                       : QStringLiteral("example"));
            continue;
        }

        // ── (a) bind the placeholders, format against example, by position ──
        const QStringList formatOperands  = operandsOf(format);
        const QStringList exampleOperands = operandsOf(example);
        if (formatOperands.size() != exampleOperands.size())
        {
            skipped << QStringLiteral("%1: format has %2 operand(s), example has %3")
                           .arg(name)
                           .arg(formatOperands.size())
                           .arg(exampleOperands.size());
            continue;
        }

        QMap<QString, QString> binding;
        QString bindProblem;
        const auto bindOne = [&](const QString &placeholder, const QString &value)
        {
            if (!bindProblem.isEmpty())
                return;
            // A format operand that is itself a register or a number is
            // not a placeholder: it is a constant the example must match.
            if (isRegisterToken(placeholder) || isIntegerToken(placeholder))
            {
                if (!sameOperand(canonicalRegister(placeholder), canonicalRegister(value)))
                    bindProblem = QStringLiteral("format fixes '%1' where the example has '%2'")
                                      .arg(placeholder, value);
                return;
            }
            const auto existing = binding.constFind(placeholder);
            if (existing != binding.constEnd() && existing.value() != value)
            {
                bindProblem = QStringLiteral("'%1' is bound twice, to '%2' and '%3'")
                                  .arg(placeholder, existing.value(), value);
                return;
            }
            binding.insert(placeholder, value);
        };

        for (int i = 0; i < formatOperands.size(); ++i)
        {
            const auto formatMemory  = memoryOperandPattern().match(formatOperands.at(i));
            const auto exampleMemory = memoryOperandPattern().match(exampleOperands.at(i));
            if (formatMemory.hasMatch() != exampleMemory.hasMatch())
            {
                bindProblem = QStringLiteral("operand %1 is '%2' in the format and '%3' in "
                                             "the example; one is a memory operand and one is not")
                                  .arg(i)
                                  .arg(formatOperands.at(i), exampleOperands.at(i));
                break;
            }
            if (formatMemory.hasMatch())
            {
                bindOne(formatMemory.captured(1), exampleMemory.captured(1)); // the offset
                bindOne(formatMemory.captured(2), exampleMemory.captured(2)); // the base
            }
            else
            {
                bindOne(formatOperands.at(i), exampleOperands.at(i));
            }
        }
        if (!bindProblem.isEmpty())
        {
            skipped << QStringLiteral("%1: %2").arg(name, bindProblem);
            continue;
        }

        // ── (b) substitute into every ';'-separated clause ──
        const QStringList clauses = expansion.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        QStringList         documentedMnemonics;
        QList<QStringList>  documentedOperands;  // field order, aliases resolved
        QStringList         substitutedLines;    // source order, for re-assembly
        QString             clauseProblem;
        for (const QString &clause : clauses)
        {
            bool stripped = false;
            const ClauseShape shape = shapeOf(clause, &stripped);
            if (stripped)
                ++proseStrips;
            if (!shape.problem.isEmpty())
            {
                clauseProblem = shape.problem;
                break;
            }
            QStringList unbound;
            QStringList resolved;
            for (const QString &atom : shape.fieldOrder)
            {
                // (c) every register alias through the assembler's table.
                resolved << canonicalRegister(substituteAtom(atom, binding, &unbound));
            }
            QStringList sourceTokens;
            for (const QString &token : shape.sourceOrder)
                sourceTokens << substituteOperand(token, binding, &unbound);
            if (!unbound.isEmpty())
            {
                unbound.removeDuplicates();
                clauseProblem =
                    QStringLiteral("the expansion names %1, which '%2' does not declare")
                        .arg(unbound.join(QStringLiteral(", ")), format);
                break;
            }
            documentedMnemonics << shape.mnemonic;
            documentedOperands << resolved;
            substitutedLines << (sourceTokens.isEmpty()
                                     ? shape.mnemonic
                                     : shape.mnemonic + QLatin1Char(' ') +
                                           sourceTokens.join(QStringLiteral(", ")));
        }
        if (!clauseProblem.isEmpty())
        {
            skipped << QStringLiteral("%1: %2").arg(name, clauseProblem);
            continue;
        }

        // ── (d) what the assembler really produced for the example ──
        const Assembled real = assembleExample(example, dir.path());
        if (!real.ok())
        {
            failures << QStringLiteral("%1: the example '%2' did not assemble (%3)")
                            .arg(name, example, real.complaints.join(QStringLiteral(" | ")));
            continue;
        }
        if (real.mnemonics.size() != documentedMnemonics.size())
        {
            failures << QStringLiteral("%1 [%2]: documented %3 clause(s) [%4], the assembler "
                                       "produced %5 [%6]")
                            .arg(name, example)
                            .arg(documentedMnemonics.size())
                            .arg(substitutedLines.join(QStringLiteral("; ")))
                            .arg(real.mnemonics.size())
                            .arg(real.mnemonics.join(QStringLiteral(" ")));
            continue;
        }

        // A documented label cannot be compared against a back-patched
        // offset textually, so the expected value for that one operand is
        // the assembler's OWN resolution of the documented label: the
        // substituted expansion, assembled in the same wrapper at the same
        // instruction index. Computed once per entry, and only if needed.
        Assembled labelOracle;
        bool      oracleBuilt = false, oracleUsable = false;
        QString   oracleProblem;
        const auto oracle = [&]() -> const Assembled &
        {
            if (!oracleBuilt)
            {
                oracleBuilt = true;
                labelOracle = assembleText(wrap(substitutedLines.join(QLatin1Char('\n'))),
                                           dir.path(),
                                           QStringLiteral("sub%1").arg(fileCounter++));
                oracleUsable = labelOracle.ok() &&
                               labelOracle.operands.size() == documentedOperands.size();
                if (!oracleUsable)
                    oracleProblem = labelOracle.ok()
                                        ? QStringLiteral("it produced %1 unit(s), not %2")
                                              .arg(labelOracle.operands.size())
                                              .arg(documentedOperands.size())
                                        : labelOracle.complaints.join(QStringLiteral(" | "));
            }
            return labelOracle;
        };

        bool entrySkipped = false;
        for (int i = 0; i < documentedMnemonics.size() && !entrySkipped; ++i)
        {
            if (documentedMnemonics.at(i) != real.mnemonics.at(i))
            {
                failures << QStringLiteral("%1 [%2] clause %3: documented '%4', the assembler "
                                           "produced '%5'")
                                .arg(name, example)
                                .arg(i)
                                .arg(documentedMnemonics.at(i), real.mnemonics.at(i));
                continue;
            }
            const QStringList realOperands =
                real.operands.at(i).split(QLatin1Char(','), Qt::SkipEmptyParts);
            const QStringList &claimed = documentedOperands.at(i);
            if (claimed.size() != realOperands.size())
            {
                failures << QStringLiteral("%1 [%2] clause %3: documented %4 operand(s) [%5], "
                                           "the assembler produced %6 [%7]")
                                .arg(name, example)
                                .arg(i)
                                .arg(claimed.size())
                                .arg(claimed.join(QStringLiteral(", ")))
                                .arg(realOperands.size())
                                .arg(realOperands.join(QStringLiteral(", ")));
                continue;
            }
            for (int j = 0; j < claimed.size(); ++j)
            {
                QString expected = claimed.at(j);
                if (isLabelToken(expected))
                {
                    if (!oracle().ok() || !oracleUsable)
                    {
                        skipped << QStringLiteral("%1: the substituted expansion '%2' does not "
                                                  "assemble, so the label operand cannot be "
                                                  "compared (%3)")
                                       .arg(name,
                                            substitutedLines.join(QStringLiteral("; ")),
                                            oracleProblem);
                        entrySkipped = true;
                        break;
                    }
                    const QStringList oracleOperands =
                        labelOracle.operands.at(i).split(QLatin1Char(','), Qt::SkipEmptyParts);
                    if (j >= oracleOperands.size())
                    {
                        skipped << QStringLiteral("%1: the substituted expansion has no operand "
                                                  "%2 in clause %3")
                                       .arg(name).arg(j).arg(i);
                        entrySkipped = true;
                        break;
                    }
                    expected = oracleOperands.at(j);
                    ++labelOperandsChecked;
                }
                if (!sameOperand(expected, realOperands.at(j)))
                    failures << QStringLiteral("%1 [%2] clause %3 operand %4: documented '%5', "
                                               "the assembler produced '%6'  "
                                               "[documented: %7] [assembler: %8]")
                                    .arg(name, example)
                                    .arg(i)
                                    .arg(j)
                                    .arg(expected, realOperands.at(j),
                                         substitutedLines.join(QStringLiteral("; ")),
                                         real.mnemonics.at(i) + QLatin1Char(' ') +
                                             realOperands.join(QStringLiteral(", ")));
            }
        }
        if (!entrySkipped)
            ++checked;
    }

    std::cerr << "    [ info ] supported entries with a non-empty expandsTo " << inScope << "\n";
    std::cerr << "    [ info ] checked " << checked << ", skipped " << skipped.size() << "\n";
    std::cerr << "    [ info ] label operands compared through the assembler's own "
                 "back-patching "
              << labelOperandsChecked << "\n";
    std::cerr << "    [ info ] trailing prose notes stripped from a clause " << proseStrips << "\n";
    for (const QString &s : skipped)
        std::cerr << "    [ skip ] " << s.toStdString() << "\n";
    for (const QString &f : failures)
        std::cerr << "    [ FAIL  ] " << f.toStdString() << "\n";

    EXPECT_GT(checked, 0) << "no expansion was compared";
    EXPECT_EQ(checked + skipped.size(), inScope)
        << "every in-scope entry must be either compared or skipped with a reason";
    EXPECT_TRUE(failures.isEmpty())
        << failures.size() << " operand mismatch(es); names above. The JSON is NOT to be "
                              "edited to make this pass without a decision.";
}

// ══════ instructions.json is exactly the supported set, both ways ═══════
//
// instructions.json is what the UI offers in completion and highlights.
// Offering a name the assembler cannot read teaches the wrong thing, and
// failing to offer one it can read hides a working instruction. The two
// files are therefore pinned to each other.

TEST(InstructionDocs, InstructionsJsonIsExactlyTheSupportedSet)
{
    const QJsonObject docs = entries();
    std::set<QString> supported;
    for (const QString &name : docs.keys())
        if (!isUnsupported(docs.value(name).toObject()))
            supported.insert(name);

    const QStringList listed = instructionNames();
    std::set<QString> offered(listed.begin(), listed.end());

    QStringList offeredButNotSupported, supportedButNotOffered;
    for (const QString &n : offered)
        if (!supported.count(n))
            offeredButNotSupported << n;
    for (const QString &n : supported)
        if (!offered.count(n))
            supportedButNotOffered << n;
    offeredButNotSupported.sort();
    supportedButNotOffered.sort();

    std::cerr << "    [ info ] supported entries " << supported.size()
              << ", names offered " << offered.size() << "\n";

    EXPECT_TRUE(offeredButNotSupported.isEmpty())
        << "offered by the UI but not a supported entry ("
        << offeredButNotSupported.size()
        << "): " << offeredButNotSupported.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(supportedButNotOffered.isEmpty())
        << "a supported entry the UI never offers (" << supportedButNotOffered.size()
        << "): " << supportedButNotOffered.join(QLatin1Char(' ')).toStdString();
}

TEST(InstructionDocs, InstructionsJsonListsNoNameTwice)
{
    // Four names were listed twice before the sync: fsqrt.s appeared
    // twice in FInstruction, and jal, jalr and nop were in both
    // R32Instruction and PseudoInstruction. Both readers flatten every
    // array, so a duplicate becomes a duplicate completion entry.
    const QStringList listed = instructionNames();
    QStringList duplicated;
    std::set<QString> seen;
    for (const QString &n : listed)
    {
        if (seen.count(n))
            duplicated << n;
        seen.insert(n);
    }
    duplicated.removeDuplicates();
    duplicated.sort();
    EXPECT_TRUE(duplicated.isEmpty())
        << "listed more than once: " << duplicated.join(QLatin1Char(' ')).toStdString();
    EXPECT_EQ(listed.size(), int(seen.size()));
}

// ══════════════════════ (g) the "caveat" field ═════════════════════════
//
// A caveat marks an entry that ASSEMBLES but misbehaves when run. The
// test suite can only assemble, so nothing here can observe the defect
// itself; the caveat is the place the file admits it, and this test
// pins the two things that can be checked mechanically.
//
// The field is optional, so this test is vacuous if it ever disappears
// from the file. The count is printed for exactly that reason - read it.

TEST(InstructionDocs, EveryCaveatIsOnASupportedEntryAndNamesARealKnownIssue)
{
    // The numbered headings of docs/audit/known_issues.md, e.g. "## 57. ...".
    const QString issues = readFile("../docs/audit/known_issues.md");
    ASSERT_FALSE(issues.isEmpty()) << "docs/audit/known_issues.md is unreadable from test/";
    std::set<int> headings;
    {
        QRegularExpression heading(QStringLiteral(R"(^##\s+(\d+)\.)"),
                                   QRegularExpression::MultilineOption);
        auto it = heading.globalMatch(issues);
        while (it.hasNext())
            headings.insert(it.next().captured(1).toInt());
    }
    ASSERT_FALSE(headings.empty()) << "no numbered heading parsed out of known_issues.md";

    const QJsonObject docs = entries();
    QStringList onUnsupported, notAString, noNumber, danglingNumber;
    int withCaveat = 0;
    for (const QString &name : docs.keys())
    {
        const QJsonObject entry = docs.value(name).toObject();
        if (!entry.contains("caveat"))
            continue;
        ++withCaveat;

        // A caveat on an unsupported entry is meaningless: an entry that
        // does not assemble cannot also "assemble but misbehave".
        if (isUnsupported(entry))
            onUnsupported << name;

        const QJsonValue value = entry.value("caveat");
        if (!value.isString())
        {
            notAString << name;
            continue;
        }
        const QString caveat = value.toString();

        // The caveat must point at a known issue by number, and that
        // number must actually have a heading in the file.
        QRegularExpression reference(QStringLiteral(R"(known[_ ]issues?\s*#?\s*(\d+))"),
                                     QRegularExpression::CaseInsensitiveOption);
        auto cited = reference.globalMatch(caveat);
        if (!cited.hasNext())
        {
            noNumber << QStringLiteral("%1: \"%2\"").arg(name, caveat);
            continue;
        }
        while (cited.hasNext())
        {
            const int number = cited.next().captured(1).toInt();
            if (!headings.count(number))
                danglingNumber << QStringLiteral("%1 cites known_issues %2, which has no heading")
                                      .arg(name).arg(number);
        }
    }

    std::cerr << "    [ info ] entries with a caveat " << withCaveat
              << "; known_issues headings parsed " << headings.size() << "\n";
    for (const QStringList *list : {&onUnsupported, &notAString, &noNumber, &danglingNumber})
        for (const QString &f : *list)
            std::cerr << "    [ FAIL  ] " << f.toStdString() << "\n";

    EXPECT_TRUE(onUnsupported.isEmpty())
        << "caveat on an unsupported entry: "
        << onUnsupported.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(notAString.isEmpty())
        << "caveat is not a string: " << notAString.join(QLatin1Char(' ')).toStdString();
    EXPECT_TRUE(noNumber.isEmpty())
        << noNumber.size() << " caveat(s) name no known_issues number; names above";
    EXPECT_TRUE(danglingNumber.isEmpty())
        << danglingNumber.size() << " caveat(s) cite a missing known issue; names above";
}
