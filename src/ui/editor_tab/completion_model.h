#pragma once

#include <QList>
#include <QString>
#include <functional>

class QTextDocument;

namespace Kites
{
/**
 * @brief The completion list's data and its matching rule, as pure functions.
 *
 * Nothing here knows about a widget, a cursor or a document. The popup draws
 * what this produces and the editor decides when to ask for it, which is what
 * lets the whole matching rule be table-tested without a window.
 */
namespace Completion
{

/// What an item is, which decides its badge. Derived from the symbol's shape
/// rather than from the completion source, because the source hands back bare
/// strings with no kind - see known_issues on what instructions.json holds.
enum class Kind
{
    Instruction,
    Register,
    Directive,
    Label
};

struct Item
{
    QString label;
    Kind    kind{Kind::Instruction};
    /// Right-aligned dim text. Empty when nothing is known, which is the
    /// usual case: the instruction data has no format or documentation.
    QString detail;
};

/**
 * @brief One scored candidate.
 *
 * `matched` holds the indices in `label` that the typed characters landed on,
 * so the popup can draw exactly those in the accent colour. It is empty when
 * `score` is 0.
 */
struct Match
{
    int        index{-1}; ///< into the input list
    int        score{0};  ///< 0 means no match
    QList<int> matched;
};

/// True for the characters that make up a symbol here: letters, digits, '_',
/// '.' and '$'. '.' and '$' are in because RISC-V has `fadd.s` and labels may
/// carry either.
bool isWordChar(QChar c);

/**
 * @brief VS Code-style fuzzy match of `pattern` against `label`.
 *
 * The first character of the pattern must land on the start of the label or on
 * the start of a segment after '.', '_' or '$'. The rest must appear in order.
 * Case-insensitive throughout; case is used for ranking, not for matching.
 *
 * Returns a zero score and no indices when it does not match.
 */
Match score(const QString &label, const QString &pattern);

/**
 * @brief Scores every item and returns the matches, best first.
 *
 * Ranking, in order: an exact-case prefix, then a case-insensitive prefix,
 * then a match that starts a segment, then longer consecutive runs, then the
 * shorter label, then alphabetical. An empty pattern matches everything with
 * equal score, so the order is alphabetical - which is what Ctrl+Space wants.
 */
QList<Match> filter(const QList<Item> &items, const QString &pattern);

/// Classifies a bare symbol. Directives start with '.', registers are the
/// names and aliases the assembler knows, everything else is an instruction
/// unless the caller says it came from the document.
Kind kindOf(const QString &symbol);

/**
 * @brief Where a completion would be inserted.
 *
 * `start` is the first character of the word the caret is in, `caret` is the
 * caret, and `end` is the last character of that word. Insert mode replaces
 * [start, caret); replace mode replaces [start, end).
 */
struct WordRange
{
    int start{0};
    int caret{0};
    int end{0};
    QString prefix() const { return {}; } // see wordAt(): the text is the caller's
};

/// The word around `offset` in `text`, by the character set above.
WordRange wordAt(const QString &text, int offset);

/**
 * @brief Where the items come from.
 *
 * An injected function rather than a direct call to LanguageService, so a test
 * can hand in a fake list AND count how often it was asked. The counter is what
 * proves a caret move never queries at all, which a "popup is not visible"
 * assertion alone cannot.
 *
 * LanguageService itself is untouched by this work.
 */
using Source = std::function<QList<Item>(const QString &prefix)>;

} // namespace Completion
} // namespace Kites
