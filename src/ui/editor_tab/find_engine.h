#pragma once
#include <QList>
#include <QString>
#include <QStringList>

namespace Kites
{
/**
 * @brief Finding and replacing, as functions of a string.
 *
 * Nothing here knows about a widget, a document or a cursor. Offsets are
 * plain indices into the text handed in, which are the same numbers a
 * QTextDocument uses for its positions - `document()->toPlainText()`
 * separates blocks with a single '\n' and counts it, so index and position
 * agree. That is asserted rather than assumed, including for tabs, empty
 * lines, multi-byte characters, a surrogate pair, and a document with and
 * without a trailing newline.
 */
namespace FindEngine
{
struct FindOptions
{
    /// Case-INSENSITIVE by default, which is what a search box does.
    bool caseSensitive {false};
    bool wholeWord     {false};
    bool regex         {false};
};

struct FindMatch
{
    int         start  {0};
    int         length {0};
    /// The matched text itself, so `$0` can be expanded without handing the
    /// whole document back to expandReplacement().
    QString     matched;
    /// Capture groups 1..n, in order. Empty outside regex mode.
    QStringList captures;

    int end() const { return start + length; }
};

struct FindResult
{
    QList<FindMatch> matches;
    /// True when the cap was reached and the search stopped early.
    bool             truncated {false};
    /// Non-empty only for a regex that will not compile. The matches are
    /// then empty and nothing has gone wrong beyond the user's pattern.
    QString          error;
};

/// One replacement, as a range of the original text and what goes there.
struct ReplaceEdit
{
    int     start  {0};
    int     length {0};
    QString text;
};

/// The most matches a single search reports. Past this, `truncated` is set
/// and the search stops: a 20,000 line file with a pattern that matches
/// everywhere must not turn into a million selections.
inline constexpr int matchCap = 10000;

/**
 * @brief Every match of `pattern` in `text`, left to right.
 *
 * Plain and regex mode both run through QRegularExpression, so the offsets
 * mean the same thing either way; plain mode simply escapes the pattern
 * first. Regex mode always has MultilineOption, so `^` and `$` are per
 * line, and a pattern may match across lines.
 *
 * An empty pattern finds nothing and is not an error. An invalid regex
 * fills `error` and finds nothing.
 *
 * ZERO-LENGTH matches - `^`, `$`, `x*`, `\b` - are reported as matches of
 * length 0. The scan advances one character past a zero-length match, so it
 * always terminates and never reports the same position twice.
 */
FindResult findAll(const QString &text, const QString &pattern, const FindOptions &options,
                   int cap = matchCap);

/**
 * @brief The match to go to from `position`.
 *
 * Forward: the first match starting at or after `position`. Backward: the
 * last match starting strictly before it. Returns -1 when there is none and
 * `wrap` is false; with `wrap` it comes round to the other end. -1 also
 * when there are no matches at all.
 */
int nextIndex(const QList<FindMatch> &matches, int position, bool forward, bool wrap);

/**
 * @brief The text a match is replaced with.
 *
 * In plain mode the template is literal - `$` means a dollar sign and
 * nothing else. In regex mode `$1`..`$9` are the capture groups, `$0` is
 * the whole match, and `$$` is a literal `$`. A `$` followed by anything
 * else is left alone, so a template is never silently mangled.
 */
QString expandReplacement(const FindMatch &match, const QString &replacement,
                          const FindOptions &options);

/// Every replacement for `result`, in document order. The caller applies
/// them back to front so earlier offsets stay valid.
QList<ReplaceEdit> replaceAllEdits(const FindResult &result, const QString &replacement,
                                   const FindOptions &options);

/**
 * @brief What counts as a word character for "whole word".
 *
 * `[A-Za-z0-9_]`, deliberately - not QRegularExpression's `\b`, which is
 * Unicode-aware and would treat an accented letter as part of a word. A
 * whole-word search wraps the pattern in lookarounds built from this rule,
 * in regex mode too, so "whole word" means one thing everywhere.
 */
bool isWordCharacter(QChar c);
} // namespace FindEngine
} // namespace Kites
