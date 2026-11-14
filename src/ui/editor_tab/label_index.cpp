#include "ui/editor_tab/label_index.h"

#include <QSet>
#include <QStringView>
#include <algorithm>

namespace Kites
{
namespace LabelIndex
{
namespace
{
/// One token found on a line, with where it sits.
/**
 * @brief One token's POSITION in the line, not a copy of its text.
 *
 * Deliberately no QString. documentLabels() rescans every line of the
 * document when the label cache is dirty, and a version that built a
 * QString for every token on every line cost 78 ms on an 877,000
 * character file - measured. Callers slice the line only for the tokens
 * they actually keep, which is almost none of them.
 */
struct Token
{
    int  start{0};
    int  length{0};
    /// True when a ':' follows it immediately - a definition.
    bool definesLabel{false};
    /// True when it sits in operand position: straight after the first
    /// token of the line, or straight after a comma.
    bool inOperandPosition{false};

    QStringView text(const QString &line) const
    {
        return QStringView(line).mid(start, length);
    }
};

/**
 * @brief Splits one line into the tokens that could be label names.
 *
 * Stops at '#'. Skips over "..." and '...' entirely. Everything else is
 * chopped on characters that cannot be in a name, and each piece records
 * whether a ':' follows it and whether it stands where an operand stands.
 */
/// True for a character Lexer::getNextToken() has a case for. Everything
/// else sends it to skipLine(), which discards the rest of the line.
bool isLexerCharacter(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char(',') ||
           c == QLatin1Char('"') || c == QLatin1Char('(') || c == QLatin1Char(')') ||
           c == QLatin1Char('.') || c == QLatin1Char('#') || c == QLatin1Char(';') ||
           c == QLatin1Char('-') || c.isSpace() ||
           // ':' never reaches the dispatch: identifier() consumes it as
           // part of the label it has just read. It is listed here because
           // this predicate is asked about every character of the line.
           c == QLatin1Char(':');
}

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
        token.start             = start;
        token.length            = end - start;
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
        // '#' AND ';'. The lexer treats both as starting a comment -
        // lexer.cpp getNextToken(): `current_char == '#' || current_char
        // == ';'` - and the index knew only about '#', so everything after
        // a ';' was scanned as code. A probe over every reference form the
        // assembler accepts found this; it was the one disagreement.
        if (c == QLatin1Char('#') || c == QLatin1Char(';'))
        {
            flush(i, QChar());
            break;
        }
        // Only a DOUBLE quote opens a string. The lexer has no case for
        // '\'' in getNextToken() at all.
        if (c == QLatin1Char('"'))
        {
            flush(i, QChar());
            inString = true;
            quote    = c;
            continue;
        }
        // Anything the lexer does not dispatch on makes it skipLine() and
        // return INVALID - it abandons the REST OF THE LINE, it does not
        // carry on past the character. `'`, `%`, `@`, `+`, `[` all land
        // here, so `    ' j loop` holds no reference as far as the
        // assembler is concerned, and the index must say the same.
        if (!isLexerCharacter(c))
        {
            flush(i, QChar());
            break;
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
        Token   token;
        int     line;
        int     lineStart;
        QString body; ///< the line, so the token can be sliced later
        QString text() const { return token.text(body).toString(); }
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
            all << Found{token, line, lineStart, body};
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
        const QString name = found.text();
        index.definitions << Definition{name, found.line, found.token.start,
                                        found.lineStart + found.token.start};
        defined.insert(name);
    }

    // ── references: an operand whose text names something that IS defined.
    //    Filtering on "is defined" is what keeps ordinary operands - a
    //    register, an immediate, a mnemonic - out of the list, and is why
    //    this index cannot find a MISSPELLED reference. See the docs.
    for (const Found &found : all)
    {
        if (found.token.definesLabel || !found.token.inOperandPosition)
            continue;
        const QString name = found.text();
        if (!defined.contains(name))
            continue;
        index.references << Reference{name, found.line, found.token.start,
                                      found.lineStart + found.token.start};
    }
    return index;
}

QStringList definitionsOnLine(const QString &line)
{
    QStringList names;
    for (const Token &token : tokenise(line))
        if (token.definesLabel)
            names << token.text(line).toString(); // only the ones we keep
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
