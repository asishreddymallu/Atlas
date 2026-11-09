#pragma once
#include <QFont>

namespace Kites
{
/**
 * @brief Monospace font used by data tables, at the table size from the type
 * scale.
 *
 * Returned by reference and built once, so a model's Qt::FontRole - which Qt
 * calls for every cell on every repaint - costs a lookup rather than a QFont
 * construction. Built on first call, which is always after QApplication
 * exists; it is deliberately not a namespace-scope object.
 */
const QFont &tableNumericFont();
} // namespace Kites
