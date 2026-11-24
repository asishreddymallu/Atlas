/**
 * @file reorder_ui_test.cpp
 * @brief The "Suggest reordering" UI and the About dialog: what the dialog
 *        shows, that Apply is one undo step, and where the action lives.
 *
 * The reordering scheduler is by Nandan Nihal (CS25BTECH11014).
 */
#include "scheduler_test_support.h"

#include "common/version.h"
#include "scheduler_gate/reorder_gate.h"
#include "ui/dialogs/about_dialog.h"
#include "ui/dialogs/reorder_dialog.h"
#include "ui/main_window/main_window.h"
#include "ui/ribbon/ribbon_bar.h"
#include "ui/ribbon/ribbon_group.h"
#include "ui/ribbon/ribbon_page.h"

#include <QAction>
#include <QApplication>
#include <QTimer>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

#include <gtest/gtest.h>

using namespace Atlas;
using namespace Atlas::test_support::scheduler;

TEST(ReorderUi, TheDialogShowsTheMeasuredCyclesAndTheCredit)
{
    setupVmStateDirectory();
    const std::string original = readDemo("load_use_original.s");
    const ReorderSuggestion s  = suggestReordering(original, ProcessorType::RV5Stage_H_F);
    ASSERT_TRUE(s.offered());

    ReorderDialog dialog(QString::fromStdString(original), QString::fromStdString(s.suggestedSource),
                         QStringLiteral("5-stage, hazard detection, forwarding"), s.originalCycles,
                         s.suggestedCycles, true);
    EXPECT_EQ(dialog.originalTitle(), QStringLiteral("Original - 11 cycles"));
    EXPECT_EQ(dialog.suggestedTitle(), QStringLiteral("Suggested - 10 cycles"));
    EXPECT_TRUE(dialog.summaryText().contains(QStringLiteral("10 cycles instead of 11 cycles")));
    EXPECT_TRUE(dialog.summaryText().contains(QStringLiteral("1 cycle fewer")));
    EXPECT_EQ(dialog.footerText(),
              QStringLiteral("Reordering scheduler by Nandan Nihal (CS25BTECH11014)"));
    EXPECT_EQ(dialog.suggestedSource(), QString::fromStdString(s.suggestedSource));
    EXPECT_TRUE(dialog.applyButton()->isEnabled());
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("Suggest reordering"));

    // Captions take the theme's pane-title style rather than a colour of
    // their own.
    for (QLabel *label : dialog.findChildren<QLabel *>())
        EXPECT_TRUE(label->styleSheet().isEmpty()) << label->text().toStdString();
    EXPECT_EQ(dialog.findChild<QLabel *>(QStringLiteral("reorderCredit"))->property("paneTitle"),
              QVariant(true));
}

TEST(ReorderUi, ApplyIsDisabledWhileTheEditorIsReadOnly)
{
    ReorderDialog dialog(QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("p"), 2, 1, false);
    EXPECT_FALSE(dialog.applyButton()->isEnabled());
    EXPECT_FALSE(dialog.applyButton()->toolTip().isEmpty());
}

TEST(ReorderUi, ApplyingIsASingleUndoStep)
{
    QPlainTextEdit editor;
    const QString before = QString::fromStdString(readDemo("load_use_original.s"));
    editor.setPlainText(before);
    editor.document()->clearUndoRedoStacks();
    editor.moveCursor(QTextCursor::End);
    editor.insertPlainText(QStringLiteral("# typed before\n")); // an earlier, separate edit
    const QString typed = editor.toPlainText();

    replaceDocumentAsOneEdit(editor, QStringLiteral("addi x7, x0, 1\n"));
    EXPECT_EQ(editor.toPlainText(), QStringLiteral("addi x7, x0, 1\n"));

    editor.undo(); // exactly one undo restores everything the apply replaced
    EXPECT_EQ(editor.toPlainText(), typed);
    editor.undo(); // and the next one is the earlier edit, not half of the apply
    EXPECT_EQ(editor.toPlainText(), before);
}

TEST(ReorderUi, TheAboutDialogNamesTheCreditAndTheLicenceFiles)
{
    EXPECT_EQ(AboutDialog::lines(),
              (QStringList{QStringLiteral("Atlas: a RISC-V assembly simulator"),
                           QStringLiteral("Version ") + QString::fromStdString(atlasVersion()),
                           QStringLiteral("Reordering scheduler by Nandan Nihal (CS25BTECH11014)"),
                           QStringLiteral("Licence and origin: see NOTICE.md and LICENSE.md")}));
    AboutDialog dialog;
    EXPECT_EQ(dialog.windowTitle(), QStringLiteral("About Atlas"));
    QStringList shown;
    for (QLabel *label : dialog.findChildren<QLabel *>())
        shown << label->text();
    EXPECT_EQ(shown, AboutDialog::lines());
}

TEST(ReorderUi, TheActionIsOnTheEditorRibbonPageAndInTheFileMenu)
{
    MainWindow window;
    QAction   *action = window.findChild<QAction *>(QStringLiteral("suggestReorderAction"));
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->text(), QStringLiteral("Suggest reordering"));
    EXPECT_TRUE(action->shortcut().isEmpty());

    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    RibbonGroup *schedule = nullptr;
    for (RibbonGroup *group : bar->page(MainWindow::EditorTabIndex)->groups())
        if (group->caption() == QStringLiteral("Schedule"))
            schedule = group;
    ASSERT_NE(schedule, nullptr);
    ASSERT_EQ(schedule->buttons().size(), 1);
    EXPECT_EQ(schedule->buttons().first()->defaultAction(), action);

    QMenu *file = window.menuBar()->actions().first()->menu();
    ASSERT_NE(file, nullptr);
    EXPECT_TRUE(file->actions().contains(action));
}

TEST(ReorderUi, TheAboutActionOpensSomethingNow)
{
    // The Help menu's About used to be connected to nothing.
    MainWindow window;
    QAction   *about = nullptr;
    for (QAction *menu : window.menuBar()->actions())
        if (menu->text() == QStringLiteral("&Help"))
            about = menu->menu()->actions().value(0);
    ASSERT_NE(about, nullptr);
    EXPECT_EQ(about->text(), QStringLiteral("About"));

    // Trigger it, and close whatever modal dialog it opens from inside that
    // dialog's own event loop.
    QString seen;
    QTimer::singleShot(50, [&seen]
                       {
                           if (QWidget *modal = QApplication::activeModalWidget())
                           {
                               seen = modal->windowTitle();
                               modal->close();
                           }
                       });
    about->trigger();
    EXPECT_EQ(seen, QStringLiteral("About Atlas"));
}
