#include "data_table_style.h"
#include <QHeaderView>
#include <QTableView>

namespace Kites
{
namespace
{
// spacing scale: compact rows, chosen so the register panel keeps roughly the
// number of registers visible that it had before
constexpr int compactRowHeight = 24;
} // namespace

void applyDataTableStyle(QTableView *view, Qt::Alignment headerAlignment)
{
    if (!view)
        return;

    view->setAlternatingRowColors(true);
    view->setShowGrid(false);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setWordWrap(false);

    QHeaderView *vertical = view->verticalHeader();
    vertical->setVisible(false);
    vertical->setSectionResizeMode(QHeaderView::Fixed);
    vertical->setDefaultSectionSize(compactRowHeight);

    view->horizontalHeader()->setDefaultAlignment(headerAlignment);
    view->horizontalHeader()->setHighlightSections(false);
}
} // namespace Kites
