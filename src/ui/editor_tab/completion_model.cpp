#include "ui/editor_tab/completion_model.h"

#include "common/register_names.h"

#include <QSet>
#include <algorithm>

namespace Kites
{
namespace Completion
{
namespace
{
bool isSegmentBreak(QChar c)
{
    return c == QLatin1Char('.') || c == QLatin1Char('_') || c == QLatin1Char('$');
}

/// True when `index` starts a segment of `label`: the first character, or the
/// one after a '.', '_' or '$'.
bool startsSegment(const QString &label, int index)
{
    return index == 0 || isSegmentBreak(label.at(index - 1));
}

const QSet<QString> &registerNames()
{
    static const QSet<QString> names = []
    {
        QSet<QString> set;
        for (const char *name : register_names::kGprNames)
            set.insert(QString::fromLatin1(name).toLower());
        for (const char *alias : register_names::kGprAliases)
            set.insert(QString::fromLatin1(alias).toLower());
        for (const char *name : register_names::kFprNames)
            set.insert(QString::fromLatin1(name).toLower());
        for (const char *alias : register_names::kFprAliases)
            set.insert(QString::fromLatin1(alias).toLower());
        return set;
    }();
    return names;
}
} // namespace

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.') ||
           c == QLatin1Char('$');
}

Kind kindOf(const QString &symbol)
{
    if (symbol.startsWith(QLatin1Char('.')))
        return Kind::Directive;
    if (registerNames().contains(symbol.toLower()))
        return Kind::Register;
    return Kind::Instruction;
}

Match score(const QString &label, const QString &pattern)
{
    Match result;
    if (pattern.isEmpty())
    {
        // Everything matches equally; Ctrl+Space relies on this.
        result.score = 1;
        return result;
    }
    if (label.isEmpty())
        return result;

    // ── the first character must start the label or a segment of it
    const QChar first = pattern.at(0).toLower();
    int         anchor = -1;
    for (int i = 0; i < label.size(); ++i)
    {
        if (label.at(i).toLower() == first && startsSegment(label, i))
        {
            anchor = i;
            break;
        }
    }
    if (anchor < 0)
        return result;

    // ── the rest must appear in order after it
    QList<int> matched;
    matched << anchor;
    int at = anchor + 1;
    for (int p = 1; p < pattern.size(); ++p)
    {
        const QChar want = pattern.at(p).toLower();
        int         found = -1;
        for (int i = at; i < label.size(); ++i)
        {
            if (label.at(i).toLower() == want)
            {
                found = i;
                break;
            }
        }
        if (found < 0)
            return result; // no match at all
        matched << found;
        at = found + 1;
    }

    // ── ranking, highest first. The bands are far enough apart that a lower
    //    band can never overtake a higher one on run length alone.
    int points = 1000;
    if (label.startsWith(pattern, Qt::CaseSensitive))
        points += 8000;
    else if (label.startsWith(pattern, Qt::CaseInsensitive))
        points += 6000;
    else if (anchor > 0)
        points += 2000; // started a segment rather than the label

    int runs = 0;
    for (int i = 1; i < matched.size(); ++i)
        if (matched.at(i) == matched.at(i - 1) + 1)
            ++runs;
    points += runs * 50;

    // Shorter labels first within a band; 200 is below the 2000 band gap.
    points += std::max(0, 200 - int(label.size()));

    result.index   = -1;
    result.score   = points;
    result.matched = matched;
    return result;
}

QList<Match> filter(const QList<Item> &items, const QString &pattern)
{
    QList<Match> out;
    out.reserve(items.size());
    for (int i = 0; i < items.size(); ++i)
    {
        Match m = score(items.at(i).label, pattern);
        if (m.score <= 0)
            continue;
        m.index = i;
        out << m;
    }
    std::stable_sort(out.begin(), out.end(),
                     [&items](const Match &a, const Match &b)
                     {
                         if (a.score != b.score)
                             return a.score > b.score;
                         const QString &la = items.at(a.index).label;
                         const QString &lb = items.at(b.index).label;
                         if (la.size() != lb.size())
                             return la.size() < lb.size();
                         return la.compare(lb, Qt::CaseInsensitive) < 0;
                     });
    return out;
}

WordRange wordAt(const QString &text, int offset)
{
    WordRange range;
    const int clamped = std::clamp(offset, 0, int(text.size()));
    int       start = clamped;
    while (start > 0 && isWordChar(text.at(start - 1)))
        --start;
    int end = clamped;
    while (end < text.size() && isWordChar(text.at(end)))
        ++end;
    range.start = start;
    range.caret = clamped;
    range.end   = end;
    return range;
}

} // namespace Completion
} // namespace Kites
