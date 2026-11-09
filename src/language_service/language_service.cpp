#include "language_service.h"
#include "assembler/assembler.h"
#include "completion_provider.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include <QRegularExpression>
#include <QSet>
#include <QtConcurrentRun>
#include <sstream>

namespace Kites
{
namespace
{
QVector<Diagnostic> computeDiagnostics(QString source)
{
    try {
        std::vector<int> expandedLineToOriginalLine;
        const std::string expanded = customPseudoManager::expandPseudoInstruction(
            source.toStdString(), expandedLineToOriginalLine);

        std::istringstream stream(expanded);
        std::vector<ParseError> parseErrors;
        ;

        QVector<Diagnostic> diagnostics;
        diagnostics.reserve(static_cast<int>(parseErrors.size()));
        for (const ParseError &info : parseErrors)
        {
            int originalLine = static_cast<int>(info.line);
            if (info.line >= 1 && info.line <= expandedLineToOriginalLine.size())
            {
                originalLine = expandedLineToOriginalLine[info.line - 1];
            }
            Diagnostic diagnostic;
            diagnostic.line        = originalLine;
            diagnostic.startColumn = static_cast<int>(info.column);
            diagnostic.endColumn   = static_cast<int>(info.end_column);
            diagnostic.severity    = info.severity;
            diagnostic.category    = info.category;
            diagnostic.message     = QString::fromStdString(info.message);
            diagnostics.append(diagnostic);
        }
        return diagnostics;
    } catch (const std::exception& e) {
        Diagnostic failure;
        failure.line = 1;
        failure.message = QString("Internal error: ") + QString::fromStdString(e.what());
        return QVector<Diagnostic>{failure};
    } catch (...) {
        Diagnostic failure;
        failure.line = 1;
        failure.message = QString("Unknown error during assembly");
        return QVector<Diagnostic>{failure};
    }
}

// Same convention as SyntaxHighlighter's label rule: a label must be alone on
// its line ("name:"), see syntax_highlighter.cpp.
QStringList scanLabels(const QString &source)
{
    static const QRegularExpression labelPattern(R"(^[A-Za-z_][A-Za-z0-9_.$]*:$)");

    QStringList labels;
    for (const QString &line : source.split('\n'))
    {
        const QString trimmed = line.trimmed();
        if (labelPattern.match(trimmed).hasMatch())
        {
            labels << trimmed.left(trimmed.size() - 1); // drop trailing ':'
        }
    }
    return labels;
}
} // namespace

LanguageService::LanguageService(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<QVector<Diagnostic>>::finished, this,
            &LanguageService::handleAssembleFinished);
}

void LanguageService::requestDiagnostics(const QString &source)
{
    m_watcher.setFuture(QtConcurrent::run(computeDiagnostics, source));
}

void LanguageService::handleAssembleFinished()
{
    try {
        //here emitting the signal will never throw
        // only m_watcher.result() can throw if the thread function throws, 
        emit diagnosticsReadySignal(m_watcher.result());
    } catch (const std::exception& e) {
        Diagnostic failure;
        failure.line = 1;
        failure.message = QString::fromStdString(e.what());
        emit diagnosticsReadySignal(QVector<Diagnostic>{failure});
    } catch (...) {
        Diagnostic failure;
        failure.line = 1;
        failure.message = QStringLiteral("Unknown exception in language service thread");
        emit diagnosticsReadySignal(QVector<Diagnostic>{failure});
    }
}

QStringList LanguageService::completions(const QString &prefix, const QString &source)
{
    if (prefix.isEmpty())
        return {};

    QSet<QString> matches;
    for (const QString &symbol : CompletionProvider::getInstance().staticSymbols())
    {
        if (symbol.startsWith(prefix, Qt::CaseInsensitive))
            matches.insert(symbol);
    }
    for (const QString &label : scanLabels(source))
    {
        if (label.startsWith(prefix, Qt::CaseInsensitive))
            matches.insert(label);
    }

    QStringList result(matches.begin(), matches.end());
    result.sort(Qt::CaseInsensitive);
    return result;
}

} // namespace Kites
