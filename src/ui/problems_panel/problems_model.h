#pragma once
#include "language_service/diagnostic.h"

#include <QAbstractTableModel>
#include <QColor>
#include <QIcon>
#include <QVector>

namespace Kites
{
/**
 * @brief Table model behind the Problems dock.
 *
 * Holds the diagnostics from the last Run, deduplicated and ordered. Knows
 * nothing about widgets: the dock supplies a view, and navigation asks
 * isNavigable()/diagnosticAt() rather than reading cells back out.
 *
 * Columns: Severity (text and a themed dot), Location, Message, Category.
 */
class ProblemsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column
    {
        SeverityColumn = 0,
        LocationColumn,
        MessageColumn,
        CategoryColumn,
        ColumnCount
    };

    explicit ProblemsModel(QObject *parent = nullptr);

    /**
     * @brief Replaces the contents with `diagnostics`, deduplicated and sorted.
     *
     * Deduplication key is the **expanded** line, not the mapped original one,
     * so two reports about the same assembled line collapse even when several
     * expanded lines map back to one source line. When a span is known the key
     * is (line, startColumn, endColumn, severity, category) and the first
     * message wins - that is what folds the pair parser.cpp:615/:662 emits for
     * one stray token into a single row. When the span is unknown
     * (startColumn == 0) the message joins the key instead, so two genuinely
     * different second-pass errors on one line stay two rows.
     *
     * Order is by expanded line, then startColumn, and is stable, so
     * diagnostics that compare equal keep their original relative order.
     * Expansion only ever inserts lines, so this is also non-decreasing in the
     * original source line.
     */
    void setDiagnostics(const QVector<Diagnostic> &diagnostics);

    /// Drops every row. Used on a successful Run and by resetErrorLines().
    void clear();

    /// Row count after deduplication; what the dock title shows.
    int problemCount() const;

    /**
     * @brief Tells the model how long the document is, so it can mark rows
     *        whose line no longer exists.
     * @param lineCount 1-based line count, or 0 for "unknown, do not check".
     */
    void setDocumentLineCount(int lineCount);

    /// False when the row has no line (0) or points past the document. Such a
    /// row stays visible with an em dash for its location, but navigation to
    /// it is a no-op.
    bool isNavigable(int row) const;

    /// The diagnostic behind `row`, or nullptr when `row` is out of range.
    const Diagnostic *diagnosticAt(int row) const;

    /// The line navigation should jump to: the mapped original line when one
    /// was recorded, otherwise the line as reported. 0 when neither is usable.
    int navigationLine(int row) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void rebuildIcons();

    QVector<Diagnostic> m_diagnostics;
    int m_documentLineCount = 0;
};

/// Theme colour for a severity. Hint shares Information's colour.
QColor severityColor(DiagnosticSeverity severity);

/// Human-readable severity name shown in the Severity column, so the row does
/// not rely on colour alone.
QString severityText(DiagnosticSeverity severity);
} // namespace Kites
