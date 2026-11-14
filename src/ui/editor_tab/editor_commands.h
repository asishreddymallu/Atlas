#pragma once
#include <QList>
#include <QString>

namespace Kites
{
/**
 * @brief The text transformations behind the editor's line commands.
 *
 * Every function here is pure: document text and a selection in, one @ref
 * Edit out. Nothing in this file knows about a QWidget, a QTextCursor or a
 * clipboard, so the rules can be tested as a table of strings and the widget
 * is left with one job - applying the result inside a single edit block, so
 * that one Ctrl+Z undoes one command.
 *
 * Offsets are positions in the document string, the same units a
 * QTextCursor uses. Lines are separated by '\n' and the last line may have
 * no newline after it; every function below is written for that case
 * explicitly rather than assuming a trailing newline.
 */
namespace EditorCommands
{
/**
 * @brief Where every line a command touches ends up.
 *
 * Breakpoints are per line, and the editor's own commands apply themselves
 * as ONE replacement over a range - which is exactly the shape that makes a
 * tracked position useless. Measured: a QTextCursor anchored to a line
 * follows ordinary typing, Enter, Backspace, deletion and paste correctly,
 * but inside a replaced range it collapses, so swapping two lines with one
 * insertText leaves both anchors on the same line.
 *
 * So the commands say where the lines went, as part of the same pure
 * function that says what the text becomes. Lines outside [first, last] are
 * not rewritten: the ones above do not move, the ones below move by
 * `shiftBelow`.
 */
struct LineMap
{
    int        firstLine  {0};
    /// Inclusive. Less than firstLine means the edit rewrites no whole line,
    /// and every line stays where it was.
    int        lastLine   {-1};
    /// One entry per old line firstLine..lastLine: its new line, or -1 when
    /// the line is gone.
    QList<int> movedTo;
    /// Applied to every old line after lastLine.
    int        shiftBelow {0};

    /// Where old `line` (0-based) ends up, or -1 if it no longer exists.
    int mapped(int line) const;
};

/**
 * @brief One command's result: a narrow replacement plus where the cursor
 *        ends up.
 *
 * `start`/`length` name the ONLY part of the document that changes. A line
 * command rewrites the lines it touches and nothing else, so an edit near
 * the end of a long file does not re-lay-out the whole document and does not
 * disturb a selection elsewhere.
 *
 * `valid == false` means the command does not apply here - the first line
 * moved up, a comment toggle on nothing but blank lines - and the caller
 * must leave the document and the cursor exactly as they were.
 */
struct Edit
{
    bool    valid    {false};
    int     start    {0};
    int     length   {0};
    QString text;
    int     anchor   {0};
    int     position {0};
    /// Where the lines this edit rewrites end up. Default is identity.
    LineMap lines;

    /// True when nothing is replaced and only the cursor moves.
    bool isCursorOnly() const { return length == 0 && text.isEmpty(); }
};

/**
 * @brief The run of whole lines a selection touches.
 *
 * A selection that ENDS at column 0 of a line does not include that line -
 * dragging down to the start of line 5 selects lines 1 to 4, and that is
 * what every line command here acts on. Both ends are line numbers, zero
 * based and inclusive.
 */
struct LineRange
{
    int first {0};
    int last  {0};
};

/**
 * @brief Marks a clipboard whose contents are a whole line, not a fragment.
 *
 * Ctrl+C and Ctrl+X with nothing selected take the whole line. Ctrl+V then
 * has to know that, so that it pastes the line ABOVE the caret's line rather
 * than into the middle of it. The marker rides on the QMimeData beside the
 * ordinary text/plain, so pasting into any other application is unaffected
 * and pasting text copied from anywhere else behaves normally.
 */
inline constexpr auto lineCopyMimeType = "application/x-kites-line-copy";

/// One level of indentation, as the editor writes it: a tab character.
/// Plain Tab has always inserted one and this does not change that.
extern const QString indentUnit;

/// How many columns a tab stands for, and therefore how many leading spaces
/// one outdent removes when a line is indented with spaces instead. The same
/// 4 that KitesEditor turns into a tab stop distance, named once.
int indentWidth();

LineRange linesTouched(const QString &text, int anchor, int position);

/// Offset of the first character of `line`, and of the character after its
/// last - the '\n', or the end of the document for the final line.
int lineStart(const QString &text, int line);
int lineEnd(const QString &text, int line);
int lineOf(const QString &text, int offset);
int lineCount(const QString &text);

/// The leading whitespace of `line`, as it is written.
QString indentOf(const QString &text, int line);

/**
 * @brief Home that lands on the text first, and on column 0 second.
 *
 * From anywhere after the indentation the caret goes to the first
 * non-whitespace character. From there, or from inside the indentation, it
 * goes to column 0. A line that is entirely whitespace behaves as if its
 * text started at the end of it.
 */
Edit smartHome(const QString &text, int anchor, int position, bool extend);

/// The whole-line span the clipboard takes when nothing is selected,
/// INCLUDING its newline. On the last line, which has none, the returned
/// `text` still ends in one - a line copy is a line wherever it is pasted.
struct LineClip
{
    QString clipboard;
    int     start  {0};
    int     length {0};
};
LineClip lineClip(const QString &text, int anchor, int position);

/// Removes the lines `lineClip` would take. The caret lands on the same
/// column of the line that follows, clamped to its length; from the last
/// line, at the end of what is left.
Edit deleteLines(const QString &text, int anchor, int position);

/// Pastes a line copy ABOVE the caret's line, leaving the caret on its own
/// line - which is now one line further down.
Edit pasteLineAbove(const QString &text, int anchor, int position, const QString &line);

/// Moves the touched lines one line up (`delta` -1) or down (+1), carrying
/// the selection with them. Does nothing at the first or last line.
Edit moveLines(const QString &text, int anchor, int position, int delta);

/// Copies the touched lines above or below. The text produced is the same
/// either way; what differs is where the selection lands - on the new copy
/// going down, on the same line numbers (now the upper copy) going up.
Edit duplicateLines(const QString &text, int anchor, int position, bool above);

/// An empty line below (or above) the caret's line, carrying that line's
/// indentation, with the caret at the end of it.
Edit insertLine(const QString &text, int anchor, int position, bool above);

/// Selects the caret's line including its newline; from a selection that
/// already ends at a line start, one line more.
Edit selectLine(const QString &text, int anchor, int position);

/// Enter: splits the line and gives the new one the indentation of the old.
/// The copied indentation is capped at the caret's column, so pressing
/// Enter inside the indentation does not deepen it.
Edit newlineWithIndent(const QString &text, int anchor, int position);

/// One indent unit on the front of every touched line, and one off again.
/// Outdent removes a tab, or up to indentWidth() spaces, and leaves a line
/// that has neither alone.
Edit indentLines(const QString &text, int anchor, int position);
Edit outdentLines(const QString &text, int anchor, int position);

/**
 * @brief Where an offset sits, as far as the pair rules care.
 *
 * The syntax highlighter cannot answer this: it has no per-block state and
 * no string rule at all - its only relevant pattern is `#[^\n]*`. So the
 * pair rules carry their own one-line scanner, which is all they need,
 * because a RISC-V assembly string cannot span lines.
 */
struct LineContext
{
    bool  inComment {false};
    bool  inString  {false};
    QChar stringQuote;
};
LineContext contextAt(const QString &text, int offset);

/**
 * @brief The pairs the editor closes for you.
 *
 * `(` `)`, `"` `"` and `'` `'`, and nothing else. `[` `]` and `{` `}` mean
 * nothing in RISC-V assembly - no addressing mode or directive uses them -
 * so closing them would be noise.
 */
bool isPairOpener(QChar c);
QChar closerFor(QChar opener);

/**
 * @brief What typing `typed` should do, or an invalid Edit for "nothing
 *        special - let the editor insert it".
 *
 * Driven by the typed TEXT rather than Qt::Key, so a layout where `(` is
 * not Shift+9 behaves the same.
 *
 * An opener closes itself only when the next character is the end of the
 * line, whitespace, or a closing `)`; never in front of a word character,
 * where the user is far more likely to be editing an existing token. A
 * quote additionally refuses after a word character (so `don't` stays one
 * apostrophe) and inside a `#` comment, and `(` refuses inside a string.
 *
 * Typing a closer when that same closer is the next character types over it
 * instead of adding a second one.
 *
 * With a non-empty selection, an opener WRAPS the selection and leaves it
 * selected.
 */
Edit autoPair(const QString &text, int anchor, int position, const QString &typed);

/// Backspace between the two halves of an empty pair removes both.
/// Invalid when the caret is not between one, so the editor does its own.
Edit pairBackspace(const QString &text, int anchor, int position);

/**
 * @brief The bracket next to `position`, and its partner.
 *
 * `offset` is the bracket the caret is beside - the one just after it, or
 * failing that the one just before - and `partner` is its match, or -1.
 * Both are -1 when the caret is not beside a bracket at all, which is the
 * common case and costs one character comparison.
 *
 * SAME LINE ONLY. A RISC-V assembly parenthesis is an addressing mode -
 * `4(sp)` - and never spans lines; searching the whole document would be
 * work done on every cursor move to find something that cannot be there.
 *
 * Brackets inside a `#` comment or a string are ignored, on both ends: a
 * `(` in a comment has no partner, and a real `(` is not matched to one.
 */
struct BracketPair
{
    int offset  {-1};
    int partner {-1};
};
BracketPair bracketAt(const QString &text, int position);

/**
 * @brief Toggles a '#' comment on the touched lines.
 *
 * Blank lines are skipped, and do not count towards the decision. If every
 * non-blank touched line is already commented, the '#' comes off - with one
 * following space if there is one. Otherwise "# " goes on at the column of
 * the LEAST indented non-blank touched line, so a commented block keeps its
 * shape instead of having each line's marker at its own depth.
 */
Edit toggleComment(const QString &text, int anchor, int position);
} // namespace EditorCommands
} // namespace Kites
