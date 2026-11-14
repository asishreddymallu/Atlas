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
#include "ui/editor_tab/find_bar.h"
#include "ui/editor_tab/main_editor.h"

#include <gtest/gtest.h>

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
    sendKey(&bench.editor, Qt::Key_Backspace);
    EXPECT_TRUE(bench.open()) << "Backspace with a prefix left should re-filter, not close";
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
