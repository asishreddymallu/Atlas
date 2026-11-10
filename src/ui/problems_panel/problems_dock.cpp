#include "problems_dock.h"
#include "problems_model.h"
#include "ui/common/data_table_style.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QStackedWidget>
#include <QTableView>
#include <QVBoxLayout>

namespace Kites
{
ProblemsDock::ProblemsDock(QWidget *parent)
    : QDockWidget(parent)
{
    setObjectName(QStringLiteral("problemsDock"));
    setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    m_model = new ProblemsModel(this);

    m_view = new QTableView(this);
    m_view->setObjectName(QStringLiteral("problemsView"));
    m_view->setModel(m_model);
    applyDataTableStyle(m_view); // same row height, zebra and header as the other tables
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->horizontalHeader()->setSectionResizeMode(ProblemsModel::SeverityColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(ProblemsModel::LocationColumn,
                                                     QHeaderView::ResizeToContents);
    m_view->horizontalHeader()->setSectionResizeMode(ProblemsModel::MessageColumn,
                                                     QHeaderView::Stretch);
    m_view->horizontalHeader()->setSectionResizeMode(ProblemsModel::CategoryColumn,
                                                     QHeaderView::ResizeToContents);

    m_emptyState = new QLabel(this);
    m_emptyState->setObjectName(QStringLiteral("problemsEmptyState"));
    m_emptyState->setAlignment(Qt::AlignCenter);
    m_emptyState->setTextFormat(Qt::PlainText);
    m_emptyState->setText(tr("No problems.\n\nRun your program (F5) to check it."));

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_emptyState); // index 0
    m_stack->addWidget(m_view);       // index 1
    setWidget(m_stack);

    connect(m_view, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index) { activateRow(index.row()); });
    // Enter on the selected row does the same as a double-click
    connect(m_view, &QAbstractItemView::activated, this,
            [this](const QModelIndex &index) { activateRow(index.row()); });

    updateTitle();
}

void ProblemsDock::setDiagnostics(const QVector<Diagnostic> &diagnostics, int sourceLineCount)
{
    m_model->setDocumentLineCount(sourceLineCount);
    m_model->setDiagnostics(diagnostics);
    m_stack->setCurrentIndex(m_model->problemCount() > 0 ? 1 : 0);
    updateTitle();
}

int ProblemsDock::problemCount() const
{
    return m_model->problemCount();
}

ProblemsModel *ProblemsDock::model() const
{
    return m_model;
}

void ProblemsDock::activateRow(int row)
{
    // a row with no usable line stays listed but goes nowhere
    if (!m_model->isNavigable(row))
        return;
    const Diagnostic *diagnostic = m_model->diagnosticAt(row);
    if (!diagnostic)
        return;
    emit diagnosticActivated(*diagnostic);
}

void ProblemsDock::updateTitle()
{
    const int count = m_model->problemCount();
    setWindowTitle(count > 0 ? tr("Problems (%1)").arg(count) : tr("Problems"));
}
} // namespace Kites
