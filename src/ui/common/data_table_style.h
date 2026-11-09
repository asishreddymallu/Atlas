#pragma once
#include <QtCore/Qt>

class QTableView;

namespace Kites
{
/**
 * @brief Applies the shared data-table presentation: compact 24px rows, zebra
 * striping, row-wise selection, no grid, and a hidden vertical header.
 *
 * Purely presentational - it touches no model and no data.
 *
 * @param headerAlignment alignment for the horizontal header labels. Qt gives
 *        QHeaderView a single default alignment rather than a per-section one,
 *        so pass AlignRight for tables whose columns are all numeric and
 *        AlignLeft for mixed ones. Per-column header alignment would mean
 *        either a custom header or headerData(), both out of scope here.
 */
void applyDataTableStyle(QTableView *view,
                         Qt::Alignment headerAlignment = Qt::AlignLeft | Qt::AlignVCenter);
} // namespace Kites
