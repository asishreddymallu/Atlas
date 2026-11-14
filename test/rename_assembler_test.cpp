/**
 * @file rename_assembler_test.cpp
 * @brief End to end: a rename done by the editor, read back by the assembler.
 *
 * Every other rename test asks the editor whether it did what it said. This
 * one asks the ASSEMBLER, which is the only thing whose opinion matters: a
 * rename that leaves a file the assembler cannot read is a broken rename
 * however neat the diff looks.
 *
 * For each file it:
 *   1. assembles the original and records the symbol table;
 *   2. renames one label through Editor::openRenameAtCaret()/applyRename(),
 *      the real code path, not a string replace;
 *   3. assembles the result and requires that the new name sits at the SAME
 *      line the old one did, that every other symbol is untouched, and that
 *      no new diagnostic has appeared.
 *
 * The assembler is read, never modified.
 */
#include "assembler/lexer.h"
#include "assembler/parser.h"
#include "ui/editor_tab/label_index.h"
#include "ui/editor_tab/label_overlays.h"
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTextStream>

#include <iostream>
#include <map>
#include <string>

using namespace Kites;

namespace
{
struct Assembled
{
    std::map<std::string, SymbolData> symbols;
    QStringList                       diagnostics; ///< every recorded ParseError
    QString                           thrown;      ///< the exception, if any

    /// Everything the assembler complained about, as one comparable string.
    QString complaints() const
    {
        QStringList all = diagnostics;
        if (!thrown.isEmpty())
            all << thrown;
        all.sort();
        return all.join(QLatin1Char('\n'));
    }
};

/// Runs the real lexer and parser over `text`, through a temp file because
/// Lexer takes a path. The error text is kept so a NEW diagnostic can be
/// told from one the file already had.
Assembled assemble(const QString &text, const QString &dir, const QString &name)
{
    const QString path = dir + "/" + name;
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            Assembled failed;
            failed.thrown = QStringLiteral("could not write %1").arg(path);
            return failed;
        }
        QTextStream(&file) << text;
    }
    Assembled out;
    try
    {
        Lexer  lexer(path.toStdString());
        auto   tokens = lexer.getTokenList();
        Parser parser(path.toStdString(), tokens);
        try
        {
            parser.parse();
        }
        catch (const std::exception &e)
        {
            out.thrown = QString::fromUtf8(e.what());
        }
        out.symbols = parser.getSymbolTable();
        // The RECORDED diagnostics, not only a thrown exception. The parser
        // collects "Invalid label reference: Label reference not found"
        // rather than throwing it, and an earlier version of this test
        // looked only at the exception - which made the whole end-to-end
        // check vacuous, as its own control proved.
        for (const ParseError &error : parser.getErrors())
            out.diagnostics << QStringLiteral("line %1: %2")
                                    .arg(error.line)
                                    .arg(QString::fromStdString(error.message));
    }
    catch (const std::exception &e)
    {
        out.thrown = QString::fromUtf8(e.what());
    }
    return out;
}

void sendKey(QWidget *w, Qt::Key k)
{
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier);
    QApplication::sendEvent(w, &press);
    QApplication::processEvents();
}

/// Renames `from` to `to` through the editor's own F2 path. Returns the
/// document afterwards, or an empty string when the rename did not happen.
QString renameThroughTheEditor(const QString &text, const QString &from,
                               const QString &to, bool definitionOnly = false)
{
    Editor editor;
    editor.resize(700, 400);
    editor.show();
    editor.setPlainText(text);
    QApplication::processEvents();

    const LabelIndex::Index index = LabelIndex::findLabels(text);
    const auto              defs  = index.definitionsOf(from);
    if (defs.isEmpty())
        return {};

    if (definitionOnly)
    {
        // The CONTROL path: rewrite the definition and leave every use
        // pointing at a name that no longer exists.
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
    sendKey(editor.renameBar()->input(), Qt::Key_Return);
    QApplication::processEvents();
    return editor.toPlainText();
}

/// The files. The eight label-heavy trap files live outside the repo, so
/// their content is carried here rather than read from a path the test
/// cannot rely on, plus the two shapes the brief names.
struct Sample
{
    const char *name;
    const char *rename; ///< the label to rename
    const char *text;
};

const Sample kSamples[] = {
    {"L01_basic", "loop",
     "main:\n    addi x1, x0, 10\nloop:\n    addi x1, x1, -1\n    bnez x1, loop\n"
     "    j done\ndone:\n    ret\n"},
    {"L02_label_then_instruction", "start",
     "start: addi x1, x0, 1\n    j start\nnext: nop\n    j next\n"},
    {"L03_mnemonic_named", "add",
     "add:\n    addi x1, x0, 1\n    j add\nsub:\n    j sub\n"},
    {"L04_prefixes", "loop",
     "loop:\n    j loop\nloop2:\n    j loop2\nloop_end:\n    j loop_end\n"},
    {"L05_comment_and_string", "loop",
     "loop:\n    j loop          # loop again\n    .asciz \"loop forever\"\n"
     "    j loop\n"},
    {"L06_underscores_digits", "a_1",
     "a_1:\n    j a_1\n_b2:\n    j _b2\nc3_d:\n    j c3_d\n"},
    {"L07_data_labels", "values",
     ".data\nvalues:\n    .word 1, 2, 3\n.text\nmain:\n    la x1, values\n"
     "    j main\n"},
    {"L08_many_refs", "target",
     "target:\n    j target\n    j target\n    j target\n    j target\n"
     "    beqz x1, target\n    bnez x1, target\n"},
    // The two shapes the brief names explicitly.
    {"two_labels_on_one_line", "a", "a: b:\n    j a\n    j b\n"},
    {"label_before_an_instruction", "c",
     "c: addi x1, x0, 1\n    j c\n    bnez x1, c\n"},

    // ── one sample per reference FORM the assembler accepts. The forms
    //    were enumerated by probing the real lexer and parser over every
    //    context a label could appear in; see editor_shortcuts.md for the
    //    table, including the forms this assembler does NOT support
    //    (`call`, `tail`, `la` on a text label, %hi/%lo, label+offset,
    //    a label as a .word operand, .globl/.type/.size/.equ).
    {"form_j", "target", ".text\ntarget:\n    j target\n"},
    {"form_jal_rd", "target", ".text\ntarget:\n    jal ra, target\n"},
    {"form_beq", "target", ".text\ntarget:\n    beq x1, x2, target\n"},
    {"form_bne", "target", ".text\ntarget:\n    bne x1, x2, target\n"},
    {"form_blt", "target", ".text\ntarget:\n    blt x1, x2, target\n"},
    {"form_bge", "target", ".text\ntarget:\n    bge x1, x2, target\n"},
    {"form_bltu", "target", ".text\ntarget:\n    bltu x1, x2, target\n"},
    {"form_bgeu", "target", ".text\ntarget:\n    bgeu x1, x2, target\n"},
    {"form_beqz", "target", ".text\ntarget:\n    beqz x1, target\n"},
    {"form_bnez", "target", ".text\ntarget:\n    bnez x1, target\n"},
    {"form_bgez", "target", ".text\ntarget:\n    bgez x1, target\n"},
    {"form_la_data", "target",
     ".data\ntarget:\n    .word 1\n.text\nmain:\n    la x1, target\n"},
    {"form_lw_data", "target",
     ".data\ntarget:\n    .word 1\n.text\nmain:\n    lw x1, target\n"},
    {"form_label_on_instruction_line", "target", ".text\ntarget: nop\n    j target\n"},
    {"form_hash_comment", "target",
     ".text\ntarget:\n    j target   # target again\n"},
    {"form_semicolon_comment", "target",
     ".text\ntarget:\n    j target   ; target again\n"},
    {"form_string_mentions_it", "target",
     ".data\nmsg:\n    .string \"target\"\n.text\ntarget:\n    j target\n"},
    {"form_many_refs_mixed", "target",
     ".text\ntarget:\n    j target\n    beq x1, x2, target\n"
     "    bnez x1, target\n    jal ra, target\n    j target  ; target\n"},
};
} // namespace

TEST(RenameAssembler, TheAssemblerReadsBackEveryRename)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    int checked = 0, symbolsChecked = 0;
    for (const Sample &sample : kSamples)
    {
        SCOPED_TRACE(sample.name);
        const QString original = QString::fromUtf8(sample.text);
        const QString from     = QString::fromUtf8(sample.rename);
        const QString to       = QStringLiteral("renamed_%1").arg(from);

        const Assembled before =
            assemble(original, dir.path(), QStringLiteral("%1_before.s").arg(sample.name));
        ASSERT_TRUE(before.symbols.count(from.toStdString()))
            << "the assembler does not even see '" << sample.rename
            << "' in the original; the sample is wrong, not the rename";

        const QString after = renameThroughTheEditor(original, from, to);
        ASSERT_FALSE(after.isEmpty()) << "the rename did not happen at all";
        ASSERT_NE(after, original);

        const Assembled now =
            assemble(after, dir.path(), QStringLiteral("%1_after.s").arg(sample.name));

        // No NEW diagnostic. A file that already failed may keep failing in
        // the same way; what must not happen is a failure that appears.
        EXPECT_EQ(now.complaints(), before.complaints())
            << "the rename introduced a diagnostic:\n"
            << now.complaints().toStdString();
        // A NEW one. A sample that already produces this diagnostic may
        // keep producing it; what must not happen is one appearing.
        EXPECT_LE(now.complaints().count(QStringLiteral("Invalid label reference")),
                  before.complaints().count(QStringLiteral("Invalid label reference")))
            << "before:\n"
            << before.complaints().toStdString() << "\nafter:\n"
            << now.complaints().toStdString();

        // The new name sits where the old one did.
        ASSERT_TRUE(now.symbols.count(to.toStdString()))
            << "the assembler does not see the new name at all";
        EXPECT_FALSE(now.symbols.count(from.toStdString()))
            << "the old name is still a symbol";
        EXPECT_EQ(now.symbols.at(to.toStdString()).line_number,
                  before.symbols.at(from.toStdString()).line_number)
            << "the new name is on a different line from the old one";
        ++symbolsChecked;

        // Every OTHER symbol is exactly where it was.
        for (const auto &[name, data] : before.symbols)
        {
            if (name == from.toStdString())
                continue;
            ASSERT_TRUE(now.symbols.count(name))
                << "'" << name << "' disappeared";
            EXPECT_EQ(now.symbols.at(name).line_number, data.line_number)
                << "'" << name << "' moved";
            ++symbolsChecked;
        }
        // A comment or a string that happens to say the old name must be
        // left exactly as it was. The symbol table cannot see either, so
        // this is checked on the text.
        for (const QString &untouched :
             {QStringLiteral("# target again"), QStringLiteral("; target again"),
              QStringLiteral("\"target\"")})
        {
            if (original.contains(untouched))
                EXPECT_TRUE(after.contains(untouched))
                    << "the rename rewrote " << untouched.toStdString();
        }
        ++checked;
    }
    EXPECT_EQ(checked, int(std::size(kSamples)));
    // Reported rather than thresholded on a number picked out of the air:
    // the figure that matters is that every symbol of every sample was
    // compared, which the loop above does unconditionally.
    EXPECT_GE(symbolsChecked, int(std::size(kSamples)))
        << "fewer symbols compared than there are samples";
    std::cerr << "    [ info ] " << std::size(kSamples) << " files, "
              << symbolsChecked << " symbols compared\n";
}

/// The control the brief asks for, kept as a test of its own so the main
/// case cannot pass by accident: renaming ONLY the definition must leave
/// the assembler complaining about the uses.
TEST(RenameAssembler, RenamingOnlyTheDefinitionBreaksTheFile)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    int broke = 0;
    for (const Sample &sample : kSamples)
    {
        const QString original = QString::fromUtf8(sample.text);
        const QString from     = QString::fromUtf8(sample.rename);
        const QString to       = QStringLiteral("renamed_%1").arg(from);

        const Assembled before =
            assemble(original, dir.path(), QStringLiteral("%1_cb.s").arg(sample.name));
        if (!before.complaints().isEmpty())
            continue; // it was already failing; it cannot show a NEW failure

        const QString after = renameThroughTheEditor(original, from, to, true);
        ASSERT_FALSE(after.isEmpty());
        const Assembled now =
            assemble(after, dir.path(), QStringLiteral("%1_ca.s").arg(sample.name));
        if (now.complaints() != before.complaints())
            ++broke;
    }
    EXPECT_GT(broke, 0)
        << "renaming only the definition left every file assembling cleanly, "
           "so the end-to-end check cannot tell a correct rename from a "
           "broken one";
}
