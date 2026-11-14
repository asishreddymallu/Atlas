#include "find_engine.h"

#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace Kites
{
namespace FindEngine
{
namespace
{
/// The lookarounds that make "whole word" mean [A-Za-z0-9_] on both sides.
/// Non-capturing throughout, so the user's own group numbers are untouched.
QString wrappedForWholeWord(const QString &body)
{
    return QStringLiteral("(?<![A-Za-z0-9_])(?:") + body + QStringLiteral(")(?![A-Za-z0-9_])");
}

QRegularExpression compile(const QString &pattern, const FindOptions &options, QString &error)
{
    QString body = options.regex ? pattern : QRegularExpression::escape(pattern);
    if (options.wholeWord)
        body = wrappedForWholeWord(body);

    QRegularExpression::PatternOptions flags = QRegularExpression::MultilineOption;
    if (!options.caseSensitive)
        flags |= QRegularExpression::CaseInsensitiveOption;

    QRegularExpression expression(body, flags);
    if (!expression.isValid())
        error = expression.errorString();
    return expression;
}
} // namespace

bool isWordCharacter(QChar c)
{
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
           (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_');
}

FindResult findAll(const QString &text, const QString &pattern, const FindOptions &options,
                   int cap)
{
    FindResult result;
    // An empty box is not a failed search, it is no search.
    if (pattern.isEmpty())
        return result;

    const QRegularExpression expression = compile(pattern, options, result.error);
    if (!result.error.isEmpty())
        return result;

    int offset = 0;
    while (offset <= text.size())
    {
        const QRegularExpressionMatch match = expression.match(text, offset);
        if (!match.hasMatch())
            break;

        if (result.matches.size() >= cap)
        {
            result.truncated = true;
            break;
        }

        FindMatch found;
        found.start   = match.capturedStart();
        found.length  = match.capturedLength();
        found.matched = match.captured(0);
        for (int group = 1; group <= match.lastCapturedIndex(); ++group)
            found.captures << match.captured(group);
        result.matches.append(found);

        // A zero-length match would otherwise be found at the same place for
        // ever, so the scan steps one character past it.
        offset = found.length > 0 ? found.end() : found.start + 1;
    }
    return result;
}

int nextIndex(const QList<FindMatch> &matches, int position, bool forward, bool wrap)
{
    if (matches.isEmpty())
        return -1;

    if (forward)
    {
        for (int i = 0; i < matches.size(); ++i)
            if (matches.at(i).start >= position)
                return i;
        return wrap ? 0 : -1;
    }

    for (int i = matches.size() - 1; i >= 0; --i)
        if (matches.at(i).start < position)
            return i;
    return wrap ? matches.size() - 1 : -1;
}

QString expandReplacement(const FindMatch &match, const QString &replacement,
                          const FindOptions &options)
{
    // Plain mode is literal: a dollar sign is a dollar sign.
    if (!options.regex)
        return replacement;

    QString out;
    out.reserve(replacement.size());
    for (int i = 0; i < replacement.size(); ++i)
    {
        const QChar c = replacement.at(i);
        if (c != QLatin1Char('$') || i + 1 >= replacement.size())
        {
            out += c;
            continue;
        }

        const QChar next = replacement.at(i + 1);
        if (next == QLatin1Char('$'))
        {
            out += QLatin1Char('$');
            ++i;
        }
        else if (next == QLatin1Char('0'))
        {
            out += match.matched;
            ++i;
        }
        else if (next.isDigit())
        {
            const int group = next.digitValue();
            out += match.captures.value(group - 1);
            ++i;
        }
        else
        {
            // A dollar in front of anything else is left exactly as typed.
            out += c;
        }
    }
    return out;
}

QList<ReplaceEdit> replaceAllEdits(const FindResult &result, const QString &replacement,
                                   const FindOptions &options)
{
    QList<ReplaceEdit> edits;
    edits.reserve(result.matches.size());
    for (const FindMatch &match : result.matches)
        edits.append({match.start, match.length, expandReplacement(match, replacement, options)});
    return edits;
}
} // namespace FindEngine
} // namespace Kites
