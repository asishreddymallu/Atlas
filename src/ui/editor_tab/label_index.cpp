#include "ui/editor_tab/label_index.h"

#include <QSet>
#include <algorithm>

namespace Kites
{
namespace LabelIndex
{
namespace
{
/// One token found on a line, with where it sits.
struct Token
{
    QString name;
    int     column{0};
    /// True when a ':' follows it immediately - a definition.
    bool    definesLabel{false};
    /// True when it sits in operand position: straight after the first
    /// token of the line, or straight after a comma.
    bool    inOperandPosition{false};
};

/**
 * @brief Splits one line into the tokens that could be label names.
 *
 * Stops at '#'. Skips over "..." and '...' entirely. Everything else is
 * chopped on characters that cannot be in a name, and each piece records
 * whether a ':' follows it and whether it stands where an operand stands.
 */
QList<Token> tokenise(const QString &line)
{
    QList<Token> tokens;
    bool         inString = false;
    QChar        quote;
    int          start = -1;
    // Operand position: after the opcode (the line's first bare token, or
    // the one after a label definition) or after a comma.
    bool afterCommaOrOpcode = false;
    int  bareTokens         = 0;

    auto flush = [&](int end, QChar following)
    {
        if (start < 0)
            return;
        Token token;
        token.name              = line.mid(start, end - start);
        token.column            = start;
        token.definesLabel      = following == QLatin1Char(':');
        token.inOperandPosition = afterCommaOrOpcode;
        tokens << token;
        if (!token.definesLabel)
        {
            ++bareTokens;
            // The first bare token on the line is the opcode; what follows
            // it is an operand.
            afterCommaOrOpcode = true;
        }
        start = -1;
    };

    for (int i = 0; i < line.size(); ++i)
    {
        const QChar c = line.at(i);
        if (inString)
        {
            if (c == quote)
                inString = false;
            continue;
        }
        if (c == QLatin1Char('#'))
        {
            flush(i, QChar());
            break;
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
        {
            flush(i, QChar());
            inString = true;
            quote    = c;
            continue;
        }
        if (isLabelChar(c))
        {
            if (start < 0)
                start = i;
            continue;
        }
        flush(i, c);
        if (c == QLatin1Char(','))
            afterCommaOrOpcode = true;
    }
    flush(line.size(), QChar());
    Q_UNUSED(bareTokens);
    return tokens;
}
} // namespace

bool isLabelChar(QChar c)
{
    // The assembler's set for a LABEL, which is narrower than its identifier
    // scan: a name containing '.' is explicitly rejected as a label, and '$'
    // never enters the scan at all.
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
           (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('_');
}

QList<Definition> Index::definitionsOf(const QString &name) const
{
    QList<Definition> found;
    for (const Definition &definition : definitions)
        if (definition.name == name)
            found << definition;
    return found;
}

bool Index::isDefined(const QString &name) const
{
    for (const Definition &definition : definitions)
        if (definition.name == name)
            return true;
    return false;
}

QStringList Index::duplicates() const
{
    QHash<QString, int> counts;
    for (const Definition &definition : definitions)
        ++counts[definition.name];
    QStringList repeated;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        if (it.value() > 1)
            repeated << it.key();
    repeated.sort();
    return repeated;
}

Index findLabels(const QString &text)
{
    Index index;

    // ── one pass to collect every token, with its line and offset
    struct Found
    {
        Token token;
        int   line;
        int   lineStart;
    };
    QList<Found> all;

    int line      = 0;
    int lineStart = 0;
    for (int i = 0; i <= text.size(); ++i)
    {
        const bool atEnd = i == text.size();
        if (!atEnd && text.at(i) != QLatin1Char('\n'))
            continue;
        const QString body = text.mid(lineStart, i - lineStart);
        for (const Token &token : tokenise(body))
            all << Found{token, line, lineStart};
        if (atEnd)
            break;
        ++line;
        lineStart = i + 1;
    }

    // ── definitions first, so references can be filtered against them
    QSet<QString> defined;
    for (const Found &found : all)
    {
        if (!found.token.definesLabel)
            continue;
        index.definitions << Definition{found.token.name, found.line, found.token.column,
                                        found.lineStart + found.token.column};
        defined.insert(found.token.name);
    }

    // ── references: an operand whose text names something that IS defined.
    //    Filtering on "is defined" is what keeps ordinary operands - a
    //    register, an immediate, a mnemonic - out of the list, and is why
    //    this index cannot find a MISSPELLED reference. See the docs.
    for (const Found &found : all)
    {
        if (found.token.definesLabel || !found.token.inOperandPosition)
            continue;
        if (!defined.contains(found.token.name))
            continue;
        index.references << Reference{found.token.name, found.line, found.token.column,
                                      found.lineStart + found.token.column};
    }
    return index;
}

QStringList definitionsOnLine(const QString &line)
{
    QStringList names;
    for (const Token &token : tokenise(line))
        if (token.definesLabel)
            names << token.name;
    return names;
}

QString nameAt(const QString &text, int offset)
{
    const int clamped = std::clamp(offset, 0, int(text.size()));
    int       start   = clamped;
    while (start > 0 && isLabelChar(text.at(start - 1)))
        --start;
    int end = clamped;
    while (end < text.size() && isLabelChar(text.at(end)))
        ++end;
    return text.mid(start, end - start);
}

} // namespace LabelIndex
} // namespace Kites
