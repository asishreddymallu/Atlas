/**
 * @file label_navigation_test.cpp
 * @brief F12 and Alt+Left through real keys.
 *
 * Every case is a GUARD. The pure rules are in label_index_test.cpp; what
 * is here is the wiring, the back stack, and the things only a widget can
 * answer.
 */
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/completion_model.h"
#include "ui/editor_tab/label_index.h"
#include "ui/editor_tab/label_overlays.h"
#include "ui/editor_tab/find_bar.h"
#include "ui/editor_tab/goto_line_bar.h"
#include <algorithm>
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QKeyEvent>
#include <QTextBlock>
#include <QLineEdit>
#include <iostream>
#include <QTextDocument>

using namespace Atlas;

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

// ════════════════════════ Ctrl+click and the hover link ══════════════════

namespace
{
/// The viewport point of `offset`, for a synthetic mouse event.
QPoint pointOf(Editor &editor, int offset)
{
    QTextCursor at(editor.document());
    at.setPosition(offset);
    const QRect r = editor.cursorRect(at);
    return QPoint(r.left() + 2, r.center().y());
}
// Mouse events go to the VIEWPORT, not the editor. QAbstractScrollArea
// forwards them from there to the editor's handlers with coordinates in
// viewport space - which is the space cursorRect() and cursorForPosition()
// both speak. Sending them straight to the editor delivers coordinates
// offset by the gutter, and every one of these cases failed that way first.
void moveMouse(Editor &editor, QPoint where, Qt::KeyboardModifiers mods)
{
    QMouseEvent move(QEvent::MouseMove, where, editor.viewport()->mapToGlobal(where),
                     Qt::NoButton, Qt::NoButton, mods);
    QApplication::sendEvent(editor.viewport(), &move);
    QApplication::processEvents();
}
void clickMouse(Editor &editor, QPoint where, Qt::KeyboardModifiers mods)
{
    QMouseEvent press(QEvent::MouseButtonPress, where, editor.viewport()->mapToGlobal(where),
                      Qt::LeftButton, Qt::LeftButton, mods);
    QApplication::sendEvent(editor.viewport(), &press);
    QApplication::processEvents();
}
} // namespace

TEST(LabelLink, CtrlClickNavigatesLikeF12)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    clickMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::ControlModifier);
    EXPECT_EQ(nav.line(), 2) << "Ctrl+click did not reach the definition";
    EXPECT_EQ(nav.editor.backStackDepth(), 1) << "it did not push the back stack";
}

TEST(LabelLink, APlainClickIsUnchanged)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    clickMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::NoModifier);
    EXPECT_EQ(nav.line(), 3) << "a plain click navigated";
    EXPECT_EQ(nav.editor.backStackDepth(), 0);
    EXPECT_TRUE(nav.editor.linkedName().isEmpty());
}

TEST(LabelLink, CtrlHoverUnderlinesADefinedLabelOnly)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    moveMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::ControlModifier);
    EXPECT_EQ(nav.editor.linkedName(), QString("loop"));

    // Over a register, which is not a label.
    const int register_ = kProgram.indexOf("x1");
    moveMouse(nav.editor, pointOf(nav.editor, register_), Qt::ControlModifier);
    EXPECT_TRUE(nav.editor.linkedName().isEmpty()) << "it underlined something that is not a label";
}

TEST(LabelLink, HoveringWithoutCtrlUnderlinesNothing)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    moveMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::NoModifier);
    EXPECT_TRUE(nav.editor.linkedName().isEmpty());
}

// ── the four clear paths. A stuck pointing hand is the classic bug, so
//    each one is its own case and each has its own negative control.

TEST(LabelLinkClears, ReleasingCtrlClearsIt)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    moveMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::ControlModifier);
    ASSERT_EQ(nav.editor.linkedName(), QString("loop"));

    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
    QApplication::sendEvent(&nav.editor, &release);
    QApplication::processEvents();
    EXPECT_TRUE(nav.editor.linkedName().isEmpty()) << "releasing Ctrl left the link up";
}

TEST(LabelLinkClears, TheMouseLeavingClearsIt)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    moveMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::ControlModifier);
    ASSERT_EQ(nav.editor.linkedName(), QString("loop"));

    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&nav.editor, &leave);
    QApplication::processEvents();
    EXPECT_TRUE(nav.editor.linkedName().isEmpty()) << "the mouse left and the link stayed";
}

TEST(LabelLinkClears, FocusLossClearsIt)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    moveMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::ControlModifier);
    ASSERT_EQ(nav.editor.linkedName(), QString("loop"));

    QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(&nav.editor, &out);
    QApplication::processEvents();
    EXPECT_TRUE(nav.editor.linkedName().isEmpty()) << "focus loss left the link up";
}

TEST(LabelLinkClears, MovingOffTheLabelClearsIt)
{
    Nav nav;
    nav.load(kProgram);
    const int reference = kProgram.indexOf("loop", kProgram.indexOf("    j loop"));
    moveMouse(nav.editor, pointOf(nav.editor, reference + 1), Qt::ControlModifier);
    ASSERT_EQ(nav.editor.linkedName(), QString("loop"));

    // Same line, but over the "j" - not a defined label.
    moveMouse(nav.editor, pointOf(nav.editor, reference - 2), Qt::ControlModifier);
    EXPECT_TRUE(nav.editor.linkedName().isEmpty())
        << "the link stayed on a label the mouse is no longer over";
}

// ══════════════ one definition of "this line defines a label" ════════════

/**
 * @brief The editor's completion scanner and the navigation index agree.
 *
 * They each carried their own copy of the rule until scanLineLabels() was
 * made to delegate to LabelIndex::definitionsOnLine(). Nothing in the
 * compiler stops them drifting apart again.
 *
 * This goes through the EDITOR - the Label-kind items its installed
 * completion source offers, which is the scanner the application uses -
 * and compares them with findLabels() over the same text. Comparing two
 * index functions instead would pass however far the scanner drifted,
 * which is what the first version of this case did.
 */
TEST(LabelScannerConsistency, TheCompletionScannerAgreesWithTheIndex)
{
    const QStringList documents = {
        "main:\n    nop     # loop: not a label\n",
        "msg:\n    .asciz \"inside: not a label\"\n",
        "add:\n    nop\n    j add\n",
        "loop:\nloop2:\nloop_end:\n    j loop\n",
        "first: second: nop\n",
        "start: addi x1, x0, 1\n",
        "        indented:\n    nop\n",
        "has.dot:\n    nop\n",
        "main:\r\n    j main\r\n",
        "main:\n    nop\nlast:",
        "",
        "main:\n    addi x1, x0, 4\nloop:\n    j loop\n    ret\n",
    };

    Nav nav;
    for (const QString &text : documents)
    {
        nav.load(text);

        // What the editor's own scanner offers, as labels.
        QStringList fromScanner;
        for (const Completion::Item &item : nav.editor.completionItemsForTest(QString()))
            if (item.kind == Completion::Kind::Label)
                fromScanner << item.label;
        fromScanner.sort();

        // What the index says is defined.
        QStringList fromIndex;
        for (const LabelIndex::Definition &d : LabelIndex::findLabels(text).definitions)
            fromIndex << d.name;
        fromIndex.removeDuplicates();
        fromIndex.sort();

        EXPECT_EQ(fromScanner, fromIndex)
            << "the completion scanner and the index disagree on:\n"
            << text.toStdString();
    }
}

// ═══════════════════════ Shift+F12: find references ═════════════════════

namespace
{
/// main: line 0; loop: line 2; references on 3 and 5; a comment and a
/// string each mention "loop" and must NOT be listed.
const QString kRefs =
    "main:\n"                       // 0
    "    addi x1, x0, 4\n"          // 1
    "loop:\n"                       // 2
    "    j loop\n"                  // 3
    "    .asciz \"loop here\"\n"    // 4
    "    j loop        # loop\n"    // 5
    "    ret\n";                    // 6
} // namespace

TEST(LabelReferences, ShiftF12OpensTheListFromTheIndex)
{
    Nav nav;
    nav.load(kRefs);
    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);

    ReferencesBar *bar = nav.editor.referencesBar();
    ASSERT_NE(bar, nullptr);
    ASSERT_TRUE(bar->isVisible()) << "Shift+F12 did not open the list";
    // The definition plus the two real references. The mention inside the
    // string on line 4 and the one in the comment on line 5 are not
    // references - the list is built from the index, not a text search.
    EXPECT_EQ(bar->count(), 3) << "the list is not the index's answer";
}

TEST(LabelReferences, CommentsAndStringsAreNotListed)
{
    Nav nav;
    nav.load(kRefs);
    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ReferencesBar *bar = nav.editor.referencesBar();
    ASSERT_TRUE(bar->isVisible());

    QList<int> lines;
    for (int i = 0; i < bar->count(); ++i)
    {
        bar->setCurrentRow(i);
        ASSERT_NE(bar->selected(), nullptr);
        lines << bar->selected()->line;
    }
    std::sort(lines.begin(), lines.end());
    EXPECT_EQ(lines, QList<int>({2, 3, 5}))
        << "a mention in a string or a comment was listed";
}

TEST(LabelReferences, EnterJumpsAndPushesTheBackStack)
{
    Nav nav;
    nav.load(kRefs);
    nav.caretTo(kRefs.indexOf("loop:") + 1);
    const int from = nav.caret();
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ReferencesBar *bar = nav.editor.referencesBar();
    ASSERT_TRUE(bar->isVisible());

    key(&nav.editor, Qt::Key_Down);
    key(&nav.editor, Qt::Key_Return);
    EXPECT_FALSE(bar->isVisible()) << "picking a row left the list open";
    EXPECT_NE(nav.caret(), from) << "picking a row did not move the caret";
    EXPECT_EQ(nav.editor.backStackDepth(), 1) << "it did not push the back stack";

    key(&nav.editor, Qt::Key_Left, Qt::AltModifier);
    EXPECT_EQ(nav.caret(), from) << "Alt+Left did not return";
}

TEST(LabelReferences, UpAndDownMoveTheSelection)
{
    Nav nav;
    nav.load(kRefs);
    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ReferencesBar *bar = nav.editor.referencesBar();
    ASSERT_TRUE(bar->isVisible());
    ASSERT_GT(bar->count(), 1);

    EXPECT_EQ(bar->currentRow(), 0);
    key(&nav.editor, Qt::Key_Down);
    EXPECT_EQ(bar->currentRow(), 1);
    key(&nav.editor, Qt::Key_Up);
    EXPECT_EQ(bar->currentRow(), 0);
    key(&nav.editor, Qt::Key_Up);
    EXPECT_EQ(bar->currentRow(), bar->count() - 1) << "Up on the first row did not wrap";
}

TEST(LabelReferences, EscClosesTheListOnlyAndLeavesTheFindBarOpen)
{
    Nav nav;
    nav.load(kRefs);
    nav.editor.openFind(false);
    QApplication::processEvents();
    ASSERT_FALSE(nav.editor.findBar()->isHidden());

    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ReferencesBar *bar = nav.editor.referencesBar();
    ASSERT_TRUE(bar->isVisible());
    const int before = nav.caret();

    key(&nav.editor, Qt::Key_Escape);
    EXPECT_FALSE(bar->isVisible()) << "Esc did not close the list";
    EXPECT_EQ(nav.caret(), before) << "Esc moved the caret";
    EXPECT_FALSE(nav.editor.findBar()->isHidden())
        << "Esc closed the find bar as well; the list must take it first";
}

TEST(LabelReferences, OpeningItClosesTheOtherOverlays)
{
    Nav nav;
    nav.load(kRefs);
    nav.editor.openGotoLine();
    QApplication::processEvents();
    ASSERT_FALSE(nav.editor.gotoLineBar()->isHidden());

    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ASSERT_TRUE(nav.editor.referencesBar()->isVisible());
    EXPECT_TRUE(nav.editor.gotoLineBar()->isHidden())
        << "opening the references list left go-to-line up";

    // And the other way: opening Find or go-to-line closes the list.
    nav.editor.openFind(false);
    EXPECT_FALSE(nav.editor.referencesBar()->isVisible())
        << "opening Find left the references list up";
}

/// The one overlay the list does NOT close, because Esc has to be able to
/// close the list alone and leave the search running. They own the same
/// corner, so the list must be placed clear of the find bar, not over it.
TEST(LabelReferences, ItOpensClearOfAnOpenFindBar)
{
    Nav nav;
    nav.editor.resize(700, 420);
    nav.editor.show();
    QApplication::processEvents();
    nav.load(kRefs);
    nav.editor.openFind(false);
    QApplication::processEvents();
    ASSERT_FALSE(nav.editor.findBar()->isHidden());

    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ASSERT_TRUE(nav.editor.referencesBar()->isVisible());
    EXPECT_FALSE(nav.editor.findBar()->isHidden())
        << "opening the list closed the search the reader was in the middle of";
    EXPECT_GE(nav.editor.referencesBar()->geometry().top(),
              nav.editor.findBar()->geometry().bottom())
        << "the list was drawn on top of the find bar";
}

TEST(LabelReferences, ItClosesTheSuggestionList)
{
    Nav nav;
    nav.load(kRefs);
    nav.editor.setCompletionSource(
        [](const QString &)
        {
            return QList<Completion::Item>{
                {QStringLiteral("loopy"), Completion::Kind::Label, {}}};
        });
    nav.editor.resize(700, 420);
    nav.editor.show();
    QApplication::processEvents();
    nav.caretTo(kRefs.indexOf("loop:") + 1);
    nav.editor.openCompletionExplicitly(); // what Ctrl+Space does
    QApplication::processEvents();
    ASSERT_TRUE(nav.editor.isCompletionVisible());

    // Driven DIRECTLY, not through the key. keyPressEvent() already calls
    // closeCompletion() after any editing key, so the key path would pass
    // even if showReferencesAtCaret() did nothing about the list - a
    // control proved exactly that. The harness opens it this way too.
    ASSERT_TRUE(nav.editor.showReferencesAtCaret());
    EXPECT_FALSE(nav.editor.isCompletionVisible())
        << "opening the references list left the suggestions up";

    // And through the key, which is what a reader actually presses.
    nav.editor.closeReferences();
    nav.editor.openCompletionExplicitly();
    QApplication::processEvents();
    ASSERT_TRUE(nav.editor.isCompletionVisible());
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    EXPECT_FALSE(nav.editor.isCompletionVisible());
}

TEST(LabelReferences, AReadOnlyPaneOpensItToo)
{
    Nav nav;
    nav.load(kRefs);
    nav.editor.setReadOnly(true);
    nav.caretTo(kRefs.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    EXPECT_TRUE(nav.editor.referencesBar()->isVisible())
        << "navigation is not an edit; a read-only pane must still list them";
    nav.editor.setReadOnly(false);
}

TEST(LabelReferences, ANameWithNoReferencesStillListsItsDefinition)
{
    Nav nav;
    nav.load("alone:\n    nop\n");
    nav.caretTo(1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ReferencesBar *bar = nav.editor.referencesBar();
    ASSERT_TRUE(bar->isVisible()) << "it refused to open for a label with no uses";
    EXPECT_EQ(bar->count(), 1);
}

TEST(LabelReferences, ShiftF12OffALabelDoesNothing)
{
    Nav nav;
    nav.load(kRefs);

    // On a real WORD that is not a label - a mnemonic, then a register.
    // The first version of this put the caret in the leading whitespace,
    // where nameAt() returns empty and nothing could open whatever the
    // code did; it passed against the unwired stub for that reason. A word
    // is what a control can actually break.
    for (const char *word : {"addi", "x1", "ret"})
    {
        nav.caretTo(kRefs.indexOf(word) + 1);
        key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
        EXPECT_FALSE(nav.editor.referencesBar()->isVisible())
            << "it opened on '" << word << "', which is not a label of this file";
        nav.editor.closeReferences();
    }

    // And in the whitespace, where there is no word at all.
    nav.caretTo(kRefs.indexOf("    addi") + 2);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    EXPECT_FALSE(nav.editor.referencesBar()->isVisible())
        << "it opened with the caret on whitespace";
}

TEST(LabelReferences, FileOpenClosesIt)
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

    tab.loadFileContent(kRefs);
    QApplication::processEvents();
    QTextCursor c(editor->document());
    c.setPosition(kRefs.indexOf("loop:") + 1);
    editor->setTextCursor(c);
    QApplication::processEvents();
    key(editor, Qt::Key_F12, Qt::ShiftModifier);
    ASSERT_TRUE(editor->referencesBar()->isVisible());

    tab.loadFileContent("other:\n    nop\n");
    QApplication::processEvents();
    EXPECT_FALSE(editor->referencesBar()->isVisible())
        << "the list still points into the document that was closed";
}

// ═══════════════════════════ F2: rename ══════════════════════════════════

namespace
{
/// `loop` is defined on line 1 and used on lines 2 and 6. `loop2` and `loop_end` are
/// separate labels that a careless substring rewrite would damage; the
/// comment and the string each say "loop" and must not be touched.
const QString kRen =
    "main:\n"                          // 0
    "loop:\n"                          // 1
    "    j loop\n"                     // 2
    "loop2:\n"                         // 3
    "    j loop2\n"                    // 4
    "    .asciz \"loop forever\"\n"    // 5
    "    j loop         # loop here\n" // 6
    "loop_end:\n"                      // 7
    "    j loop_end\n"                 // 8
    "    ret\n";                       // 9

void typeInto(QLineEdit *input, const QString &text)
{
    input->setText(text);
    QApplication::processEvents();
}

/// Opens the rename overlay on the label at `offset` and returns the bar.
RenameBar *openRename(Nav &nav, int offset)
{
    nav.caretTo(offset);
    key(&nav.editor, Qt::Key_F2);
    return nav.editor.renameBar();
}
} // namespace

TEST(LabelRename, F2OpensOnTheLabelAtTheCaret)
{
    Nav nav;
    nav.load(kRen);
    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    ASSERT_NE(bar, nullptr);
    ASSERT_TRUE(bar->isVisible()) << "F2 did not open the rename overlay";
    EXPECT_EQ(bar->newName(), QStringLiteral("loop"))
        << "it did not prefill with the name being renamed";
}

TEST(LabelRename, F2OffALabelDoesNothing)
{
    Nav nav;
    nav.load(kRen);
    for (const char *word : {"addi", "ret", "asciz"})
    {
        const int at = kRen.indexOf(word);
        if (at < 0)
            continue;
        nav.caretTo(at + 1);
        key(&nav.editor, Qt::Key_F2);
        EXPECT_FALSE(nav.editor.renameBar()->isVisible())
            << "it opened on '" << word << "', which is not a label";
        nav.editor.closeRename();
    }
}

TEST(LabelRename, ItRewritesEverySiteAndNothingElse)
{
    Nav nav;
    nav.load(kRen);
    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    ASSERT_TRUE(bar->isVisible());
    typeInto(bar->input(), QStringLiteral("spin"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();

    const QString after = nav.editor.toPlainText();
    EXPECT_FALSE(bar->isVisible()) << "it stayed open after a legal rename";

    // The definition and both references moved.
    EXPECT_TRUE(after.contains(QStringLiteral("spin:\n"))) << after.toStdString();
    EXPECT_EQ(after.count(QStringLiteral("j spin")), 2) << after.toStdString();
    // The neighbours did NOT.
    EXPECT_TRUE(after.contains(QStringLiteral("loop2:"))) << "loop2 was damaged";
    EXPECT_TRUE(after.contains(QStringLiteral("j loop2"))) << "a loop2 use was damaged";
    EXPECT_TRUE(after.contains(QStringLiteral("loop_end:"))) << "loop_end was damaged";
    EXPECT_TRUE(after.contains(QStringLiteral("j loop_end"))) << "a loop_end use was damaged";
    // The comment and the string did NOT.
    EXPECT_TRUE(after.contains(QStringLiteral("\"loop forever\"")))
        << "it rewrote inside a string";
    EXPECT_TRUE(after.contains(QStringLiteral("# loop here")))
        << "it rewrote inside a comment";
}

TEST(LabelRename, OneCtrlZUndoesTheWholeRename)
{
    Nav nav;
    nav.load(kRen);
    const QString before = nav.editor.toPlainText();

    // undoCommandAdded() fires once per undo COMMAND, which is the unit
    // Ctrl+Z works in. availableUndoSteps() counts internal operations and
    // reports 7 for a three-site rename either way, so it cannot tell a
    // grouped rename from an ungrouped one.
    int commands = 0;
    QObject::connect(nav.editor.document(), &QTextDocument::undoCommandAdded,
                     [&commands] { ++commands; });

    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    typeInto(bar->input(), QStringLiteral("spin"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();
    ASSERT_NE(nav.editor.toPlainText(), before);

    // The undo STEP COUNT, not just "one Ctrl+Z restored it". A control
    // that removed beginEditBlock/endEditBlock left the round trip
    // passing - Qt coalesces adjacent edits on its own often enough that
    // a single Ctrl+Z happened to work - so the old assertion was blind to
    // exactly the thing this test is named after.
    EXPECT_EQ(commands, 1)
        << "the rename pushed " << commands
        << " undo commands; it must push exactly one";

    int presses = 0;
    while (nav.editor.toPlainText() != before && presses < 20)
    {
        key(&nav.editor, Qt::Key_Z, Qt::ControlModifier);
        ++presses;
    }
    EXPECT_EQ(presses, 1) << "one Ctrl+Z did not put every site back";

    EXPECT_EQ(nav.editor.toPlainText(), before)
        << "one Ctrl+Z did not put every site back";

    key(&nav.editor, Qt::Key_Y, Qt::ControlModifier);
    EXPECT_TRUE(nav.editor.toPlainText().contains(QStringLiteral("spin:")))
        << "Ctrl+Y did not redo the rename";
    EXPECT_EQ(nav.editor.toPlainText().count(QStringLiteral("j spin")), 2);
}

TEST(LabelRename, TheSameNameIsANoOpAndLeavesNoUndoEntry)
{
    Nav nav;
    nav.load(kRen);
    const QString before = nav.editor.toPlainText();
    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    typeInto(bar->input(), QStringLiteral("loop"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();

    EXPECT_FALSE(bar->isVisible()) << "renaming to the same name left it open";
    EXPECT_EQ(nav.editor.toPlainText(), before);
    EXPECT_FALSE(nav.editor.document()->isUndoAvailable())
        << "a no-op rename pushed an undo entry";
}

TEST(LabelRename, ARefusedNameStaysOpenAndSaysWhy)
{
    struct Case { const char *name; const char *mentions; };
    const Case cases[] = {
        {"", "empty"},
        {"has.dot", "cannot be in a label"},
        {"has space", "cannot be in a label"},
        {"1start", "digit"},
        {"x1", "register"},
        {"loop2", "defined"},
    };
    for (const Case &c : cases)
    {
        Nav nav;
        nav.load(kRen);
        const QString before = nav.editor.toPlainText();
        RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
        ASSERT_TRUE(bar->isVisible());
        typeInto(bar->input(), QString::fromLatin1(c.name));
        key(bar->input(), Qt::Key_Return);
        QApplication::processEvents();

        EXPECT_TRUE(bar->isVisible())
            << "'" << c.name << "' was refused but the overlay closed";
        EXPECT_FALSE(bar->reasonText().isEmpty())
            << "'" << c.name << "' was refused with no visible reason";
        EXPECT_TRUE(bar->reasonText().contains(QString::fromLatin1(c.mentions)))
            << "'" << c.name << "' reason did not mention '" << c.mentions
            << "': " << bar->reasonText().toStdString();
        EXPECT_EQ(nav.editor.toPlainText(), before)
            << "'" << c.name << "' was refused but the document changed";
        nav.editor.closeRename();
    }
}

TEST(LabelRename, AReadOnlyPaneRefusesWithAVisibleReason)
{
    Nav nav;
    nav.load(kRen);
    const QString before = nav.editor.toPlainText();
    nav.editor.setReadOnly(true);
    nav.caretTo(kRen.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F2);

    RenameBar *bar = nav.editor.renameBar();
    ASSERT_NE(bar, nullptr);
    // Unlike F12 and Shift+F12, renaming IS an edit. It must say why it
    // cannot happen rather than appearing to do nothing - a key that
    // silently does nothing reads as a broken key.
    ASSERT_TRUE(bar->isVisible())
        << "F2 in a read-only pane did nothing at all, with no reason shown";
    EXPECT_TRUE(bar->reasonText().contains(QStringLiteral("read-only")))
        << bar->reasonText().toStdString();
    EXPECT_FALSE(bar->input()->isEnabled())
        << "it offered a field that cannot do anything";

    typeInto(bar->input(), QStringLiteral("spin"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();
    EXPECT_EQ(nav.editor.toPlainText(), before)
        << "a read-only pane was edited anyway";
    nav.editor.closeRename();
    nav.editor.setReadOnly(false);
}

TEST(LabelRename, EscClosesOnlyIt)
{
    Nav nav;
    nav.load(kRen);
    nav.editor.openFind(false);
    QApplication::processEvents();
    ASSERT_FALSE(nav.editor.findBar()->isHidden());

    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    ASSERT_TRUE(bar->isVisible());
    const QString before = nav.editor.toPlainText();

    key(bar->input(), Qt::Key_Escape);
    QApplication::processEvents();
    EXPECT_FALSE(bar->isVisible()) << "Esc did not close the rename overlay";
    EXPECT_EQ(nav.editor.toPlainText(), before) << "Esc applied the rename";
    EXPECT_FALSE(nav.editor.findBar()->isHidden())
        << "Esc closed the find bar as well";
}

TEST(LabelRename, ItOpensClearOfAnOpenFindBar)
{
    Nav nav;
    nav.load(kRen);
    nav.editor.openFind(false);
    QApplication::processEvents();
    ASSERT_FALSE(nav.editor.findBar()->isHidden());

    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    ASSERT_TRUE(bar->isVisible());
    EXPECT_FALSE(nav.editor.findBar()->isHidden())
        << "opening rename closed the search the reader was in the middle of";
    EXPECT_GE(bar->geometry().top(), nav.editor.findBar()->geometry().bottom())
        << "the rename overlay was drawn on top of the find bar";
}

TEST(LabelRename, OpeningItClosesTheOtherOverlays)
{
    Nav nav;
    nav.load(kRen);

    // go-to-line
    nav.editor.openGotoLine();
    QApplication::processEvents();
    ASSERT_FALSE(nav.editor.gotoLineBar()->isHidden());
    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    ASSERT_TRUE(bar->isVisible());
    EXPECT_TRUE(nav.editor.gotoLineBar()->isHidden())
        << "opening rename left go-to-line up";
    nav.editor.closeRename();

    // the references list
    nav.caretTo(kRen.indexOf("loop:") + 1);
    key(&nav.editor, Qt::Key_F12, Qt::ShiftModifier);
    ASSERT_TRUE(nav.editor.referencesBar()->isVisible());
    openRename(nav, kRen.indexOf("loop:") + 1);
    EXPECT_FALSE(nav.editor.referencesBar()->isVisible())
        << "opening rename left the references list up";
    nav.editor.closeRename();

    // the suggestion list, driven directly for the reason given in
    // ItClosesTheSuggestionList.
    nav.editor.openCompletionExplicitly();
    QApplication::processEvents();
    ASSERT_TRUE(nav.editor.isCompletionVisible());
    nav.caretTo(kRen.indexOf("loop:") + 1);
    ASSERT_TRUE(nav.editor.openRenameAtCaret());
    EXPECT_FALSE(nav.editor.isCompletionVisible())
        << "opening rename left the suggestions up";
}

TEST(LabelRename, FileOpenClosesIt)
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

    tab.loadFileContent(kRen);
    QApplication::processEvents();
    QTextCursor c(editor->document());
    c.setPosition(kRen.indexOf("loop:") + 1);
    editor->setTextCursor(c);
    QApplication::processEvents();
    ASSERT_TRUE(editor->openRenameAtCaret());
    ASSERT_TRUE(editor->renameBar()->isVisible());

    tab.loadFileContent("other:\n    nop\n");
    QApplication::processEvents();
    EXPECT_FALSE(editor->renameBar()->isVisible())
        << "the overlay still points at the document that was closed";
}

TEST(LabelRename, AfterARenameTheCompletionOffersTheNewName)
{
    Nav nav;
    nav.load(kRen);
    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    typeInto(bar->input(), QStringLiteral("spin"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();

    QStringList labels;
    for (const Completion::Item &item : nav.editor.completionItemsForTest(QString()))
        if (item.kind == Completion::Kind::Label)
            labels << item.label;
    EXPECT_TRUE(labels.contains(QStringLiteral("spin")))
        << "the label cache still does not know the new name: "
        << labels.join(',').toStdString();
    EXPECT_FALSE(labels.contains(QStringLiteral("loop")))
        << "the label cache still offers the old name: "
        << labels.join(',').toStdString();
    // The neighbours are still there.
    EXPECT_TRUE(labels.contains(QStringLiteral("loop2")));
    EXPECT_TRUE(labels.contains(QStringLiteral("loop_end")));
}

TEST(LabelRename, AfterARenameTheHoverUnderlineFollows)
{
    Nav nav;
    nav.load(kRen);
    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    typeInto(bar->input(), QStringLiteral("spin"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();

    const QString after = nav.editor.toPlainText();
    QTextCursor at(nav.editor.document());
    at.setPosition(after.indexOf(QStringLiteral("j spin")) + 3);
    nav.editor.setTextCursor(at);
    QApplication::processEvents();

    const QPoint p = nav.editor.cursorRect().center();
    QMouseEvent move(QEvent::MouseMove, p, nav.editor.viewport()->mapToGlobal(p),
                     Qt::NoButton, Qt::NoButton, Qt::ControlModifier);
    QApplication::sendEvent(nav.editor.viewport(), &move);
    QApplication::processEvents();
    EXPECT_EQ(nav.editor.linkedName(), QStringLiteral("spin"))
        << "the Ctrl-hover did not follow the rename";
}

TEST(LabelRename, AltLeftAfterARenameIsInRange)
{
    Nav nav;
    nav.load(kRen);
    // Jump first, so there is something on the back stack.
    nav.caretTo(kRen.indexOf("j loop\n") + 3);
    key(&nav.editor, Qt::Key_F12);
    ASSERT_EQ(nav.editor.backStackDepth(), 1);

    RenameBar *bar = openRename(nav, kRen.indexOf("loop:") + 1);
    ASSERT_TRUE(bar->isVisible());
    // A SHORTER name, so every offset after it moves back.
    typeInto(bar->input(), QStringLiteral("a"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();

    key(&nav.editor, Qt::Key_Left, Qt::AltModifier);
    QApplication::processEvents();
    const int caret = nav.editor.textCursor().position();
    EXPECT_GE(caret, 0);
    EXPECT_LE(caret, nav.editor.document()->characterCount() - 1)
        << "Alt+Left landed outside the document after a rename";
}

TEST(LabelRename, TwoLabelsOnOneLineAndALabelBeforeAnInstruction)
{
    Nav nav;
    // Both shapes the assembler accepts and that a naive line rewrite breaks.
    nav.load("a: b:\n    j a\n    j b\nc: addi x1, x0, 1\n    j c\n");
    RenameBar *bar = openRename(nav, 0);
    ASSERT_TRUE(bar->isVisible());
    typeInto(bar->input(), QStringLiteral("first"));
    key(bar->input(), Qt::Key_Return);
    QApplication::processEvents();

    const QString after = nav.editor.toPlainText();
    EXPECT_TRUE(after.startsWith(QStringLiteral("first: b:\n"))) << after.toStdString();
    EXPECT_TRUE(after.contains(QStringLiteral("    j first\n"))) << after.toStdString();
    EXPECT_TRUE(after.contains(QStringLiteral("    j b\n"))) << "b was damaged";
    EXPECT_TRUE(after.contains(QStringLiteral("c: addi x1, x0, 1\n"))) << "c was damaged";
}
