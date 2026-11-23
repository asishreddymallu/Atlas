/**
 * @file rename_real_files_test.cpp
 * @brief Rename EVERY label in EVERY real file, and assemble the result.
 *
 * `rename_assembler_test.cpp` uses samples written to exercise one shape
 * each. This one takes the files that actually ship — everything under
 * `examples/` plus the eight label-heavy trap files — and renames every
 * label each of them defines, one at a time, through the editor's own F2
 * path. For each rename the assembler must give back the same symbol
 * table modulo the one name, every symbol on the line it was on, and no
 * diagnostic that was not already there.
 *
 * The files are read, never written. Each rename starts from the original
 * text, so one bad rename cannot cascade into the next.
 */
#include "assembler/lexer.h"
#include "assembler/parser.h"
#include "ui/editor_tab/label_index.h"
#include "ui/editor_tab/label_overlays.h"
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>
#include <map>
#include <set>
#include <string>

using namespace Atlas;

namespace
{
struct Assembled
{
    std::map<std::string, SymbolData> symbols;
    QStringList                       diagnostics;
    QString                           thrown;

    QString complaints() const
    {
        QStringList all = diagnostics;
        if (!thrown.isEmpty())
            all << thrown;
        all.sort();
        return all.join(QLatin1Char('\n'));
    }
};

Assembled assemble(const QString &text, const QString &path)
{
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            Assembled bad;
            bad.thrown = QStringLiteral("could not write %1").arg(path);
            return bad;
        }
        QTextStream(&file) << text;
    }
    Assembled out;
    try
    {
        Lexer  lexer(path.toStdString());
        auto   tokens = lexer.getTokenList();
        Parser parser(path.toStdString(), tokens);
        try { parser.parse(); }
        catch (const std::exception &e) { out.thrown = QString::fromUtf8(e.what()); }
        out.symbols = parser.getSymbolTable();
        for (const ParseError &error : parser.getErrors())
            out.diagnostics << QStringLiteral("line %1: %2")
                                   .arg(error.line)
                                   .arg(QString::fromStdString(error.message));
    }
    catch (const std::exception &e) { out.thrown = QString::fromUtf8(e.what()); }
    return out;
}

/// Renames `from` to `to` through Editor::openRenameAtCaret()/applyRename().
/// With `definitionOnly`, rewrites just the definition - the control.
QString renameThroughEditor(const QString &text, const QString &from,
                            const QString &to, bool definitionOnly = false)
{
    Editor editor;
    editor.resize(600, 300);
    editor.show();
    editor.setPlainText(text);
    QApplication::processEvents();

    const LabelIndex::Index index = LabelIndex::findLabels(text);
    const auto              defs  = index.definitionsOf(from);
    if (defs.isEmpty())
        return {};

    if (definitionOnly)
    {
        QTextCursor cursor(editor.document());
        cursor.setPosition(defs.first().offset);
        cursor.setPosition(defs.first().offset + from.size(), QTextCursor::KeepAnchor);
        cursor.insertText(to);
        QApplication::processEvents();
        return editor.toPlainText();
    }

    QTextCursor caret(editor.document());
    caret.setPosition(defs.first().offset + 1);
    editor.setTextCursor(caret);
    QApplication::processEvents();
    if (!editor.openRenameAtCaret())
        return {};
    editor.renameBar()->input()->setText(to);
    QApplication::processEvents();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(editor.renameBar()->input(), &press);
    QApplication::processEvents();
    return editor.toPlainText();
}

/// Every real file, by path relative to test/ (the CTest working directory).
QStringList everyRealFile()
{
    // RECURSIVE. examples/ holds two subdirectories - custom-replacement-
    // scripts and fd-extension - and a non-recursive listing silently
    // skipped both, which would have made the coverage claim here larger
    // than what was actually checked.
    QStringList files;
    for (const QString &root : {QStringLiteral("../examples"),
                                QStringLiteral("data/labelcorpus")})
    {
        // Assembly only. examples/ also ships a Lua replacement-policy
        // script, which is not something the assembler can read; including
        // it produced one meaningless "skipped" line.
        QDirIterator it(root, {QStringLiteral("*.s"), QStringLiteral("*.asm")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();
    }
    files.sort();
    return files;
}

QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(f.readAll());
}

/// A name no file uses, and legal as a label.
QString freshName(const QString &oldName, const LabelIndex::Index &index)
{
    for (int n = 0;; ++n)
    {
        const QString candidate = QStringLiteral("zz_renamed_%1").arg(n);
        if (!index.isDefined(candidate) && candidate != oldName)
            return candidate;
    }
}
} // namespace

TEST(RenameRealFiles, EveryLabelInEveryShippedFileSurvivesARename)
{
    QTemporaryDir scratch;
    ASSERT_TRUE(scratch.isValid());

    int filesSeen = 0, filesWithLabels = 0, renames = 0, failures = 0;
    QStringList failed, skipped;

    for (const QString &path : everyRealFile())
    {
        ++filesSeen;
        const QString original = readAll(path);
        if (original.isEmpty())
            continue;
        const LabelIndex::Index index = LabelIndex::findLabels(original);
        if (index.definitions.isEmpty())
            continue;
        ++filesWithLabels;

        const QString base = QFileInfo(path).fileName();
        const Assembled before =
            assemble(original, scratch.path() + "/" + base + ".before.s");

        // Every DISTINCT name the file defines.
        std::set<QString> names;
        for (const LabelIndex::Definition &d : index.definitions)
            names.insert(d.name);

        for (const QString &from : names)
        {
            // A name the assembler does not see is not a label it can
            // check. Recorded rather than silently dropped: a name the
            // index calls a definition and the assembler does not is
            // itself worth knowing about.
            if (!before.symbols.count(from.toStdString()))
            {
                skipped << QStringLiteral("%1: '%2' (the assembler has no such symbol%3)")
                               .arg(base, from,
                                    before.complaints().isEmpty()
                                        ? QString()
                                        : QStringLiteral("; the file already fails to parse"));
                continue;
            }

            const QString to    = freshName(from, index);
            const QString after = renameThroughEditor(original, from, to);
            if (after.isEmpty() || after == original)
            {
                ++failures;
                failed << QStringLiteral("%1: rename of '%2' did not happen").arg(base, from);
                continue;
            }
            ++renames;

            const Assembled now =
                assemble(after, scratch.path() + "/" + base + ".after.s");

            auto fail = [&](const QString &why)
            { ++failures; failed << QStringLiteral("%1 [%2]: %3").arg(base, from, why); };

            if (now.complaints() != before.complaints())
            {
                fail(QStringLiteral("a diagnostic changed:\n  before: %1\n  after:  %2")
                         .arg(before.complaints(), now.complaints()));
                continue;
            }
            if (!now.symbols.count(to.toStdString()))
            { fail(QStringLiteral("the assembler does not see the new name")); continue; }
            if (now.symbols.count(from.toStdString()))
            { fail(QStringLiteral("the old name is still a symbol")); continue; }
            if (now.symbols.at(to.toStdString()).line_number !=
                before.symbols.at(from.toStdString()).line_number)
            { fail(QStringLiteral("the new name is on a different line")); continue; }
            if (now.symbols.size() != before.symbols.size())
            { fail(QStringLiteral("the symbol table changed size")); continue; }

            for (const auto &[name, data] : before.symbols)
            {
                if (name == from.toStdString())
                    continue;
                if (!now.symbols.count(name))
                { fail(QStringLiteral("'%1' disappeared").arg(QString::fromStdString(name))); break; }
                if (now.symbols.at(name).line_number != data.line_number)
                { fail(QStringLiteral("'%1' moved").arg(QString::fromStdString(name))); break; }
            }
        }
    }

    std::cerr << "    [ info ] files seen " << filesSeen
              << ", files defining labels " << filesWithLabels
              << ", renames performed " << renames
              << ", failures " << failures << "\n";
    std::cerr << "    [ info ] names skipped " << skipped.size() << "\n";
    for (const QString &sk : skipped)
        std::cerr << "    [ skip  ] " << sk.toStdString() << "\n";
    for (const QString &f : failed)
        std::cerr << "    [ FAIL  ] " << f.toStdString() << "\n";

    EXPECT_GT(filesWithLabels, 0) << "no shipped file defines a label; the paths are wrong";
    EXPECT_GT(renames, 0) << "no rename was performed";
    EXPECT_EQ(failures, 0);
}

/// The control: skip the references and the files must break.
TEST(RenameRealFiles, RenamingOnlyTheDefinitionBreaksThem)
{
    QTemporaryDir scratch;
    ASSERT_TRUE(scratch.isValid());

    int tried = 0, broke = 0;
    for (const QString &path : everyRealFile())
    {
        const QString original = readAll(path);
        if (original.isEmpty())
            continue;
        const LabelIndex::Index index = LabelIndex::findLabels(original);
        if (index.references.isEmpty())
            continue; // nothing to leave dangling

        const QString base = QFileInfo(path).fileName();
        const Assembled before =
            assemble(original, scratch.path() + "/" + base + ".cb.s");
        if (!before.complaints().isEmpty())
            continue; // already failing; a NEW failure cannot be seen

        const QString from = index.references.first().name;
        if (!before.symbols.count(from.toStdString()))
            continue;
        const QString to    = freshName(from, index);
        const QString after = renameThroughEditor(original, from, to, true);
        if (after.isEmpty() || after == original)
            continue;
        ++tried;
        const Assembled now = assemble(after, scratch.path() + "/" + base + ".ca.s");
        if (now.complaints() != before.complaints())
            ++broke;
    }
    std::cerr << "    [ info ] definition-only renames tried " << tried
              << ", files the assembler then complained about " << broke << "\n";
    EXPECT_GT(tried, 0) << "the control never ran on a real file";
    EXPECT_EQ(broke, tried)
        << "renaming only the definition left some real file assembling cleanly, "
           "so this check cannot tell a correct rename from a broken one";
}
