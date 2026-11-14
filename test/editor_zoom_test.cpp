/**
 * @file editor_zoom_test.cpp
 * @brief Auto-closing pairs through real keys, and session zoom.
 *
 * The pair rules have their own table in editor_pairs_test.cpp. What is
 * here is the wiring, plus the things only a widget can answer: that one
 * Ctrl+Z undoes a whole typed pair, what Ctrl+Z does after a type-over, and
 * that zoom moves the font and the gutters together without touching the
 * document.
 */
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QKeyEvent>
#include <QTextDocument>

using namespace Kites;

namespace
{
void sendKey(QWidget *target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString &text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

/// Types one character, the way a keyboard would: the text is what decides.
void type(QWidget *target, QChar c)
{
    sendKey(target, Qt::Key(c.unicode()), Qt::NoModifier, QString(c));
}

struct Bench
{
    Editor editor;
    Bench()
    {
        editor.resize(700, 400);
        editor.show();
        QApplication::processEvents();
    }
    void place(const QString &text, int anchor, int position)
    {
        editor.setPlainText(text);
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        editor.setTextCursor(cursor);
        QApplication::processEvents();
        for (QWidget *top : QApplication::topLevelWidgets())
            if (top->isVisible() && qobject_cast<QAbstractItemView *>(top))
                top->hide();
        QApplication::processEvents();
    }
    QString text() const { return editor.toPlainText(); }
    int caret() const { return editor.textCursor().position(); }
};
} // namespace

TEST(EditorPairsWiring, TypingReachesTheRulesAndBackspaceRemovesBoth)
{
    Bench bench;

    bench.place("", 0, 0);
    type(&bench.editor, '(');
    EXPECT_EQ(bench.text(), QString("()"));
    EXPECT_EQ(bench.caret(), 1);

    // Typing the closer steps over it rather than adding a second.
    type(&bench.editor, ')');
    EXPECT_EQ(bench.text(), QString("()"));
    EXPECT_EQ(bench.caret(), 2);

    // Backspace between an empty pair takes both.
    bench.place("a()b", 2, 2);
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_EQ(bench.text(), QString("ab"));
    EXPECT_EQ(bench.caret(), 1);

    // And an ordinary Backspace is still ordinary.
    bench.place("abc", 2, 2);
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_EQ(bench.text(), QString("ac"));

    // A quote after a word character does not pair.
    bench.place("don", 3, 3);
    type(&bench.editor, '\'');
    EXPECT_EQ(bench.text(), QString("don'"));
}

TEST(EditorPairsWiring, AnOpenerWrapsASelectionAndKeepsIt)
{
    Bench bench;
    bench.place("alpha beta", 0, 5);
    type(&bench.editor, '(');
    EXPECT_EQ(bench.text(), QString("(alpha) beta"));
    EXPECT_EQ(bench.editor.textCursor().selectedText(), QString("alpha"))
        << "the selection was lost";
}

TEST(EditorPairsUndo, OneCtrlZUndoesAWholeTypedPairAndWhatATypeOverDoes)
{
    Bench bench;

    bench.place("", 0, 0);
    type(&bench.editor, '(');
    ASSERT_EQ(bench.text(), QString("()"));
    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.text(), QString(""))
        << "one Ctrl+Z did not undo the whole pair";

    // After a TYPE-OVER. The type-over inserts nothing - it only moves the
    // caret - so there is nothing of its own on the undo stack, and one
    // Ctrl+Z goes back past it to the pair that was typed before. Pinned
    // because it is the one place a user might expect two steps.
    bench.place("", 0, 0);
    type(&bench.editor, '(');
    type(&bench.editor, ')');
    ASSERT_EQ(bench.text(), QString("()"));
    ASSERT_EQ(bench.caret(), 2);
    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.text(), QString(""))
        << "Ctrl+Z after a type-over should undo the pair, in one step";

    // Backspace over a pair is also one step.
    bench.place("a()b", 2, 2);
    sendKey(&bench.editor, Qt::Key_Backspace);
    ASSERT_EQ(bench.text(), QString("ab"));
    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.text(), QString("a()b"));
}

TEST(EditorPairsReadOnly, AReadOnlyPaneTypesNothingAtAll)
{
    Bench bench;
    bench.place("abc", 3, 3);
    bench.editor.setReadOnly(true);
    for (QChar c : QString("(\"'"))
        type(&bench.editor, c);
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_EQ(bench.text(), QString("abc")) << "a read-only pane was changed";
}

// ═══════════════════════════════ zoom ═════════════════════════════════════

namespace
{
int fontSizeOf(const Editor &editor)
{
    const QFont font = editor.font();
    return font.pixelSize() > 0 ? font.pixelSize() : font.pointSize();
}
} // namespace

TEST(EditorZoom, EachKeyMovesTheFontByOneStepAndCtrlZeroReturns)
{
    Bench bench;
    bench.place("one\ntwo\nthree", 0, 0);
    const int base = fontSizeOf(bench.editor);
    ASSERT_GT(base, 0) << "the editor font has no size to zoom";

    sendKey(&bench.editor, Qt::Key_Plus, Qt::ControlModifier, "+");
    EXPECT_EQ(fontSizeOf(bench.editor), base + 1) << "Ctrl++";
    sendKey(&bench.editor, Qt::Key_Equal, Qt::ControlModifier, "=");
    EXPECT_EQ(fontSizeOf(bench.editor), base + 2) << "Ctrl+=";
    sendKey(&bench.editor, Qt::Key_Minus, Qt::ControlModifier, "-");
    EXPECT_EQ(fontSizeOf(bench.editor), base + 1) << "Ctrl+-";

    sendKey(&bench.editor, Qt::Key_0, Qt::ControlModifier, "0");
    EXPECT_EQ(fontSizeOf(bench.editor), base) << "Ctrl+0 did not return to the start";
    EXPECT_EQ(bench.editor.zoomDelta(), 0);
}

TEST(EditorZoom, ItClampsAtBothEnds)
{
    Bench bench;
    bench.place("one", 0, 0);
    const int base = fontSizeOf(bench.editor);

    for (int i = 0; i < 40; ++i)
        sendKey(&bench.editor, Qt::Key_Plus, Qt::ControlModifier, "+");
    EXPECT_EQ(bench.editor.zoomDelta(), Editor::maxZoomDelta) << "zoom in did not clamp";
    EXPECT_EQ(fontSizeOf(bench.editor), base + Editor::maxZoomDelta);

    for (int i = 0; i < 80; ++i)
        sendKey(&bench.editor, Qt::Key_Minus, Qt::ControlModifier, "-");
    EXPECT_EQ(bench.editor.zoomDelta(), Editor::minZoomDelta) << "zoom out did not clamp";
    EXPECT_EQ(fontSizeOf(bench.editor), base + Editor::minZoomDelta);
    EXPECT_GT(fontSizeOf(bench.editor), 0) << "the font went to nothing";
}

TEST(EditorZoom, TheGutterFollowsTheFont)
{
    Bench bench;
    bench.place("one\ntwo\nthree", 0, 0);
    // The baseline is taken at delta 0 rather than before any zoom at all.
    // Until the first applyZoomedFont() the gutter columns use whatever font
    // they INHERITED, which in kites_test is not the editor's - there are no
    // font resources here, so FontManager falls back and the two differ by
    // two pixels of gutter. In the application they are the same font and
    // this makes no difference; measuring both ends the same way is what
    // keeps the case about zoom.
    sendKey(&bench.editor, Qt::Key_0, Qt::ControlModifier);
    const int narrow = bench.editor.viewport()->x();

    for (int i = 0; i < 10; ++i)
        sendKey(&bench.editor, Qt::Key_Plus, Qt::ControlModifier, "+");
    const int wide = bench.editor.viewport()->x();
    EXPECT_GT(wide, narrow) << "the gutter did not grow with the font";

    sendKey(&bench.editor, Qt::Key_0, Qt::ControlModifier, "0");
    EXPECT_EQ(bench.editor.viewport()->x(), narrow)
        << "the gutter did not come back with Ctrl+0";
}

TEST(EditorZoom, NeitherTheDocumentNorTheBreakpointsMove)
{
    Bench bench;
    const QString original = "one\ntwo\nthree\nfour";
    bench.place(original, 0, 0);
    bench.editor.setBreakpoints({});
    for (int line : {2, 4})
        bench.editor.toggleBreakpointAtLine(line);

    const int revision = int(bench.editor.document()->revision());
    const bool undoWas = bench.editor.document()->isUndoAvailable();

    for (int i = 0; i < 5; ++i)
        sendKey(&bench.editor, Qt::Key_Plus, Qt::ControlModifier, "+");
    for (int i = 0; i < 9; ++i)
        sendKey(&bench.editor, Qt::Key_Minus, Qt::ControlModifier, "-");
    sendKey(&bench.editor, Qt::Key_0, Qt::ControlModifier, "0");

    EXPECT_EQ(bench.text(), original) << "zoom changed the document";
    EXPECT_EQ(int(bench.editor.document()->revision()), revision)
        << "zoom bumped the document revision";
    EXPECT_EQ(bench.editor.document()->isUndoAvailable(), undoWas);

    QList<int> lines;
    for (uint64_t line : bench.editor.getBreakpoints())
        lines << int(line);
    EXPECT_EQ(lines, (QList<int>{2, 4})) << "zoom moved a breakpoint";
}

TEST(EditorZoom, AReadOnlyPaneZoomsToo)
{
    // Zoom changes nothing in the document, so a read-only pane has no
    // reason to refuse it - and the Expanded and Output panes are read-only.
    Bench bench;
    bench.place("one\ntwo", 0, 0);
    bench.editor.setReadOnly(true);
    const int base = fontSizeOf(bench.editor);

    sendKey(&bench.editor, Qt::Key_Plus, Qt::ControlModifier, "+");
    EXPECT_EQ(fontSizeOf(bench.editor), base + 1);
    EXPECT_EQ(bench.text(), QString("one\ntwo"));
}
