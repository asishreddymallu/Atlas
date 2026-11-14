/**
 * @file completion_popup_test.cpp
 * @brief The suggestion list through real keys, with an injected source.
 *
 * Every case here is a GUARD unless its comment says otherwise. The source is
 * a fake with a QUERY COUNTER: "the popup is not visible" would also hold if
 * the editor queried and happened to find nothing, which is a different and
 * slower bug, so the navigation cases assert the counter did not move.
 */
#include "ui/editor_tab/completion_popup.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/find_bar.h"
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

#include "ui/theme/font_manager.h"
#include "ui/theme/theme_manager.h"
#include <QMouseEvent>
#include <cmath>
#include <cstdio>
#include <QApplication>
#include <QKeyEvent>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextDocument>

using namespace Kites;

namespace
{
/// A fixed symbol list, so what the popup shows never depends on resources.
const QStringList kSymbols = {"add", "addi", "addiw", "addw", "and", "andi",
                              "sub", "subw", "sw",    "sd",   "x1",  "x10",
                              "x11", "loop", "loop2", "loop_end", ".word"};

struct Bench
{
    Editor editor;
    int    queries{0};
    QString lastPrefix;

    Bench()
    {
        editor.resize(800, 400);
        editor.show();
        editor.setCompletionSource(
            [this](const QString &prefix)
            {
                ++queries;
                lastPrefix = prefix;
                QList<Completion::Item> items;
                for (const QString &s : kSymbols)
                    items << Completion::Item{s, Completion::kindOf(s), {}};
                return items;
            });
        QApplication::processEvents();
    }

    void place(const QString &text, int offset)
    {
        editor.setPlainText(text);
        QTextCursor c = editor.textCursor();
        c.setPosition(offset);
        editor.setTextCursor(c);
        QApplication::processEvents();
        queries = 0; // the setup itself must not count against a case
    }
    bool open() const { return editor.isCompletionVisible(); }
    QString text() const { return editor.toPlainText(); }
    int caret() const { return editor.textCursor().position(); }
};

void sendKey(QWidget *w, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString &text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(w, &release);
    QApplication::processEvents();
}
void type(QWidget *w, const QString &s)
{
    for (QChar c : s)
        sendKey(w, Qt::Key(c.toUpper().unicode()), Qt::NoModifier, QString(c));
}
} // namespace

// ═══════════════════════════ T2: the whole table ═════════════════════════

/**
 * @brief The recorded bug, and every other way the caret can move.
 *
 * Two lines of real assembly; the caret arrives on an existing word by every
 * navigation key there is. None of them may open the list, and none may even
 * ASK the source.
 */
TEST(CompletionTrigger, NavigationNeverOpensIt)
{
    Bench bench;
    const QString program = "addi x0,x0,9\nsub x0,x0,x0\n";

    struct Case { const char *name; Qt::Key key; Qt::KeyboardModifiers mods; };
    const QList<Case> navigation = {
        {"Up",          Qt::Key_Up,       Qt::NoModifier},
        {"Down",        Qt::Key_Down,     Qt::NoModifier},
        {"Left",        Qt::Key_Left,     Qt::NoModifier},
        {"Right",       Qt::Key_Right,    Qt::NoModifier},
        {"Home",        Qt::Key_Home,     Qt::NoModifier},
        {"End",         Qt::Key_End,      Qt::NoModifier},
        {"PageUp",      Qt::Key_PageUp,   Qt::NoModifier},
        {"PageDown",    Qt::Key_PageDown, Qt::NoModifier},
        {"Ctrl+Left",   Qt::Key_Left,     Qt::ControlModifier},
        {"Ctrl+Right",  Qt::Key_Right,    Qt::ControlModifier},
        {"Ctrl+Home",   Qt::Key_Home,     Qt::ControlModifier},
        {"Ctrl+End",    Qt::Key_End,      Qt::ControlModifier},
    };

    for (const Case &c : navigation)
    {
        // Park on the empty third line, exactly as in the recording.
        bench.place(program, 26);
        sendKey(&bench.editor, c.key, c.mods);
        EXPECT_FALSE(bench.open()) << c.name << " opened the suggestion list";
        EXPECT_EQ(bench.queries, 0) << c.name << " queried the completion source";
    }

    // The other half of T2: things that are not keys at all.
    bench.place(program, 26);
    QTextCursor onWord(bench.editor.document());
    onWord.setPosition(13); // the start of "sub"
    bench.editor.setTextCursor(onWord);
    QApplication::processEvents();
    EXPECT_FALSE(bench.open()) << "setTextCursor opened it";
    EXPECT_EQ(bench.queries, 0) << "setTextCursor queried the source";

    bench.editor.setPlainText(program);
    QApplication::processEvents();
    EXPECT_FALSE(bench.open()) << "setPlainText opened it";

    bench.place(program, 13);
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_FALSE(bench.open()) << "Backspace opened it";
    EXPECT_EQ(bench.queries, 0) << "Backspace queried the source while closed";

    bench.place(program, 13);
    sendKey(&bench.editor, Qt::Key_Delete);
    EXPECT_FALSE(bench.open()) << "Delete opened it";

    bench.place(program, 13);
    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_FALSE(bench.open()) << "undo opened it";

    // A Tier 1 edit command.
    bench.place(program, 13);
    sendKey(&bench.editor, Qt::Key_Down, Qt::AltModifier); // move line down
    EXPECT_FALSE(bench.open()) << "a line command opened it";
}

TEST(CompletionTrigger, TypingOpensIt)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    EXPECT_TRUE(bench.open()) << "typing a word did not open the list";
    EXPECT_EQ(bench.lastPrefix, QString("ad"));
    EXPECT_GT(bench.queries, 0);
}

TEST(CompletionTrigger, TypingInTheMiddleFiltersByTheTextBeforeTheCaret)
{
    // THE BUG, as a rule: the prefix is what is typed, not the whole word.
    Bench bench;
    bench.place("subw", 0);
    // Caret at the start of "subw"; type an 'a' there.
    type(&bench.editor, "a");
    EXPECT_TRUE(bench.open());
    EXPECT_EQ(bench.lastPrefix, QString("a"))
        << "the tail of the word leaked into the prefix";
    EXPECT_EQ(bench.text(), QString("asubw"));
}

TEST(CompletionTrigger, CommentsAndStringsNeverOpenIt)
{
    Bench bench;
    bench.place("# ", 2);
    type(&bench.editor, "ad");
    EXPECT_FALSE(bench.open()) << "it opened inside a comment";

    bench.place("  .asciz \"", 10);
    type(&bench.editor, "ad");
    EXPECT_FALSE(bench.open()) << "it opened inside a string";
}

TEST(CompletionTrigger, CtrlSpaceOpensWithAnEmptyPrefix)
{
    Bench bench;
    bench.place("", 0);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    EXPECT_TRUE(bench.open()) << "Ctrl+Space did not open it";
    EXPECT_EQ(bench.lastPrefix, QString("")) << "the empty prefix was not used";
    EXPECT_EQ(bench.editor.completionPopup()->count(), kSymbols.size())
        << "an empty prefix must offer everything";
}

// ════════════════════════════ keys while open ════════════════════════════

TEST(CompletionKeys, UpDownWrap)
{
    Bench bench;
    bench.place("", 0);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    ASSERT_TRUE(bench.open());
    CompletionPopup *popup = bench.editor.completionPopup();
    const int n = popup->count();
    ASSERT_GT(n, 1);

    EXPECT_EQ(popup->currentRow(), 0) << "the first item is not selected on open";
    sendKey(&bench.editor, Qt::Key_Up);
    EXPECT_EQ(popup->currentRow(), n - 1) << "Up on the first row did not wrap";
    sendKey(&bench.editor, Qt::Key_Down);
    EXPECT_EQ(popup->currentRow(), 0) << "Down on the last row did not wrap";
    // and the caret did not move
    EXPECT_EQ(bench.caret(), 0);
}

TEST(CompletionKeys, EnterAndTabAccept)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "addi");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_Return);
    EXPECT_EQ(bench.text(), QString("addi"));
    EXPECT_FALSE(bench.open()) << "accepting left it open";

    bench.place("", 0);
    type(&bench.editor, "sub");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_Tab);
    EXPECT_EQ(bench.text(), QString("sub"));
    EXPECT_FALSE(bench.open());
}

TEST(CompletionKeys, ShiftEnterReplacesTheTail)
{
    Bench bench;
    // "subw" with the caret after "su": insert mode keeps "bw", replace drops it.
    bench.place("subw", 2);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    ASSERT_TRUE(bench.open());
    bench.editor.completionPopup()->setCurrentRow(0);
    const QString chosen = bench.editor.completionPopup()->selectedLabel();
    sendKey(&bench.editor, Qt::Key_Return, Qt::ShiftModifier);
    EXPECT_EQ(bench.text(), chosen) << "replace mode left the tail behind";
}

TEST(CompletionKeys, AcceptIsOneUndoStepAndRestoresThePrefix)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_Return);
    const QString accepted = bench.text();
    EXPECT_NE(accepted, QString("ad"));
    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.text(), QString("ad")) << "one Ctrl+Z did not put the prefix back";
}

TEST(CompletionKeys, NonWordCharactersCloseWithoutAccepting)
{
    Bench bench;
    for (const QString &ch : {QStringLiteral(" "), QStringLiteral(","),
                              QStringLiteral("("), QStringLiteral("#")})
    {
        bench.place("", 0);
        type(&bench.editor, "ad");
        ASSERT_TRUE(bench.open()) << "setup failed for " << ch.toStdString();
        type(&bench.editor, ch);
        EXPECT_FALSE(bench.open()) << ch.toStdString() << " left it open";
        EXPECT_TRUE(bench.text().startsWith("ad" + ch))
            << ch.toStdString() << " accepted a completion instead of closing: "
            << bench.text().toStdString();
    }
}

TEST(CompletionKeys, BackspaceToTheWordStartCloses)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    const int queriesBefore = bench.queries;
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_TRUE(bench.open()) << "Backspace with a prefix left should re-filter, not close";
    // It must actually RE-FILTER, not merely stay up with the old list. The
    // item COUNT is no use here - every symbol matching "ad" also matches
    // "a", so it is 6 either way - but the source must have been asked
    // again, with the shorter prefix.
    EXPECT_GT(bench.queries, queriesBefore)
        << "Backspace did not re-query; the previous filter's items are still up";
    EXPECT_EQ(bench.lastPrefix, QString("a"));
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_FALSE(bench.open()) << "Backspace to the word start did not close it";
}

TEST(CompletionKeys, LeavingTheWordCloses)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_Left);
    EXPECT_TRUE(bench.open()) << "Left inside the word closed it";
    sendKey(&bench.editor, Qt::Key_Left);
    EXPECT_FALSE(bench.open()) << "Left past the word start left it open";
}

TEST(CompletionKeys, EscClosesWithoutMovingTheCaretOrClosingTheFindBar)
{
    Bench bench;
    // "ad" so that typing 'd' gives the prefix "add", which matches. A
    // prefix that matched nothing would close the list for the right reason
    // and the case would prove nothing about Esc.
    bench.place("ad", 2);
    bench.editor.openFind(false);
    QApplication::processEvents();
    ASSERT_NE(bench.editor.findBar(), nullptr);
    ASSERT_FALSE(bench.editor.findBar()->isHidden());

    type(&bench.editor, "d");
    ASSERT_TRUE(bench.open());
    const int before = bench.caret();

    sendKey(&bench.editor, Qt::Key_Escape);
    EXPECT_FALSE(bench.open()) << "Esc did not close the suggestion list";
    EXPECT_EQ(bench.caret(), before) << "Esc moved the caret";
    EXPECT_FALSE(bench.editor.findBar()->isHidden())
        << "Esc closed the find bar as well; the popup must take it first";

    // A second Esc now reaches the find bar.
    sendKey(&bench.editor, Qt::Key_Escape);
    EXPECT_TRUE(bench.editor.findBar()->isHidden()) << "the second Esc did not reach the bar";
}

TEST(CompletionKeys, OpeningFindClosesIt)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    EXPECT_FALSE(bench.open()) << "opening Find left the suggestion list up";

    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
    EXPECT_FALSE(bench.open()) << "opening Go to line left the suggestion list up";
}

TEST(CompletionKeys, UndoClosesIt)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_FALSE(bench.open()) << "undo left it open";
}

TEST(CompletionKeys, FocusLossCloses)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason);
    QApplication::sendEvent(&bench.editor, &out);
    QApplication::processEvents();
    EXPECT_FALSE(bench.open()) << "focus loss left it open";
}

TEST(CompletionKeys, NoMatchesCloseIt)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    type(&bench.editor, "zzz");
    EXPECT_FALSE(bench.open()) << "a filter with no matches left it open";
}

// ═══════════════════════ geometry: L1, L2, L4 ════════════════════════════

namespace
{
/// Opens the list with everything in it and returns the popup.
CompletionPopup *openFull(Bench &bench, int editorWidth = 800)
{
    bench.editor.resize(editorWidth, 400);
    bench.place("", 0);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    QApplication::processEvents();
    return bench.editor.completionPopup();
}
} // namespace

TEST(CompletionGeometry, ItStaysInsideTheViewportAndFlipsWhenThereIsNoRoom)
{
    Bench bench;
    CompletionPopup *popup = openFull(bench);
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(popup->isVisible());

    const QSize viewport = bench.editor.viewport()->size();
    const QRect box = popup->geometry();
    EXPECT_GE(box.left(), 0);
    EXPECT_GE(box.top(), 0);
    EXPECT_LE(box.right(), viewport.width()) << "the list hangs off the right";
    EXPECT_LE(box.bottom(), viewport.height()) << "the list hangs off the bottom";
    EXPECT_FALSE(popup->isAbove()) << "there was room below; it should not have flipped";

    // Put the caret on the last visible line, where there is no room below.
    QStringList many;
    for (int i = 0; i < 60; ++i)
        many << QString(); // blank, so Ctrl+Space sees an empty prefix
    bench.editor.setPlainText(many.join('\n'));
    QTextCursor last(bench.editor.document());
    last.movePosition(QTextCursor::End);
    bench.editor.setTextCursor(last);
    bench.editor.ensureCursorVisible();
    QApplication::processEvents();
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    QApplication::processEvents();
    ASSERT_TRUE(popup->isVisible());
    EXPECT_TRUE(popup->isAbove()) << "it did not flip above when the caret was at the bottom";
    EXPECT_GE(popup->geometry().top(), 0);
    EXPECT_LE(popup->geometry().bottom(), bench.editor.viewport()->height());
}

TEST(CompletionGeometry, ItNeverShowsMoreThanTwelveRows)
{
    Bench bench;
    CompletionPopup *popup = openFull(bench);
    ASSERT_GT(popup->count(), CompletionPopup::maxVisibleRows)
        << "the fixture needs more items than fit for this to mean anything";
    const int rows = popup->height() / popup->rowHeight();
    EXPECT_LE(rows, CompletionPopup::maxVisibleRows);
}

TEST(CompletionGeometry, ItFitsInsideANarrowPane)
{
    // The 849 px window leaves the Input pane a few hundred pixels.
    Bench bench;
    CompletionPopup *popup = openFull(bench, 240);
    ASSERT_TRUE(popup->isVisible());
    EXPECT_LE(popup->geometry().right(), bench.editor.viewport()->width())
        << "the list is wider than the pane it is drawn in";
    EXPECT_GE(popup->geometry().left(), 0);
}

TEST(CompletionGeometry, RowsFollowTheZoom)
{
    // Pin the configured size. The zoom floor is relative to it, and
    // SettingsSandbox.AWriteWithoutARestoreStaysInTheSandbox deliberately
    // leaves 3 pt behind - at which the floor is 0 and -6 clamps to no
    // change. Found by --gtest_shuffle, which is exactly what that test's
    // comment said would happen to a case that assumed 11 silently.
    FontManager &fonts   = FontManager::getInstance();
    const int    restore = fonts.currentFont().pointSize();
    fonts.setSize(11);
    QApplication::processEvents();

    Bench bench;
    CompletionPopup *popup = openFull(bench);
    ASSERT_TRUE(popup->isVisible());

    bench.editor.setZoomDelta(0);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    const int atZero = popup->rowHeight();

    bench.editor.setZoomDelta(18);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    QApplication::processEvents();
    const int atMax = popup->rowHeight();

    bench.editor.setZoomDelta(-6);
    sendKey(&bench.editor, Qt::Key_Space, Qt::ControlModifier);
    QApplication::processEvents();
    const int atMin = popup->rowHeight();

    EXPECT_GT(atMax, atZero) << "zooming in did not grow the rows";
    EXPECT_LT(atMin, atZero) << "zooming out did not shrink the rows";
    bench.editor.setZoomDelta(0);
    fonts.setSize(restore);
    QApplication::processEvents();
}

TEST(CompletionGeometry, AClickAcceptsAndAClickElsewhereCloses)
{
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    CompletionPopup *popup = bench.editor.completionPopup();
    const QString first = popup->selectedLabel();

    // A click on the first row accepts it.
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(5, 5), QPointF(5, 5),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(popup, &press);
    QApplication::processEvents();
    EXPECT_EQ(bench.text(), first) << "clicking a row did not insert it";
    EXPECT_FALSE(bench.open());

    // A click in the editor closes it without inserting.
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    QTextCursor away(bench.editor.document());
    away.setPosition(0);
    bench.editor.setTextCursor(away);
    QApplication::processEvents();
    EXPECT_FALSE(bench.open()) << "moving the caret away left it open";
    EXPECT_EQ(bench.text(), QString("ad")) << "it inserted something on the way out";
}

TEST(CompletionGeometry, ScrollingTheCaretOutOfViewCloses)
{
    Bench bench;
    QStringList many;
    for (int i = 0; i < 200; ++i)
        many << QString();
    bench.editor.setPlainText(many.join('\n'));
    QTextCursor top(bench.editor.document());
    top.setPosition(0);
    bench.editor.setTextCursor(top);
    QApplication::processEvents();
    bench.queries = 0;

    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    bench.editor.verticalScrollBar()->setValue(
        bench.editor.verticalScrollBar()->maximum());
    QApplication::processEvents();
    EXPECT_FALSE(bench.open()) << "the caret line scrolled away and the list stayed";
}

// ══════════════════════════ L6: contrast ═════════════════════════════════

/**
 * @brief Every colour the list draws, against the surface it draws it on.
 *
 * Arithmetic here; the pixels are checked in the out-of-tree harness. Below
 * 4.5:1 is a defect, not a note.
 */
TEST(CompletionContrast, EveryColourClearsFourAndAHalfOnEveryTheme)
{
    auto luminance = [](const QColor &c)
    {
        auto channel = [](double v)
        { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) +
               0.0722 * channel(c.blueF());
    };
    auto contrast = [&](const QColor &a, const QColor &b)
    {
        const double la = luminance(a), lb = luminance(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };

    int failures = 0;
    for (const QString &theme : {"kites-dark", "kites-light", "tokyo-night",
                                 "tokyo-night-storm"})
    {
        ThemeManager::getInstance().setTheme(theme);
        // The widget's OWN colours, not a second copy of the arithmetic -
        // otherwise the test can pass while the popup draws something else.
        const CompletionPopup::Colours c = CompletionPopup::colours();
        const QColor page     = c.page;
        const QColor text     = c.text;
        const QColor detail   = c.detail;
        const QColor matched  = c.matched;
        const QColor selected = c.selected;

        struct Case { const char *what; QColor ink; QColor ground; };
        const QList<Case> cases = {
            {"label on the list",        text,    page},
            {"label on the selection",   text,    selected},
            {"matched on the list",      matched, page},
            {"matched on the selection", matched, selected},
            {"detail on the list",       detail,  page},
            {"detail on the selection",  detail,  selected},
        };
        for (const Case &c : cases)
        {
            const double ratio = contrast(c.ink, c.ground);
            std::printf("    %-20s %-26s %5.2f\n", theme.toStdString().c_str(), c.what,
                        ratio);
            EXPECT_GE(ratio, 4.5) << theme.toStdString() << ": " << c.what
                                  << " is " << ratio;
            if (ratio < 4.5)
                ++failures;
        }
    }
    EXPECT_EQ(failures, 0);
}

TEST(CompletionTrigger, ADocumentSwapClosesAnOpenList)
{
    // File > Open and loadFileContent both land as a setPlainText. A list
    // left standing would be offering the OLD document's labels against a
    // caret in a new one.
    Bench bench;
    bench.place("", 0);
    type(&bench.editor, "ad");
    ASSERT_TRUE(bench.open());
    bench.editor.setPlainText("sub x0,x0,x0\n");
    QApplication::processEvents();
    EXPECT_FALSE(bench.open()) << "a document swap left the suggestion list up";

    // NOTE for whoever mutates this next: no single line owns this. The swap
    // resets the caret, the caret-move handler sees the prefix is empty, and
    // closeCompletion() runs from there. An explicit "charsRemoved > 1 closes
    // it" check was written, measured to change nothing, and removed rather
    // than kept as a second mechanism nobody would know was redundant.
}

// ═══════════════════ the label cache, and when it goes stale ═════════════

namespace
{
/// A bench whose source offers the DOCUMENT's labels, so these cases see
/// what the real editor's default source sees. The static symbols are left
/// out: what is under test is the buffer scan, not the merge.
struct LabelBench
{
    Editor editor;
    LabelBench()
    {
        editor.resize(800, 400);
        editor.show();
        QApplication::processEvents();
    }
    /// The labels the editor would offer for `prefix`, straight from the
    /// cache the completion source reads.
    QStringList offered(const QString &prefix)
    {
        QStringList out;
        // The default source is what the editor installs; ask it the same
        // way the trigger does.
        for (const Completion::Item &item : editor.completionItemsForTest(prefix))
            if (item.kind == Completion::Kind::Label)
                out << item.label;
        out.sort();
        return out;
    }
    void setText(const QString &t)
    {
        editor.setPlainText(t);
        QApplication::processEvents();
    }
    void caretTo(int offset)
    {
        QTextCursor c(editor.document());
        c.setPosition(offset);
        editor.setTextCursor(c);
        QApplication::processEvents();
    }
};
} // namespace

/**
 * @brief (a) Deleting the ':' must retire the label.
 *
 * The cache is invalidated by looking at the edited block AFTER the change.
 * Backspacing over the colon leaves a line with no ':' in it, so the check
 * does not fire and the label survives in the suggestions - for a name that
 * is no longer defined anywhere.
 */
TEST(CompletionLabelCache, DeletingTheColonRetiresTheLabel)
{
    LabelBench bench;
    bench.setText("loop:\n    addi x1, x1, 1\n");
    EXPECT_EQ(bench.offered("lo"), QStringList({"loop"})) << "setup: loop should be offered";

    // Backspace over the ':' at offset 5.
    bench.caretTo(5);
    sendKey(&bench.editor, Qt::Key_Backspace);
    QApplication::processEvents();
    ASSERT_EQ(bench.editor.toPlainText().left(4), QString("loop"));
    ASSERT_FALSE(bench.editor.toPlainText().startsWith("loop:"));

    EXPECT_EQ(bench.offered("lo"), QStringList())
        << "the label survived the deletion of its colon";
}

/**
 * @brief (b) A replacement whose colon is on a LATER line.
 *
 * Two lines replaced by two lines, so the block count does not move, and
 * the block at `from` is the first one - which has no colon. Only the
 * second pasted line defines a label.
 */
TEST(CompletionLabelCache, AReplacementDefiningALabelOnALaterLineIsSeen)
{
    LabelBench bench;
    bench.setText("aaa\nbbb\nccc\n");
    EXPECT_EQ(bench.offered("fr"), QStringList()) << "setup: nothing defined yet";

    // Select the first two lines and replace them with two lines whose
    // SECOND line carries the colon.
    QTextCursor cursor(bench.editor.document());
    cursor.setPosition(0);
    cursor.setPosition(8, QTextCursor::KeepAnchor); // "aaa\nbbb\n"
    cursor.insertText("nop\nfresh:\n");
    QApplication::processEvents();

    EXPECT_EQ(bench.offered("fr"), QStringList({"fresh"}))
        << "a label defined on a later line of the replacement was not seen";
}

/**
 * @brief (c) Undo and redo of both.
 */
TEST(CompletionLabelCache, UndoAndRedoKeepTheLabelsHonest)
{
    LabelBench bench;
    bench.setText("loop:\n    addi x1, x1, 1\n");
    bench.caretTo(5);
    sendKey(&bench.editor, Qt::Key_Backspace);
    QApplication::processEvents();
    ASSERT_EQ(bench.offered("lo"), QStringList()) << "precondition: the colon is gone";

    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    QApplication::processEvents();
    ASSERT_TRUE(bench.editor.toPlainText().startsWith("loop:"));
    EXPECT_EQ(bench.offered("lo"), QStringList({"loop"}))
        << "undo put the colon back but the label did not return";

    sendKey(&bench.editor, Qt::Key_Y, Qt::ControlModifier);
    QApplication::processEvents();
    if (!bench.editor.toPlainText().startsWith("loop:"))
        EXPECT_EQ(bench.offered("lo"), QStringList())
            << "redo removed the colon again but the label came back";
}

/**
 * @brief (d) Deleting the whole line a label lives on.
 */
TEST(CompletionLabelCache, DeletingTheLineRetiresTheLabel)
{
    LabelBench bench;
    bench.setText("start:\nloop:\n    nop\n");
    EXPECT_EQ(bench.offered("lo"), QStringList({"loop"})) << "setup";

    // Ctrl+Shift+K deletes the line the caret is on.
    bench.caretTo(8); // inside "loop:"
    sendKey(&bench.editor, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::processEvents();
    ASSERT_FALSE(bench.editor.toPlainText().contains("loop:"));

    EXPECT_EQ(bench.offered("lo"), QStringList())
        << "the label survived its line being deleted";
}

/**
 * @brief (a) A document SWAP that keeps the line count, new label later on.
 *
 * setPlainText does not report what it added - measured, it emits one
 * contentsChange of from=0, charsRemoved=N, charsAdded=0 - so the handler
 * only ever looks at block 0. With the line count unchanged and both
 * documents' first lines unlabelled, nothing tells the cache to move.
 */
TEST(CompletionLabelCache, ASwapKeepingTheLineCountSeesANewLabel)
{
    LabelBench bench;
    bench.setText("    nop\n    nop\n    nop\n");
    EXPECT_EQ(bench.offered("fr"), QStringList()) << "setup: nothing defined in A";

    // Same number of lines; the label is on the SECOND line.
    bench.setText("    nop\nfresh:\n    nop\n");
    EXPECT_EQ(bench.offered("fr"), QStringList({"fresh"}))
        << "a label in the newly loaded document was never seen";
}

/// (b) The same, the other way: A defines it, B does not.
TEST(CompletionLabelCache, ASwapKeepingTheLineCountRetiresAGoneLabel)
{
    LabelBench bench;
    bench.setText("    nop\nstale:\n    nop\n");
    EXPECT_EQ(bench.offered("st"), QStringList({"stale"})) << "setup: A defines it";

    bench.setText("    nop\n    nop\n    nop\n");
    EXPECT_EQ(bench.offered("st"), QStringList())
        << "a label from the previous document is still being offered";
}

/// (c) Select all and paste over it - a real edit, not a document swap.
TEST(CompletionLabelCache, SelectAllAndPasteOfTheSameLineCount)
{
    LabelBench bench;
    bench.setText("    nop\n    nop\n    nop\n");
    QTextCursor all(bench.editor.document());
    all.select(QTextCursor::Document);
    all.insertText("    nop\npasted:\n    nop\n");
    QApplication::processEvents();
    EXPECT_EQ(bench.offered("pa"), QStringList({"pasted"}))
        << "a label pasted over a selection was not seen";
}

/**
 * @brief (d) The real File > Open path.
 *
 * MainWindow calls EditorTab::loadFileContent(), which clears the
 * breakpoints and then setRawText() -> Editor::setPlainText(). This drives
 * the same call the application does, with a replacement of equal line
 * count.
 */
TEST(CompletionLabelCache, TheFileOpenPathSeesTheNewDocumentsLabels)
{
    EditorTab tab;
    tab.resize(800, 400);
    tab.show();
    QApplication::processEvents();

    Editor *editor = nullptr;
    for (QPlainTextEdit *pane : tab.findChildren<QPlainTextEdit *>())
        if (!pane->isReadOnly())
            editor = static_cast<Editor *>(pane);
    ASSERT_NE(editor, nullptr);

    auto labelsFor = [&](const QString &prefix)
    {
        QStringList out;
        for (const Completion::Item &item : editor->completionItemsForTest(prefix))
            if (item.kind == Completion::Kind::Label)
                out << item.label;
        out.sort();
        return out;
    };

    tab.loadFileContent("    nop\n    nop\n    nop\n");
    QApplication::processEvents();
    ASSERT_EQ(labelsFor("op"), QStringList()) << "setup: nothing defined";

    tab.loadFileContent("    nop\nopened:\n    nop\n");
    QApplication::processEvents();
    EXPECT_EQ(labelsFor("op"), QStringList({"opened"}))
        << "File > Open loaded a document whose labels were never scanned";
}
