#include "problems_model.h"
#include "ui/common/table_font.h"
#include "ui/theme/theme_manager.h"

#include <QPainter>
#include <QPixmap>

#include <algorithm>
#include <tuple>

namespace Kites
{
namespace
{
constexpr int severityDotSize = 10;

/// Key that decides whether two diagnostics are the same problem. See
/// ProblemsModel::setDiagnostics for why the message is only part of it when
/// the span is unknown.
using DedupKey = std::tuple<int, int, int, int, int, QString>;

DedupKey dedupKeyFor(const Diagnostic &diagnostic)
{
    const bool haveSpan = diagnostic.startColumn > 0;
    return {diagnostic.line,
            diagnostic.startColumn,
            diagnostic.endColumn,
            static_cast<int>(diagnostic.severity),
            static_cast<int>(diagnostic.category),
            haveSpan ? QString() : diagnostic.message};
}
} // namespace

QColor severityColor(DiagnosticSeverity severity)
{
    const ThemeManager &theme = ThemeManager::getInstance();
    switch (severity)
    {
    case DiagnosticSeverity::Error:       return theme.getErrorColor();
    case DiagnosticSeverity::Warning:     return theme.getWarningColor();
    case DiagnosticSeverity::Information: return theme.getInfoColor();
    case DiagnosticSeverity::Hint:        return theme.getInfoColor();
    }
    return theme.getErrorColor();
}

QString severityText(DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::Error:       return QStringLiteral("Error");
    case DiagnosticSeverity::Warning:     return QStringLiteral("Warning");
    case DiagnosticSeverity::Information: return QStringLiteral("Info");
    case DiagnosticSeverity::Hint:        return QStringLiteral("Hint");
    }
    return QStringLiteral("Error");
}

ProblemsModel::ProblemsModel(QObject *parent)
    : QAbstractTableModel(parent)
{
    // the dots are tinted from theme tokens, so they have to be repainted when
    // the theme changes
    connect(&ThemeManager::getInstance(), &ThemeManager::editorThemeChangedSignal, this,
            [this](const QString &) { rebuildIcons(); });
}

void ProblemsModel::rebuildIcons()
{
    if (m_diagnostics.isEmpty())
        return;
    emit dataChanged(index(0, SeverityColumn),
                     index(m_diagnostics.size() - 1, SeverityColumn),
                     {Qt::DecorationRole});
}

void ProblemsModel::setDiagnostics(const QVector<Diagnostic> &diagnostics)
{
    QVector<Diagnostic> deduplicated;
    deduplicated.reserve(diagnostics.size());
    QList<DedupKey> seen;
    for (const Diagnostic &diagnostic : diagnostics)
    {
        const DedupKey key = dedupKeyFor(diagnostic);
        if (seen.contains(key))
            continue; // first message for this key wins
        seen.append(key);
        deduplicated.append(diagnostic);
    }

    // stable, so equal keys keep the order the assembler produced them in
    std::stable_sort(deduplicated.begin(), deduplicated.end(),
                     [](const Diagnostic &a, const Diagnostic &b)
                     {
                         if (a.line != b.line)
                             return a.line < b.line;
                         return a.startColumn < b.startColumn;
                     });

    beginResetModel();
    m_diagnostics = deduplicated;
    endResetModel();
}

void ProblemsModel::clear()
{
    if (m_diagnostics.isEmpty())
        return;
    beginResetModel();
    m_diagnostics.clear();
    endResetModel();
}

int ProblemsModel::problemCount() const
{
    return static_cast<int>(m_diagnostics.size());
}

void ProblemsModel::setDocumentLineCount(int lineCount)
{
    if (m_documentLineCount == lineCount)
        return;
    m_documentLineCount = lineCount;
    if (!m_diagnostics.isEmpty())
    {
        // the Location column renders an em dash for rows that fall outside
        emit dataChanged(index(0, LocationColumn),
                         index(m_diagnostics.size() - 1, LocationColumn));
    }
}

const Diagnostic *ProblemsModel::diagnosticAt(int row) const
{
    if (row < 0 || row >= m_diagnostics.size())
        return nullptr;
    return &m_diagnostics.at(row);
}

int ProblemsModel::navigationLine(int row) const
{
    const Diagnostic *diagnostic = diagnosticAt(row);
    if (!diagnostic)
        return 0;
    return diagnostic->originalLine > 0 ? diagnostic->originalLine : diagnostic->line;
}

bool ProblemsModel::isNavigable(int row) const
{
    const int line = navigationLine(row);
    if (line <= 0)
        return false;
    if (m_documentLineCount > 0 && line > m_documentLineCount)
        return false;
    return true;
}

int ProblemsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_diagnostics.size());
}

int ProblemsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ProblemsModel::data(const QModelIndex &index, int role) const
{
    const Diagnostic *diagnostic = diagnosticAt(index.row());
    if (!diagnostic)
        return {};

    if (role == Qt::DecorationRole && index.column() == SeverityColumn)
    {
        QPixmap dot(severityDotSize, severityDotSize);
        dot.fill(Qt::transparent);
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(severityColor(diagnostic->severity));
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(dot.rect().adjusted(1, 1, -1, -1));
        painter.end();
        return QIcon(dot);
    }

    if (role == Qt::FontRole && index.column() == LocationColumn)
    {
        return tableNumericFont(); // line:column reads as data, not prose
    }

    if (role == Qt::TextAlignmentRole && index.column() == LocationColumn)
    {
        return QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
    }

    if (role == Qt::ToolTipRole)
    {
        return diagnostic->message;
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column())
    {
    case SeverityColumn:
        return severityText(diagnostic->severity);
    case LocationColumn:
    {
        if (!isNavigable(index.row()))
            return QStringLiteral("—"); // em dash: nowhere to go
        const int line = navigationLine(index.row());
        if (diagnostic->startColumn > 0)
            return QStringLiteral("%1:%2").arg(line).arg(diagnostic->startColumn);
        return QString::number(line);
    }
    case MessageColumn:
        return diagnostic->message;
    case CategoryColumn:
        return QString::fromUtf8(toString(diagnostic->category).data(),
                                 static_cast<int>(toString(diagnostic->category).size()));
    default:
        return {};
    }
}

QVariant ProblemsModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section)
    {
    case SeverityColumn: return QStringLiteral("Severity");
    case LocationColumn: return QStringLiteral("Location");
    case MessageColumn:  return QStringLiteral("Message");
    case CategoryColumn: return QStringLiteral("Category");
    default:             return {};
    }
}
} // namespace Kites
