#include "editor_commands.h"

#include "ui/common/kites_editor.h"
#include "ui/editor_tab/find_engine.h"

#include <algorithm>

namespace Kites
{
namespace EditorCommands
{
const QString indentUnit = QStringLiteral("\t");

int indentWidth()
{
    return editorTabWidth;
}

int LineMap::mapped(int line) const
{
    if (lastLine < firstLine || line < firstLine)
        return line;
    if (line > lastLine)
        return line + shiftBelow;
    return movedTo.value(line - firstLine, -1);
}

namespace
{
/// The document as lines, without the separators. Joining with '\n' gives
/// the original back, trailing newline or not.
QStringList split(const QString &text)
{
    return text.split(QLatin1Char('\n'));
}

bool isBlank(const QString &line)
{
    for (QChar c : line)
        if (!c.isSpace())
            return false;
    return true;
}

/// Column of the first non-whitespace character, or the line's length when
/// there is none.
int firstNonSpace(const QString &line)
{
    int i = 0;
    while (i < line.size() && line.at(i).isSpace())
        ++i;
    return i;
}

/**
 * @brief A change to one line: at `column`, drop `removed` characters and
 *        put `inserted` in their place.
 *
 * Indent, outdent and comment toggling are all of this shape, and so is the
 * arithmetic that keeps the selection on the same text afterwards.
 */
struct Splice
{
    int     line;
    int     column;
    int     removed;
    QString inserted;
};

/// Where `offset` ends up once every splice has been applied.
int mapOffset(const QString &text, int offset, const QList<Splice> &splices)
{
    const int line   = lineOf(text, offset);
    const int column = offset - lineStart(text, line);
    int       moved  = offset;
    for (const Splice &splice : splices)
    {
        if (splice.line < line)
        {
            moved += splice.inserted.size() - splice.removed;
        }
        else if (splice.line == line && splice.removed == 0)
        {
            // A pure insertion. An offset exactly AT the column stays put, so
            // that indenting a selection that starts at column 0 leaves it
            // starting at column 0 - covering the tab it just gained rather
            // than sitting after it.
            if (column > splice.column)
                moved += splice.inserted.size();
        }
        else if (splice.line == line)
        {
            if (column >= splice.column + splice.removed)
                moved += splice.inserted.size() - splice.removed;
            else if (column > splice.column)
                // Inside the removed run: the caret collapses onto its start.
                moved -= column - splice.column;
        }
    }
    return moved;
}

/// Every old line in [first, last] stays where it is; nothing below moves.
LineMap identityMap(int first, int last)
{
    LineMap map;
    map.firstLine = first;
    map.lastLine  = last;
    for (int line = first; line <= last; ++line)
        map.movedTo << line;
    return map;
}

/**
 * @brief The map for a pure INSERTION at `at`.
 *
 * One rule covers Enter, Ctrl+Enter, Ctrl+Shift+Enter and a line paste: the
 * line the insertion lands in keeps its breakpoint unless the insertion is
 * at its very first character, in which case the whole line - text and
 * breakpoint together - is pushed down. Everything below moves by the number
 * of newlines inserted.
 */
LineMap insertionMap(const QString &text, int at, const QString &inserted)
{
    const int added = inserted.count(QLatin1Char('\n'));
    const int line  = lineOf(text, at);

    LineMap map;
    map.firstLine  = line;
    map.lastLine   = line;
    map.movedTo << (at == lineStart(text, line) ? line + added : line);
    map.shiftBelow = added;
    return map;
}

/// Builds the Edit that applies `splices`, replacing only the lines they
/// touch. An empty list means the command does not apply.
Edit spliced(const QString &text, int anchor, int position, const QList<Splice> &splices)
{
    if (splices.isEmpty())
        return {};

    int firstLine = splices.first().line;
    int lastLine  = splices.first().line;
    for (const Splice &splice : splices)
    {
        firstLine = std::min(firstLine, splice.line);
        lastLine  = std::max(lastLine, splice.line);
    }

    const QStringList lines = split(text);
    QStringList       rebuilt;
    for (int i = firstLine; i <= lastLine; ++i)
    {
        QString line = lines.at(i);
        for (const Splice &splice : splices)
            if (splice.line == i)
                line.replace(splice.column, splice.removed, splice.inserted);
        rebuilt << line;
    }

    Edit edit;
    edit.valid    = true;
    edit.start    = lineStart(text, firstLine);
    edit.length   = lineEnd(text, lastLine) - edit.start;
    edit.text     = rebuilt.join(QLatin1Char('\n'));
    edit.anchor   = mapOffset(text, anchor, splices);
    edit.position = mapOffset(text, position, splices);
    // Indent, outdent and the comment toggle rewrite the same lines in
    // place: no line is created, removed or moved.
    edit.lines    = identityMap(firstLine, lastLine);
    return edit;
}

/// Replaces the whole run of lines [first, last] with `replacement` (which
/// carries its own separators but no trailing newline).
Edit wholeLines(const QString &text, int first, int last, const QString &replacement,
                int anchor, int position)
{
    Edit edit;
    edit.valid    = true;
    edit.start    = lineStart(text, first);
    edit.length   = lineEnd(text, last) - edit.start;
    edit.text     = replacement;
    edit.anchor   = anchor;
    edit.position = position;
    return edit;
}
} // namespace

int lineCount(const QString &text)
{
    return text.count(QLatin1Char('\n')) + 1;
}

int lineOf(const QString &text, int offset)
{
    const int clamped = std::clamp(offset, 0, int(text.size()));
    return QStringView(text).left(clamped).count(QLatin1Char('\n'));
}

int lineStart(const QString &text, int line)
{
    if (line <= 0)
        return 0;
    int offset = 0;
    for (int seen = 0; seen < line; ++seen)
    {
        const int next = text.indexOf(QLatin1Char('\n'), offset);
        if (next < 0)
            return text.size();
        offset = next + 1;
    }
    return offset;
}

int lineEnd(const QString &text, int line)
{
    const int start = lineStart(text, line);
    const int next  = text.indexOf(QLatin1Char('\n'), start);
    return next < 0 ? int(text.size()) : next;
}

QString indentOf(const QString &text, int line)
{
    const int start = lineStart(text, line);
    const int end   = lineEnd(text, line);
    return text.mid(start, firstNonSpace(text.mid(start, end - start)));
}

LineRange linesTouched(const QString &text, int anchor, int position)
{
    const int low  = std::min(anchor, position);
    const int high = std::max(anchor, position);

    LineRange range;
    range.first = lineOf(text, low);
    range.last  = lineOf(text, high);
    // A selection that stops at column 0 of a line has not reached into it.
    if (range.last > range.first && high == lineStart(text, range.last))
        --range.last;
    return range;
}

Edit smartHome(const QString &text, int anchor, int position, bool extend)
{
    const int line   = lineOf(text, position);
    const int start  = lineStart(text, line);
    const int end    = lineEnd(text, line);
    const int indent = start + firstNonSpace(text.mid(start, end - start));

    // At the text, or still inside the indentation: go all the way to the
    // left. Anywhere else: stop at the text first.
    const int target = (position <= indent) ? start : indent;

    Edit edit;
    edit.valid    = true;
    edit.start    = target;
    edit.length   = 0;
    edit.anchor   = extend ? anchor : target;
    edit.position = target;
    return edit;
}

LineClip lineClip(const QString &text, int anchor, int position)
{
    const LineRange range = linesTouched(text, anchor, position);
    const int       start = lineStart(text, range.first);
    const int       end   = lineEnd(text, range.last);
    const bool      hasNewline = end < text.size();

    LineClip clip;
    clip.start     = start;
    clip.length    = hasNewline ? end + 1 - start : end - start;
    clip.clipboard = text.mid(start, end - start) + QLatin1Char('\n');
    return clip;
}

Edit deleteLines(const QString &text, int anchor, int position)
{
    const LineRange range  = linesTouched(text, anchor, position);
    const int       column = position - lineStart(text, lineOf(text, position));

    Edit edit;
    edit.valid = true;
    if (range.last + 1 < lineCount(text))
    {
        // Lines follow: take them and their newlines, and land on the same
        // column of what is now the first line below.
        edit.start  = lineStart(text, range.first);
        edit.length = lineStart(text, range.last + 1) - edit.start;
        const int followingLength =
            lineEnd(text, range.last + 1) - lineStart(text, range.last + 1);
        edit.position = edit.start + std::min(column, followingLength);
    }
    else if (range.first == 0)
    {
        // The whole document.
        edit.start    = 0;
        edit.length   = text.size();
        edit.position = 0;
    }
    else
    {
        // The last lines: the newline that joined them to the line above
        // goes too, and the caret lands at the end of that line.
        edit.start    = lineEnd(text, range.first - 1);
        edit.length   = text.size() - edit.start;
        edit.position = edit.start;
    }
    edit.anchor = edit.position;

    // Every touched line is gone; everything below moves up by as many.
    edit.lines.firstLine  = range.first;
    edit.lines.lastLine   = range.last;
    for (int line = range.first; line <= range.last; ++line)
        edit.lines.movedTo << -1;
    edit.lines.shiftBelow = -(range.last - range.first + 1);
    return edit;
}

Edit pasteLineAbove(const QString &text, int anchor, int position, const QString &line)
{
    QString inserted = line;
    if (!inserted.endsWith(QLatin1Char('\n')))
        inserted += QLatin1Char('\n');

    const int at = lineStart(text, lineOf(text, std::min(anchor, position)));

    Edit edit;
    edit.valid  = true;
    edit.start  = at;
    edit.length = 0;
    edit.text   = inserted;
    // The caret stays on its own line, which the insertion has pushed down.
    edit.anchor   = anchor + inserted.size();
    edit.position = position + inserted.size();
    edit.lines    = insertionMap(text, at, inserted);
    return edit;
}

Edit moveLines(const QString &text, int anchor, int position, int delta)
{
    if (delta != 1 && delta != -1)
        return {};
    const LineRange range = linesTouched(text, anchor, position);
    if (delta < 0 && range.first == 0)
        return {};
    if (delta > 0 && range.last + 1 >= lineCount(text))
        return {};

    const QStringList lines = split(text);
    const int         first = delta < 0 ? range.first - 1 : range.first;
    const int         last  = delta < 0 ? range.last : range.last + 1;

    QStringList block;
    for (int i = range.first; i <= range.last; ++i)
        block << lines.at(i);

    QStringList rebuilt;
    int         shift = 0;
    if (delta < 0)
    {
        rebuilt = block;
        rebuilt << lines.at(range.first - 1);
        shift = -(lines.at(range.first - 1).size() + 1);
    }
    else
    {
        rebuilt << lines.at(range.last + 1);
        rebuilt += block;
        shift = lines.at(range.last + 1).size() + 1;
    }

    Edit edit = wholeLines(text, first, last, rebuilt.join(QLatin1Char('\n')), anchor + shift,
                           position + shift);

    // The block and the line it jumped over swap places, and they carry
    // their breakpoints with them. Nothing outside the pair moves.
    edit.lines.firstLine = first;
    edit.lines.lastLine  = last;
    if (delta < 0)
    {
        // The line above goes to the bottom of the block.
        edit.lines.movedTo << last;
        for (int line = range.first; line <= range.last; ++line)
            edit.lines.movedTo << line - 1;
    }
    else
    {
        // The line below comes to the top of the block.
        for (int line = range.first; line <= range.last; ++line)
            edit.lines.movedTo << line + 1;
        edit.lines.movedTo << first;
    }
    return edit;
}

Edit duplicateLines(const QString &text, int anchor, int position, bool above)
{
    const LineRange   range = linesTouched(text, anchor, position);
    const QStringList lines = split(text);

    QStringList block;
    for (int i = range.first; i <= range.last; ++i)
        block << lines.at(i);
    const QString joined = block.join(QLatin1Char('\n'));

    // Identical text either way - two copies where there was one. Only the
    // selection tells the two commands apart.
    const int shift = above ? 0 : joined.size() + 1;
    Edit      edit  = wholeLines(text, range.first, range.last,
                                 joined + QLatin1Char('\n') + joined, anchor + shift,
                                 position + shift);

    // The ORIGINAL keeps its breakpoint and the copy gets none. Copying
    // downwards leaves the original where it was; copying upwards puts the
    // copy first, so the original - and its breakpoint - moves down.
    const int count      = range.last - range.first + 1;
    edit.lines.firstLine = range.first;
    edit.lines.lastLine  = range.last;
    for (int line = range.first; line <= range.last; ++line)
        edit.lines.movedTo << (above ? line + count : line);
    edit.lines.shiftBelow = count;
    return edit;
}

Edit insertLine(const QString &text, int anchor, int position, bool above)
{
    const int     line   = lineOf(text, position);
    const QString indent = indentOf(text, line);

    Edit edit;
    edit.valid  = true;
    edit.length = 0;
    if (above)
    {
        edit.start    = lineStart(text, line);
        edit.text     = indent + QLatin1Char('\n');
        edit.position = edit.start + indent.size();
    }
    else
    {
        edit.start    = lineEnd(text, line);
        edit.text     = QLatin1Char('\n') + indent;
        edit.position = edit.start + 1 + indent.size();
    }
    edit.anchor = edit.position;
    // Said explicitly rather than through insertionMap: on an EMPTY line the
    // end of the line and the start of it are the same offset, so the
    // generic "inserting at the first character pushes the line down" rule
    // cannot tell "below" from "above" there.
    edit.lines.firstLine  = line;
    edit.lines.lastLine   = line;
    edit.lines.movedTo << (above ? line + 1 : line);
    edit.lines.shiftBelow = 1;
    (void)anchor;
    return edit;
}

Edit selectLine(const QString &text, int anchor, int position)
{
    const int low  = std::min(anchor, position);
    const int high = std::max(anchor, position);

    const int firstLine = lineOf(text, low);
    // Taking the line OF the high end and then the start of the next one
    // means a selection that already ends at a line start naturally grows by
    // exactly one more line, with no special case for "again".
    const int nextLine = lineOf(text, high) + 1;

    Edit edit;
    edit.valid    = true;
    edit.start    = lineStart(text, firstLine);
    edit.length   = 0;
    edit.anchor   = lineStart(text, firstLine);
    edit.position = nextLine < lineCount(text) ? lineStart(text, nextLine) : int(text.size());
    return edit;
}

Edit newlineWithIndent(const QString &text, int anchor, int position)
{
    const int low  = std::min(anchor, position);
    const int high = std::max(anchor, position);

    const int line   = lineOf(text, low);
    const int start  = lineStart(text, line);
    QString   indent = indentOf(text, line);
    // Pressing Enter inside the indentation must not deepen it: copy only
    // what is to the LEFT of the caret.
    indent.truncate(std::min(indent.size(), qsizetype(low - start)));

    Edit edit;
    edit.valid    = true;
    edit.start    = low;
    edit.length   = high - low;
    edit.text     = QLatin1Char('\n') + indent;
    edit.anchor   = low + 1 + indent.size();
    edit.position = edit.anchor;

    if (high == low)
    {
        // The ordinary case: one newline goes in and nothing is removed.
        edit.lines = insertionMap(text, low, edit.text);
    }
    else
    {
        // Enter over a selection. The first line keeps its head and its
        // breakpoint; the last line's tail becomes the new second line and
        // takes its breakpoint with it; anything wholly inside is gone.
        const int lastLine   = lineOf(text, high);
        edit.lines.firstLine = line;
        edit.lines.lastLine  = lastLine;
        for (int at = line; at <= lastLine; ++at)
            edit.lines.movedTo << (at == line ? line : (at == lastLine ? line + 1 : -1));
        edit.lines.shiftBelow = 1 - (lastLine - line);
    }
    return edit;
}

Edit indentLines(const QString &text, int anchor, int position)
{
    const LineRange range = linesTouched(text, anchor, position);
    QList<Splice>   splices;
    for (int line = range.first; line <= range.last; ++line)
        splices << Splice{line, 0, 0, indentUnit};
    return spliced(text, anchor, position, splices);
}

Edit outdentLines(const QString &text, int anchor, int position)
{
    const LineRange   range = linesTouched(text, anchor, position);
    const QStringList lines = split(text);

    QList<Splice> splices;
    for (int line = range.first; line <= range.last; ++line)
    {
        const QString &content = lines.at(line);
        int            remove  = 0;
        if (content.startsWith(QLatin1Char('\t')))
        {
            remove = 1;
        }
        else
        {
            while (remove < indentWidth() && remove < content.size() &&
                   content.at(remove) == QLatin1Char(' '))
                ++remove;
        }
        if (remove > 0)
            splices << Splice{line, 0, remove, QString()};
    }
    // Every touched line already starts at column 0: nothing to do, and
    // nothing should be pushed onto the undo stack.
    return spliced(text, anchor, position, splices);
}

bool isPairOpener(QChar c)
{
    return c == QLatin1Char('(') || c == QLatin1Char('"') || c == QLatin1Char('\'');
}

QChar closerFor(QChar opener)
{
    if (opener == QLatin1Char('('))
        return QLatin1Char(')');
    return opener; // a quote closes itself
}

LineContext contextAt(const QString &text, int offset)
{
    LineContext context;
    const int   start = lineStart(text, lineOf(text, offset));
    for (int i = start; i < offset && i < text.size(); ++i)
    {
        const QChar c = text.at(i);
        if (context.inString)
        {
            // A backslash escapes the next character, so `.asciz "a\"b"` is
            // one string and not two.
            if (c == QLatin1Char('\\'))
                ++i;
            else if (c == context.stringQuote)
                context.inString = false;
            continue;
        }
        if (c == QLatin1Char('#'))
        {
            context.inComment = true;
            break; // a comment runs to the end of the line
        }
        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
        {
            context.inString   = true;
            context.stringQuote = c;
        }
    }
    return context;
}

Edit autoPair(const QString &text, int anchor, int position, const QString &typed)
{
    if (typed.size() != 1)
        return {};
    const QChar c = typed.at(0);

    const int low  = std::min(anchor, position);
    const int high = std::max(anchor, position);

    // ── a non-empty selection: an opener wraps it and keeps it selected
    if (high > low && isPairOpener(c))
    {
        const QChar closer = closerFor(c);
        Edit        edit;
        edit.valid  = true;
        edit.start  = low;
        edit.length = high - low;
        edit.text   = c + text.mid(low, high - low) + closer;
        edit.anchor   = low + 1;
        edit.position = high + 1;
        // Wrapping a selection that spans lines rewrites those lines, so the
        // breakpoints need the same map every other command provides. No
        // line is created or destroyed: the quotes go inside the span.
        const int first = lineOf(text, low);
        const int last  = lineOf(text, high);
        edit.lines.firstLine = first;
        edit.lines.lastLine  = last;
        for (int line = first; line <= last; ++line)
            edit.lines.movedTo << line;
        return edit;
    }
    if (high > low)
        return {}; // a closer or anything else with a selection: ordinary

    const QChar next = low < text.size() ? text.at(low) : QChar();
    const QChar previous = low > 0 ? text.at(low - 1) : QChar();
    const LineContext context = contextAt(text, low);

    // ── typing a closer over the one that is already there
    const bool closesSomething = c == QLatin1Char(')') || c == QLatin1Char('"') ||
                                 c == QLatin1Char('\'');
    if (closesSomething && next == c)
    {
        Edit edit;
        edit.valid    = true;
        edit.start    = low + 1; // cursor only: step over it
        edit.length   = 0;
        edit.anchor   = low + 1;
        edit.position = low + 1;
        return edit;
    }

    if (!isPairOpener(c))
        return {};

    // ── when an opener is allowed to close itself
    const bool nextIsFree = next.isNull() || next == QLatin1Char('\n') || next.isSpace() ||
                            next == QLatin1Char(')');
    if (!nextIsFree)
        return {};
    if (context.inComment)
        return {};

    const bool isQuote = c == QLatin1Char('"') || c == QLatin1Char('\'');
    if (isQuote && FindEngine::isWordCharacter(previous))
        return {}; // don\'t
    if (isQuote && context.inString)
        return {}; // closing the string that is already open
    if (!isQuote && context.inString)
        return {}; // a ( inside "..." is just text

    Edit edit;
    edit.valid    = true;
    edit.start    = low;
    edit.length   = 0;
    edit.text     = QString(c) + closerFor(c);
    edit.anchor   = low + 1;
    edit.position = low + 1;
    return edit;
}

Edit pairBackspace(const QString &text, int anchor, int position)
{
    if (anchor != position || position <= 0 || position >= text.size())
        return {};

    const QChar before = text.at(position - 1);
    const QChar after  = text.at(position);
    if (!isPairOpener(before) || after != closerFor(before))
        return {};

    Edit edit;
    edit.valid    = true;
    edit.start    = position - 1;
    edit.length   = 2;
    edit.anchor   = position - 1;
    edit.position = position - 1;
    return edit;
}

Edit toggleComment(const QString &text, int anchor, int position)
{
    const LineRange   range = linesTouched(text, anchor, position);
    const QStringList lines = split(text);

    QList<int> interesting;
    for (int line = range.first; line <= range.last; ++line)
        if (!isBlank(lines.at(line)))
            interesting << line;
    if (interesting.isEmpty())
        return {};

    bool allCommented = true;
    int  leastIndent  = -1;
    for (int line : interesting)
    {
        const QString &content = lines.at(line);
        const int      column  = firstNonSpace(content);
        if (content.at(column) != QLatin1Char('#'))
            allCommented = false;
        leastIndent = leastIndent < 0 ? column : std::min(leastIndent, column);
    }

    QList<Splice> splices;
    for (int line : interesting)
    {
        const QString &content = lines.at(line);
        if (allCommented)
        {
            const int column = firstNonSpace(content);
            // The '#', and one space after it if the comment was written
            // with one - so a toggle round trip gives the line back exactly.
            const int remove =
                (column + 1 < content.size() && content.at(column + 1) == QLatin1Char(' ')) ? 2
                                                                                            : 1;
            splices << Splice{line, column, remove, QString()};
        }
        else
        {
            splices << Splice{line, leastIndent, 0, QStringLiteral("# ")};
        }
    }
    return spliced(text, anchor, position, splices);
}
} // namespace EditorCommands
} // namespace Kites
