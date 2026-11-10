#pragma once
#include "language_service/diagnostic.h"

#include <QDockWidget>
#include <QVector>

class QLabel;
class QStackedWidget;
class QTableView;

namespace Kites
{
class ProblemsModel;

/**
 * @brief Bottom dock listing the diagnostics from the last Run.
 *
 * Owns a ProblemsModel and a table view over it, plus an empty state shown
 * when there is nothing to report. Navigation is not performed here: a
 * double-click or Enter on a navigable row emits diagnosticActivated() and
 * MainWindow routes it to the editor.
 */
class ProblemsDock : public QDockWidget
{
    Q_OBJECT
public:
    explicit ProblemsDock(QWidget *parent = nullptr);

    /**
     * @brief Replaces the listed problems.
     * @param diagnostics  what to show; empty shows the empty state.
     * @param sourceLineCount lines in the user's source, so rows pointing past
     *        the end can be marked unreachable. 0 disables that check.
     */
    void setDiagnostics(const QVector<Diagnostic> &diagnostics, int sourceLineCount = 0);

    /// Rows currently listed, after deduplication.
    int problemCount() const;

    ProblemsModel *model() const;

signals:
    /// A navigable row was activated. Rows with no usable line never emit.
    void diagnosticActivated(const Diagnostic &diagnostic);

private:
    void activateRow(int row);
    void updateTitle();

    ProblemsModel *m_model = nullptr;
    QTableView *m_view = nullptr;
    QLabel *m_emptyState = nullptr;
    QStackedWidget *m_stack = nullptr;
};
} // namespace Kites
