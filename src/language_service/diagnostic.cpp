#include "diagnostic.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Kites
{
QVector<Diagnostic> parseDiagnosticsJson(const QByteArray &json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isNull() || !doc.isObject())
    {
        // same outcome as the original code path: nothing to show
        return {};
    }

    const QJsonArray errors = doc.object().value("errors").toArray();

    QVector<Diagnostic> diagnostics;
    diagnostics.reserve(errors.size());
    for (const QJsonValue &value : errors)
    {
        const QJsonObject entry = value.toObject();

        Diagnostic diagnostic;
        diagnostic.line        = entry.value("line").toInt();
        diagnostic.startColumn = entry.value("column").toInt();
        diagnostic.endColumn   = entry.value("endColumn").toInt();
        diagnostic.severity    = severityFromString(
            entry.value("severity").toString().toStdString(), DiagnosticSeverity::Error);
        diagnostic.category    = categoryFromString(
            entry.value("category").toString().toStdString(), DiagnosticCategory::Syntax);
        diagnostic.message     = entry.value("message").toString();

        diagnostics.append(diagnostic);
    }
    return diagnostics;
}
} // namespace Kites
