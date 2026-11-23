/**
 * @file memory_layout_test.cpp
 * @brief The two data panels put their controls above the data.
 *
 * Both panels used to read bottom-up: `memory_tab.ui` had the table at
 * row 0 and Display Type, the search box and the Search button on row 2;
 * `register_container.ui` had the two collapsible sections at row 0 and
 * Display Type at row 1. Measured offscreen at 1200x800 before the move,
 * in panel coordinates: the memory table spanned y=9..577 and its
 * controls sat at y=583..617, and the register sections spanned y=9..581
 * with their combo at y=587..617.
 *
 * Geometry is asserted rather than row indices, because a `.ui` row
 * number is not what a reader sees - a widget can be on row 0 and still
 * be drawn last if the layout is nested.
 *
 * The second half is the part the move could silently break. Each of
 * these controls has a ribbon twin, and the tab's own widget stays the
 * state. Five tests in `ribbon_navigation_test.cpp` already cover that
 * sync in both directions (`:951`, `:1001`, `:1025`, `:1126`, `:1165`),
 * and they keep working because they reach their widgets by objectName.
 * What they do not say is that the widget the ribbon drives is the one
 * now at the top. These do, so the sync and the geometry cannot drift
 * apart.
 *
 * `pageGroupNamed`/`buttonNamed`/`expectOnlyChecked` are copied from
 * `ribbon_navigation_test.cpp:921-948` rather than shared: they live in
 * that file's anonymous namespace, and exporting them would mean a new
 * test-support header for three four-line functions.
 */
#include "ui/common/display_base_types.h"
#include "ui/main_window/main_window.h"
#include "ui/memory_tab/memory_tab.h"
#include "ui/register_table/register_container.h"
#include "ui/ribbon/ribbon_bar.h"
#include "ui/ribbon/ribbon_group.h"
#include "ui/ribbon/ribbon_page.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QFontMetrics>
#include <QHeaderView>
#include <QTableView>
#include <QToolButton>
#include <QWidget>

#include <cstdlib>

using namespace Atlas;

namespace
{
/// A shown window with the Memory tab in front, so every geometry below
/// is a laid-out one rather than a default.
struct Window
{
    MainWindow        window;
    MemoryTab        *memory    {nullptr};
    RegisterContainer *registers {nullptr};
    RibbonBar        *bar       {nullptr};

    Window()
    {
        window.resize(1200, 800);
        window.show();
        window.setCurrentTab(MainWindow::MemoryTabIndex);
        QApplication::processEvents();
        memory    = window.findChild<MemoryTab *>();
        registers = window.findChild<RegisterContainer *>();
        bar       = window.findChild<RibbonBar *>();
    }

    bool complete() const { return memory && registers && bar; }
};

/// `child`'s top and bottom edge in `panel`'s coordinates.
int topIn(QWidget *panel, QWidget *child)
{
    return child->mapTo(panel, QPoint(0, 0)).y();
}
int bottomIn(QWidget *panel, QWidget *child)
{
    return topIn(panel, child) + child->height();
}

RibbonGroup *pageGroupNamed(RibbonBar *bar, MainWindow::TabIndex page, const QString &caption)
{
    for (RibbonGroup *group : bar->page(static_cast<int>(page))->groups())
        if (group->caption() == caption)
            return group;
    return nullptr;
}

QToolButton *buttonNamed(RibbonGroup *group, const QString &text)
{
    if (!group)
        return nullptr;
    for (QToolButton *button : group->buttons())
        if (button->text() == text)
            return button;
    return nullptr;
}

void expectOnlyChecked(RibbonGroup *group, const QString &text)
{
    for (QToolButton *button : group->buttons())
        EXPECT_EQ(button->isChecked(), button->text() == text)
            << "'" << button->text().toStdString() << "' in group '"
            << group->caption().toStdString() << "'";
}
} // namespace

TEST(MemoryPanelLayout, EveryControlIsAboveTheTable)
{
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *table = ui.memory->findChild<QTableView *>("memoryTableView");
    ASSERT_NE(table, nullptr);
    const int tableTop = topIn(ui.memory, table);

    const struct
    {
        const char *name;
        QWidget    *widget;
    } controls[] = {
        {"Display Type combo", ui.memory->findChild<QComboBox *>("comboBox")},
        {"byte order combo", ui.memory->findChild<QComboBox *>("byteOrderCombo")},
        {"search bar", ui.memory->findChild<QLineEdit *>("searchBar")},
        {"Search button", ui.memory->findChild<QPushButton *>("searchButton")},
        {"Display Type label", ui.memory->findChild<QLabel *>("label")},
        {"Search Memory label", ui.memory->findChild<QLabel *>("label_2")},
    };

    for (const auto &control : controls)
    {
        ASSERT_NE(control.widget, nullptr) << control.name << " is missing";
        EXPECT_LE(bottomIn(ui.memory, control.widget), tableTop)
            << control.name << " is not above the table: it ends at "
            << bottomIn(ui.memory, control.widget) << ", the table starts at " << tableTop;
    }

    // And the table takes the rest: nothing is left under it.
    EXPECT_GT(table->height(), ui.memory->height() / 2)
        << "the table is no longer the bulk of the tab";
    EXPECT_GE(bottomIn(ui.memory, table), ui.memory->height() - 20)
        << "something is sitting below the table";
}

TEST(RegisterPanelLayout, DisplayTypeIsAboveTheSections)
{
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *sections = ui.registers->findChild<QWidget *>("sectionsContainer");
    auto *combo    = ui.registers->findChild<QComboBox *>("displayType");
    auto *label    = ui.registers->findChild<QLabel *>("label");
    ASSERT_NE(sections, nullptr);
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(label, nullptr);

    const int sectionsTop = topIn(ui.registers, sections);
    EXPECT_LE(bottomIn(ui.registers, combo), sectionsTop)
        << "the combo ends at " << bottomIn(ui.registers, combo) << ", the sections start at "
        << sectionsTop;
    EXPECT_LE(bottomIn(ui.registers, label), sectionsTop);

    EXPECT_GE(bottomIn(ui.registers, sections), ui.registers->height() - 20)
        << "something is sitting below the register sections";
}

TEST(MemoryPanelLayout, TheByteOrderControlIsInTheTopRowBesideDisplayType)
{
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *order   = ui.memory->findChild<QComboBox *>("byteOrderCombo");
    auto *display = ui.memory->findChild<QComboBox *>("comboBox");
    ASSERT_NE(order, nullptr) << "there is no byte-order control";
    ASSERT_NE(display, nullptr);

    EXPECT_EQ(order->count(), 2) << "the byte-order control is not two-state";
    EXPECT_EQ(order->itemText(0).toStdString(), "Little");
    EXPECT_EQ(order->itemText(1).toStdString(), "Big");
    EXPECT_EQ(order->currentText().toStdString(), "Little") << "the default is not little-endian";

    // Same row as Display Type, within a control's height.
    EXPECT_LT(std::abs(topIn(ui.memory, order) - topIn(ui.memory, display)), display->height())
        << "the byte-order control is not on the controls row";

    // And that row is the top one. Without this the test passed against a
    // stub that put the control at the BOTTOM beside the old Display
    // Type - "on the controls row" was true and useless.
    auto *table = ui.memory->findChild<QTableView *>("memoryTableView");
    ASSERT_NE(table, nullptr);
    EXPECT_LE(bottomIn(ui.memory, order), topIn(ui.memory, table))
        << "the byte-order control is below the table";
}

TEST(MemoryPanelLayout, TheByteOrderComboDrivesTheModel)
{
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *order = ui.memory->findChild<QComboBox *>("byteOrderCombo");
    auto *table = ui.memory->findChild<QTableView *>("memoryTableView");
    ASSERT_NE(order, nullptr);
    ASSERT_NE(table, nullptr);
    ASSERT_NE(table->model(), nullptr);

    auto header = [table] {
        return table->model()
            ->headerData(1, Qt::Horizontal, Qt::DisplayRole)
            .toString()
            .toStdString();
    };

    EXPECT_EQ(header(), "Double Word (LE)");
    EXPECT_EQ(ui.memory->byteOrder(), ByteOrder::Little);

    order->setCurrentText(QStringLiteral("Big"));
    EXPECT_EQ(ui.memory->byteOrder(), ByteOrder::Big);
    EXPECT_EQ(header(), "Double Word (BE)") << "the combo did not reach the model";

    order->setCurrentText(QStringLiteral("Little"));
    EXPECT_EQ(ui.memory->byteOrder(), ByteOrder::Little);
    EXPECT_EQ(header(), "Double Word (LE)");
}

TEST(MemoryPanelLayout, TheDoubleWordHeaderIsWideEnoughToReadInBothOrders)
{
    // The suffix exists so the order on screen is never ambiguous, which
    // it cannot do if it is clipped. The first after-screenshot of this
    // stage showed exactly that: ten equal-width columns, and a header
    // reading "...Word (LE)".
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *table = ui.memory->findChild<QTableView *>("memoryTableView");
    ASSERT_NE(table, nullptr);
    ASSERT_NE(table->model(), nullptr);
    QHeaderView *header = table->horizontalHeader();
    ASSERT_NE(header, nullptr);

    for (ByteOrder order : {ByteOrder::Little, ByteOrder::Big})
    {
        ui.memory->setByteOrder(order);
        QApplication::processEvents();

        const QString text =
            table->model()->headerData(1, Qt::Horizontal, Qt::DisplayRole).toString();
        const int needed = QFontMetrics(header->font()).horizontalAdvance(text);
        EXPECT_GE(header->sectionSize(1), needed)
            << "'" << text.toStdString() << "' is clipped: the section is "
            << header->sectionSize(1) << "px and the text needs " << needed << "px";
    }
}

TEST(MemoryPanelLayout, TheRibbonStillDrivesTheWidgetsThatMovedToTheTop)
{
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *combo  = ui.memory->findChild<QComboBox *>("comboBox");
    auto *table  = ui.memory->findChild<QTableView *>("memoryTableView");
    auto *button = ui.memory->findChild<QPushButton *>("searchButton");
    auto *search = ui.memory->findChild<QLineEdit *>("searchBar");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(table, nullptr);
    ASSERT_NE(button, nullptr);
    ASSERT_NE(search, nullptr);

    // The widgets under test are the moved ones, not some other copy.
    const int tableTop = topIn(ui.memory, table);
    ASSERT_LE(bottomIn(ui.memory, combo), tableTop);
    ASSERT_LE(bottomIn(ui.memory, button), tableTop);

    RibbonGroup *display = pageGroupNamed(ui.bar, MainWindow::MemoryTabIndex, "Display");
    ASSERT_NE(display, nullptr);

    // Ribbon -> tab.
    buttonNamed(display, "Binary")->click();
    EXPECT_EQ(combo->currentText().toStdString(), "Binary");
    EXPECT_EQ(ui.memory->displayBase(), Base::Binary);

    // Tab -> ribbon.
    combo->setCurrentText(QStringLiteral("Decimal"));
    expectOnlyChecked(display, "Decimal");

    combo->setCurrentText(QStringLiteral("Hex"));
    expectOnlyChecked(display, "Hex");

    // Find still goes through the tab's own button, which is also at the top.
    int clicks = 0;
    QObject::connect(button, &QPushButton::clicked, &ui.window, [&clicks] { ++clicks; });
    RibbonGroup *find = pageGroupNamed(ui.bar, MainWindow::MemoryTabIndex, "Find");
    ASSERT_NE(find, nullptr);
    const QString topBefore = table->model()->index(0, 0).data().toString();
    search->setText(QStringLiteral("0x100"));
    buttonNamed(find, "Search")->click();
    EXPECT_EQ(clicks, 1) << "the ribbon stopped going through the tab's button";

    // setCentralAddress puts the address in the MIDDLE of the window, and
    // this window is shown, so the view is many rows tall and row 0 is the
    // highest address on screen rather than the one searched for. The
    // check is that the address is now in view and that the view moved.
    bool inView = false;
    for (int r = 0; r < table->model()->rowCount(); ++r)
        if (table->model()->index(r, 0).data().toString() == QStringLiteral("0x100"))
            inView = true;
    EXPECT_TRUE(inView) << "the table did not move to the searched address; the top row is "
                        << table->model()->index(0, 0).data().toString().toStdString();
    EXPECT_NE(table->model()->index(0, 0).data().toString(), topBefore);
}

TEST(RegisterPanelLayout, TheRibbonStillDrivesTheComboThatMovedToTheTop)
{
    Window ui;
    ASSERT_TRUE(ui.complete());

    auto *combo    = ui.registers->findChild<QComboBox *>("displayType");
    auto *sections = ui.registers->findChild<QWidget *>("sectionsContainer");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(sections, nullptr);
    ASSERT_LE(bottomIn(ui.registers, combo), topIn(ui.registers, sections));

    RibbonGroup *group = pageGroupNamed(ui.bar, MainWindow::ProcessorTabIndex, "Registers");
    ASSERT_NE(group, nullptr);

    buttonNamed(group, "Binary")->click();
    EXPECT_EQ(combo->currentText().toStdString(), "Binary");
    EXPECT_EQ(ui.registers->displayBase(), Base::Binary);

    combo->setCurrentText(QStringLiteral("Decimal/Float"));
    expectOnlyChecked(group, "Decimal/Float");

    combo->setCurrentText(QStringLiteral("Hexadecimal"));
    expectOnlyChecked(group, "Hexadecimal");
}
