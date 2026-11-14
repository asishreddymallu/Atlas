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
#include "ui/theme/font_manager.h"

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

// ════════════════════ a font change, with a zoom standing ════════════════

/**
 * @brief Changing the font in Settings must not drop the session zoom.
 *
 * FontManager emits fontChangedSignal; KitesEditor re-clamps the standing
 * delta and re-applies it on top of the NEW size, then re-measures the
 * gutter. Three things can go wrong and all three are asserted: the delta
 * silently resetting to 0, the new size being rendered without the delta,
 * and the gutter keeping its old width because nothing re-measured it.
 */
TEST(EditorZoomFontChange, ASettingsFontChangeKeepsTheDeltaAndRemeasures)
{
    FontManager &fonts = FontManager::getInstance();
    const int    original = fonts.currentFont().pointSize();

    Bench bench;
    bench.place("one\ntwo\nthree", 0, 0);
    sendKey(&bench.editor, Qt::Key_0, Qt::ControlModifier, "0");

    for (int i = 0; i < 3; ++i)
        sendKey(&bench.editor, Qt::Key_Plus, Qt::ControlModifier, "+");
    ASSERT_EQ(bench.editor.zoomDelta(), 3);
    const int zoomedAtOriginal = bench.editor.viewport()->x();

    // A LARGER font in Settings. The delta is well inside the clamp at both
    // sizes, so it must survive untouched.
    fonts.setSize(original + 4);
    QApplication::processEvents();
    EXPECT_EQ(bench.editor.zoomDelta(), 3) << "the session zoom was lost on a font change";
    const int zoomedAtLarger = bench.editor.viewport()->x();
    EXPECT_GT(zoomedAtLarger, zoomedAtOriginal)
        << "the gutter did not re-measure for the larger font";

    // The rendered size is the new configured size PLUS the delta, not one
    // or the other. Reading it back through font() is what the application
    // actually renders with.
    const QFont rendered = bench.editor.font();
    const int   renderedSize =
        rendered.pixelSize() > 0 ? rendered.pixelSize() : rendered.pointSize();
    EXPECT_EQ(renderedSize, original + 4 + 3)
        << "the delta was not re-applied on top of the new size";

    // And a SMALL font, where the standing delta would fall through the
    // floor. It must be re-clamped, not rendered.
    sendKey(&bench.editor, Qt::Key_0, Qt::ControlModifier, "0");
    for (int i = 0; i < 6; ++i)
        sendKey(&bench.editor, Qt::Key_Minus, Qt::ControlModifier, "-");
    fonts.setSize(6);
    QApplication::processEvents();
    const QFont small = bench.editor.font();
    const int   smallSize = small.pixelSize() > 0 ? small.pixelSize() : small.pointSize();
    EXPECT_GE(smallSize, KitesEditor::minRenderedSize)
        << "a small Settings font let the zoom render below the floor";

    fonts.setSize(original);
    QApplication::processEvents();
}

/**
 * @brief The breakpoint dot must never be taller than the line it marks.
 *
 * Pixels, not arithmetic: the dot's size is decided inside paintEvent, so
 * the only honest check is to paint the gutter and measure the ink.
 *
 * The configured sizes matter more than the zoom here. The clamp keeps
 * zoom from reaching a line too short for a 9 px dot, but a small font
 * chosen in Settings reaches it with no zoom at all - which is how the
 * fixed-size dot got out of its line in the first place.
 *
 * KNOWN LIMIT, measured rather than assumed: this test does not catch a
 * dot put back to a fixed 9 px. kites_test has no font resources, so
 * FontManager falls back to a family whose line height never drops below
 * 9 px at any size this asks for, and the overflow cannot be reached
 * here. The out-of-tree harness does reach it - at a configured 4 pt the
 * line is 7 px and a fixed dot reports NO - and that is where the control
 * for the scaling lives. What this test is worth is the invariant: if the
 * metrics ever do shrink in kites_test, it fails rather than drifting.
 */
TEST(EditorZoomDot, TheDotStaysInsideItsLineAtEverySize)
{
    FontManager &fonts = FontManager::getInstance();
    const int    original = fonts.currentFont().pointSize();

    Bench bench;
    bench.place("one\ntwo\nthree\nfour\nfive", 0, 0);
    bench.editor.toggleBreakpointAtLine(3);

    QWidget *dots = nullptr;
    for (QWidget *child : bench.editor.findChildren<QWidget *>())
        if (QString::fromLatin1(child->metaObject()->className()).contains("BreakPoint"))
            dots = child;
    ASSERT_NE(dots, nullptr);

    for (int size : {6, 8, 11, 16})
    {
        fonts.setSize(size);
        QApplication::processEvents();
        for (int delta : {-6, 0, 6})
        {
            bench.editor.setZoomDelta(delta);
            QApplication::processEvents();

            QTextCursor third(bench.editor.document());
            third.setPosition(bench.editor.document()->findBlockByNumber(2).position());
            const QRect band = bench.editor.cursorRect(third);

            const QImage shot = dots->grab().toImage();
            ASSERT_FALSE(shot.isNull());
            const QRgb ground = shot.pixel(0, 0);
            int top = -1, bottom = -1;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x)
                    if (shot.pixel(x, y) != ground)
                    {
                        if (top < 0) top = y;
                        bottom = y;
                        break;
                    }
            ASSERT_GE(top, 0) << "no dot was painted at size " << size << " delta " << delta;
            // Containment, not height. A dot drawn above its line is
            // CLIPPED by the gutter widget, so its ink measures shorter
            // than it is and a height comparison calls the overflow a fit.
            // Where the ink sits is what shows the overflow.
            EXPECT_GE(top, band.top())
                << "the dot starts above its line at configured " << size
                << " pt, delta " << delta << " (line " << band.top() << ".."
                << band.bottom() << ", dot " << top << ".." << bottom << ")";
            EXPECT_LE(bottom, band.bottom())
                << "the dot ends below its line at configured " << size
                << " pt, delta " << delta << " (line " << band.top() << ".."
                << band.bottom() << ", dot " << top << ".." << bottom << ")";
        }
    }
    bench.editor.setZoomDelta(0);
    fonts.setSize(original);
    QApplication::processEvents();
}
