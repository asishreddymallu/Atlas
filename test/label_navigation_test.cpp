/**
 * @file label_navigation_test.cpp
 * @brief F12 and Alt+Left through real keys.
 *
 * Every case is a GUARD. The pure rules are in label_index_test.cpp; what
 * is here is the wiring, the back stack, and the things only a widget can
 * answer.
 */
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QKeyEvent>
#include <QTextBlock>
#include <QTextDocument>

using namespace Kites;

namespace
{
void key(QWidget *w, Qt::Key k, Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QKeyEvent press(QEvent::KeyPress, k, mods);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, k, mods);
    QApplication::sendEvent(w, &release);
    QApplication::processEvents();
}

struct Nav
{
    Editor editor;
    Nav()
    {
        editor.resize(700, 400);
        editor.show();
        QApplication::processEvents();
    }
    void load(const QString &text)
    {
        editor.setPlainText(text);
        QApplication::processEvents();
    }
    void caretTo(int offset)
    {
        QTextCursor c(editor.document());
        c.setPosition(offset);
        editor.setTextCursor(c);
        QApplication::processEvents();
    }
    int  caret() const { return editor.textCursor().position(); }
    int  line() const { return editor.textCursor().blockNumber(); }
};

/// main: on line 0, a loop label on line 2, references on lines 3 and 5.
const QString kProgram =
    "main:\n"          // 0
    "    addi x1, x0, 4\n"  // 1
    "loop:\n"          // 2
    "    j loop\n"     // 3
    "    nop\n"        // 4
    "    j loop\n";    // 5
} // namespace

TEST(LabelNavigation, F12OnAReferenceJumpsToTheDefinition)
{
    Nav nav;
    nav.load(kProgram);
    // Put the caret on the "loop" of line 3.
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    nav.caretTo(reference + 1);
    ASSERT_EQ(nav.line(), 3);

    key(&nav.editor, Qt::Key_F12);
    EXPECT_EQ(nav.line(), 2) << "F12 did not land on the definition";
    EXPECT_EQ(nav.caret(), kProgram.indexOf("loop:"));
}

TEST(LabelNavigation, F12ElsewhereDoesNothingSilently)
{
    Nav nav;
    nav.load(kProgram);
    nav.caretTo(kProgram.indexOf("    nop") + 2); // inside the whitespace/nop
    const int before = nav.caret();
    key(&nav.editor, Qt::Key_F12);
    EXPECT_EQ(nav.caret(), before) << "F12 moved the caret off a non-label";
    EXPECT_EQ(nav.editor.backStackDepth(), 0) << "it pushed a position anyway";
}

TEST(LabelNavigation, F12OnAnUndefinedNameDoesNotJump)
{
    Nav nav;
    nav.load("main:\n    j nowhere\n");
    nav.caretTo(QString("main:\n    j now").size());
    const int before = nav.caret();
    key(&nav.editor, Qt::Key_F12);
    EXPECT_EQ(nav.caret(), before) << "it jumped somewhere for an undefined name";
    EXPECT_EQ(nav.editor.backStackDepth(), 0);
}

TEST(LabelNavigation, F12OnTheDefinitionItselfStaysPut)
{
    Nav nav;
    nav.load(kProgram);
    nav.caretTo(kProgram.indexOf("loop:") + 2);
    const int before = nav.caret();
    key(&nav.editor, Qt::Key_F12);
    EXPECT_EQ(nav.caret(), before)
        << "F12 on the sole definition should not move - there is nowhere to go";
    EXPECT_EQ(nav.editor.backStackDepth(), 0);
}

TEST(LabelNavigation, AJumpAndABack)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    nav.caretTo(reference + 1);
    const int from = nav.caret();

    key(&nav.editor, Qt::Key_F12);
    ASSERT_EQ(nav.line(), 2);
    EXPECT_EQ(nav.editor.backStackDepth(), 1);

    key(&nav.editor, Qt::Key_Left, Qt::AltModifier);
    EXPECT_EQ(nav.caret(), from) << "Alt+Left did not come back to where it left";
    EXPECT_EQ(nav.editor.backStackDepth(), 0);
}

TEST(LabelNavigation, ThreeJumpsAndThreeBacks)
{
    Nav nav;
    nav.load(kProgram);
    QList<int> from;
    // Jump from line 3, then from line 5, then from line 3 again.
    for (int occurrence : {0, 1, 0})
    {
        int at = -1;
        for (int i = 0; i <= occurrence; ++i)
            at = kProgram.indexOf("    j loop", at + 1);
        nav.caretTo(at + 7);
        from << nav.caret();
        key(&nav.editor, Qt::Key_F12);
        ASSERT_EQ(nav.line(), 2);
    }
    EXPECT_EQ(nav.editor.backStackDepth(), 3);

    for (int i = from.size() - 1; i >= 0; --i)
    {
        key(&nav.editor, Qt::Key_Left, Qt::AltModifier);
        EXPECT_EQ(nav.caret(), from.at(i)) << "back step " << i << " landed wrong";
    }
    EXPECT_EQ(nav.editor.backStackDepth(), 0);
}

TEST(LabelNavigation, BackOnAnEmptyStackDoesNothing)
{
    Nav nav;
    nav.load(kProgram);
    nav.caretTo(10);
    const int before = nav.caret();
    key(&nav.editor, Qt::Key_Left, Qt::AltModifier);
    EXPECT_EQ(nav.caret(), before) << "Alt+Left moved with nothing on the stack";
}

TEST(LabelNavigation, AnEditAboveAStoredPositionCarriesItAlong)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    nav.caretTo(reference + 1);
    key(&nav.editor, Qt::Key_F12);
    ASSERT_EQ(nav.editor.backStackDepth(), 1);

    // Insert a whole line at the very top: every offset below shifts.
    QTextCursor top(nav.editor.document());
    top.setPosition(0);
    top.insertText("# a new first line\n");
    QApplication::processEvents();

    key(&nav.editor, Qt::Key_Left, Qt::AltModifier);
    // The stored position was on the old line 3, which is now line 4.
    EXPECT_EQ(nav.line(), 4)
        << "the stored position did not move with the text above it";
}

TEST(LabelNavigation, TheBackStackIsClearedOnOpen)
{
    EditorTab tab;
    tab.resize(700, 400);
    tab.show();
    QApplication::processEvents();

    Editor *editor = nullptr;
    for (QPlainTextEdit *pane : tab.findChildren<QPlainTextEdit *>())
        if (!pane->isReadOnly())
            editor = static_cast<Editor *>(pane);
    ASSERT_NE(editor, nullptr);

    tab.loadFileContent(kProgram);
    QApplication::processEvents();
    QTextCursor c(editor->document());
    c.setPosition(kProgram.indexOf("    j loop") + 7);
    editor->setTextCursor(c);
    QApplication::processEvents();
    key(editor, Qt::Key_F12);
    ASSERT_EQ(editor->backStackDepth(), 1);

    tab.loadFileContent("other:\n    nop\n");
    QApplication::processEvents();
    EXPECT_EQ(editor->backStackDepth(), 0)
        << "the back stack still points into the document that was closed";
}

TEST(LabelNavigation, ADuplicateDefinitionGoesToTheFirst)
{
    Nav nav;
    const QString text = "dup:\n    nop\ndup:\n    nop\n    j dup\n";
    nav.load(text);
    nav.caretTo(text.indexOf("    j dup") + 7);
    key(&nav.editor, Qt::Key_F12);
    EXPECT_EQ(nav.line(), 0) << "it did not go to the FIRST definition";
}

TEST(LabelNavigation, TheIndexIsCachedAgainstTheDocumentRevision)
{
    Nav nav;
    nav.load(kProgram);
    const LabelIndex::Index *first = &nav.editor.labelIndex();
    const LabelIndex::Index *again = &nav.editor.labelIndex();
    EXPECT_EQ(first, again) << "the cache handed back a different object";
    EXPECT_EQ(nav.editor.labelIndex().definitions.size(), 2);

    QTextCursor end(nav.editor.document());
    end.movePosition(QTextCursor::End);
    end.insertText("fresh:\n");
    QApplication::processEvents();
    EXPECT_EQ(nav.editor.labelIndex().definitions.size(), 3)
        << "the index did not notice the document changed";
}
