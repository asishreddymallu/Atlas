#include "main_editor.h"
#include "completion_popup.h"
#include "find_bar.h"
#include "goto_line_bar.h"
#include "ui/theme/theme_manager.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QTextDocument>
#include <algorithm>
#include "language_service/language_service.h"
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStringListModel>
#include <QTextBlock>
#include <QToolTip>
#include <set>
#include "ui/common/line_number_gutter_column.h"
#include "ui/theme/font_manager.h"
#include "ui/theme/theme_manager.h"
#include "break_point_gutter_column.h"
namespace Kites
{
namespace
{
/**
 * @brief One line's label definitions, kept on the block itself.
 *
 * Block user data moves with the block: splitting a line gives the new
 * block no data (so it is rescanned), and deleting a line takes its labels
 * with it. Nothing else in this project uses the slot - the syntax
 * highlighter keeps its state in setUserState(), which is a separate int.
 */
class BlockLabels : public QTextBlockUserData
{
  public:
    explicit BlockLabels(QStringList found) : labels(std::move(found)) {}
    QStringList labels;
};

/// The labels defined on one line. A name followed by ':' outside a '#'
/// comment and outside a "string". One left-to-right pass.
QStringList scanLineLabels(const QString &line)
{
    QStringList found;
    bool        inString = false;
    QChar       quote;
    int         wordStart = -1;
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
            break;
        if (c == QLatin1Char('"') || c == QLatin1Char('\''))
        {
            inString  = true;
            quote     = c;
            wordStart = -1;
            continue;
        }
        if (Completion::isWordChar(c))
        {
            if (wordStart < 0)
                wordStart = i;
            continue;
        }
        if (c == QLatin1Char(':') && wordStart >= 0)
            found << line.mid(wordStart, i - wordStart);
        wordStart = -1;
    }
    return found;
}
} // namespace

Editor::Editor(QWidget *parent )
    : KitesEditor(parent)
{
    setMouseTracking(true);
    m_syntaxHighlighter = new SyntaxHighlighter(this->document());
    setFont(FontManager::getInstance().currentFont());

    const int tapspace = 4;
    setTabStopDistance(tapspace * fontMetrics().horizontalAdvance(' '));
    addLeftGutterColumn(new BreakPointGutterColumn(this));
    addLeftGutterColumn(new LineNumberGutterColumn(this));
    updateViewPortMargins();

    // ── the suggestion list. A child of the viewport, like the find bar, so
    //    it is clipped to the editor and can be driven in kites_test.
    m_completionPopup = new CompletionPopup(viewport());
    m_completionSource = [this](const QString &prefix)
    {
        QList<Completion::Item> items;
        // The static half - mnemonics, directives, registers, custom pseudo
        // instructions. LanguageService hands back bare strings with no kind
        // and no detail; the kind is derived from the symbol's shape, because
        // instructions.json holds nothing else. See known_issues.
        for (const QString &symbol : LanguageService::completions(prefix, QString()))
            items << Completion::Item{symbol, Completion::kindOf(symbol), {}};
        // The buffer's own labels, from the revision-keyed cache rather than
        // by handing the whole document over on every key.
        for (const QString &label : documentLabels())
        {
            if (!prefix.isEmpty() && !label.startsWith(prefix, Qt::CaseInsensitive))
                continue;
            items << Completion::Item{label, Completion::Kind::Label, {}};
        }
        return items;
    };
    connect(m_completionPopup, &CompletionPopup::rowActivated, this,
            [this](const QString &label) { acceptCompletion(label, false); });


    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            [this]
            {
                updateBracketHighlight();
                // K6: Left/Right keep it open inside the word and close it
                // the moment the caret leaves. A click lands here too, which
                // is what closes the list on a click elsewhere.
                if (isCompletionVisible() && !m_completionApplying)
                {
                    const Completion::WordRange word = caretWord();
                    const bool leftTheWord =
                        m_completionWordStart < 0 || word.start != m_completionWordStart ||
                        textCursor().position() > word.end;
                    // The caret reaching the word start empties the prefix,
                    // and the list never shows with an empty prefix except
                    // for the one Ctrl+Space opened explicitly.
                    const bool emptyPrefix = word.caret <= word.start;
                    if (leftTheWord || emptyPrefix)
                        closeCompletion();
                    else
                        placeCompletionPopup();
                }
            });
    // K7: it follows the caret while the editor scrolls, and goes when the
    // caret line leaves the viewport.
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this]
            {
                if (!isCompletionVisible())
                    return;
                const QRect caret = cursorRect();
                if (caret.bottom() < 0 || caret.top() > viewport()->height())
                    closeCompletion();
                else
                    placeCompletionPopup();
            });

    m_lastBlockCount = document()->blockCount();
    connect(document(), &QTextDocument::contentsChange, this,
            [this](int from, int charsRemoved, int charsAdded)
            {
                // applyEdit() remaps its own change through the command's
                // LineMap; anchors collapse inside a replaced range.
                // ── the label cache, rescanned where the edit landed.
                //
                // The previous rule - "the edited block's text contains a
                // ':'" - was wrong in both directions and is kept here as a
                // warning. Backspacing over a colon leaves a line WITHOUT
                // one, so the label was never retired; and a replacement
                // spanning lines only ever looked at the first of them, so a
                // label defined on a later line was never seen.
                //
                // Now the blocks the change actually touched are rescanned
                // and compared with what they held. The aggregate is rebuilt
                // only when some line's labels really differ, so typing
                // ordinary code still costs one scan of one short line.
                {
                    const int firstBlock = document()->findBlock(from).blockNumber();
                    const int lastBlock =
                        document()->findBlock(from + charsAdded).blockNumber();
                    // A line appearing or disappearing moves the aggregate
                    // whatever the touched lines now say.
                    bool moved = document()->blockCount() != m_lastBlockCount;
                    for (int number = firstBlock; number <= lastBlock; ++number)
                    {
                        QTextBlock block = document()->findBlockByNumber(number);
                        if (!block.isValid())
                            break;
                        const QStringList found = scanLineLabels(block.text());
                        auto *stored = static_cast<BlockLabels *>(block.userData());
                        if (!stored)
                        {
                            block.setUserData(new BlockLabels(found));
                            if (!found.isEmpty())
                                moved = true;
                        }
                        else if (stored->labels != found)
                        {
                            stored->labels = found;
                            moved = true;
                        }
                    }
                    if (moved)
                        m_labelsDirty = true;
                }

                if (m_remappingBreakpoints)
                    return;

                // Loading a file has always left the breakpoint NUMBERS
                // alone, and still does; they are only re-anchored to the
                // new document. Telling that apart from a user wiping the
                // document took measuring, because setPlainText arrives as
                // TWO changes and the first of them looks exactly like a
                // select-all-and-delete:
                //
                //   setPlainText     (0, removed=15, added=0,  undo=0)
                //                    (0, removed=0,  added=14, undo=0)
                //   select all + del (0, removed=13, added=0,  undo=1)
                //
                // The undo stack is the discriminator: setPlainText clears
                // it, an edit fills it. characterCount() counts the final
                // block separator, which is why the second test is >=.
                const bool everythingGone =
                    from == 0 && charsAdded == 0 && document()->characterCount() <= 1;
                const bool everythingArrived =
                    from == 0 && charsRemoved == 0 &&
                    charsAdded >= document()->characterCount() - 1;
                const bool documentReplaced =
                    everythingArrived || (everythingGone && !document()->isUndoAvailable());
                if (documentReplaced)
                {
                    m_lastBlockCount = document()->blockCount();
                    reanchorBreakpoints();
                    return;
                }
                refreshBreakpointsFromAnchors();
            });
}

QTextCursor Editor::anchorForLine(int line) const
{
    const QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return QTextCursor();

    QTextCursor cursor(document());
    cursor.setPosition(block.position());
    if (block.next().isValid())
        cursor.setPosition(block.next().position(), QTextCursor::KeepAnchor);
    else
        cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    return cursor;
}

void Editor::reanchorBreakpoints()
{
    m_breakpointAnchors.clear();
    for (uint64_t line : m_breakPoints)
        m_breakpointAnchors << anchorForLine(int(line));
}

void Editor::normaliseBreakpoints()
{
    const int lines = document()->blockCount();
    std::vector<uint64_t> kept;
    for (uint64_t line : m_breakPoints)
        if (line >= 1 && int(line) <= lines &&
            std::find(kept.begin(), kept.end(), line) == kept.end())
            kept.push_back(line);
    std::sort(kept.begin(), kept.end());
    m_breakPoints = kept;
}

void Editor::refreshBreakpointsFromAnchors()
{
    const int  blocks       = document()->blockCount();
    // A line can only have been created or destroyed if the count moved. If
    // it did not, an anchor whose selection went empty merely had its text
    // deleted - the line is still there, and so is its breakpoint.
    const bool linesChanged = blocks != m_lastBlockCount;
    m_lastBlockCount        = blocks;

    std::vector<uint64_t> lines;
    for (int i = 0; i < m_breakpointAnchors.size() && i < int(m_breakPoints.size()); ++i)
    {
        const QTextCursor &anchor = m_breakpointAnchors.at(i);
        if (anchor.isNull())
        {
            // Never anchored, because the line does not exist. It keeps its
            // number, which is what loading a shorter file has always done.
            lines.push_back(m_breakPoints.at(i));
            continue;
        }
        if (!anchor.hasSelection() && linesChanged)
            continue; // the line itself is gone
        lines.push_back(uint64_t(document()->findBlock(anchor.anchor()).blockNumber() + 1));
    }

    m_breakPoints = lines;
    normaliseBreakpoints();
    reanchorBreakpoints();
    viewport()->update();
}

bool Editor::hasBreakpointAtLine(int line) const
{
    return std::find(m_breakPoints.begin(), m_breakPoints.end(), uint64_t(line)) !=
           m_breakPoints.end();
}

void Editor::toggleBreakpointAtLine(int line)
{
    auto it = std::find(m_breakPoints.begin(), m_breakPoints.end(), uint64_t(line));
    if (it != m_breakPoints.end())
        m_breakPoints.erase(it);
    else
        m_breakPoints.push_back(uint64_t(line));
    normaliseBreakpoints();
    reanchorBreakpoints();
}

void Editor::clearBreakpoints()
{
    m_breakPoints.clear();
    m_breakpointAnchors.clear();
    viewport()->update();
    // The gutter needs no repaint from here: both callers already cause one.
    // Opening a file sets the document, and the gutter's own "Remove All"
    // calls update() on itself. See known_issues 18.
}

std::vector<uint64_t> Editor::getBreakpoints() const
{
    return m_breakPoints;
}

void Editor::setBreakpoints(const std::vector<uint64_t> &breakpoints)
{
    // Deliberately NOT normalised: this is the setter the Expanded pane and
    // a restored session come through, and it has always taken the numbers
    // as given, out of range or not. Anchoring is what is added.
    m_breakPoints = breakpoints;
    reanchorBreakpoints();
    m_lastBlockCount = document()->blockCount();
    viewport()->update();
}

void Editor::setBreakpointInteractionEnabled(bool enabled)
{
    m_breakpointInteractionEnabled = enabled;
}

void Editor::clearHighlights()
{
    m_LinesToHighlight.clear();
    viewport()->update(); // Trigger a repaint to remove highlights
}

void Editor::resetErrors()
{
    m_errorMessages.clear();
}

void Editor::setLinesToHighlight(const std::vector<std::pair<int, std::string>> &linesToHighlight)
{
    m_LinesToHighlight = linesToHighlight;
    viewport()->update(); // Trigger a repaint to show the highlights
}

void Editor::paintEvent(QPaintEvent *event)
{
    std::set<int> drawnLines;
    QPlainTextEdit::paintEvent(event);

    QPainter painter(viewport());

    painter.setRenderHint(QPainter::Antialiasing);

    for (const auto &[lineNumber, instructionStage] : m_LinesToHighlight)
    {
        if (lineNumber < 0 || lineNumber > this->blockCount() ||
            drawnLines.find(lineNumber) != drawnLines.end())
        {
            continue;
        }
        drawnLines.insert(lineNumber);
        QTextBlock block =
            this->document()->findBlockByNumber(lineNumber - 1); // -1 for 0-based index

        if (block.isValid())
        {
            QRectF blockRect = this->blockBoundingGeometry(block).translated(this->contentOffset());

            QLinearGradient gradient(blockRect.topLeft(), blockRect.topRight());
            gradient.setColorAt(0.0, QColor(255, 255, 255, 0)); // white
            gradient.setColorAt(1.0, QColor(255, 0, 0, 100));   // red // Semi-transparent orange
            painter.fillRect(blockRect, gradient);

            QRectF textRect = blockRect.adjusted(0, 0, 0, -1);
            textRect.setWidth(viewport()->width() - 10);

            painter.setPen(ThemeManager::getInstance().getEditorForegroundColor());
            painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, instructionStage.c_str());
        }
    }

    // we clear the lines to highlight after painting
    // otherwise the cursor will keep jumping to last highlighted line on every repaint
    // Now highlight the specified lines with gradient and draw messages
}

bool Editor::event(QEvent *event)
{
    // Check if the event is a tooltip request
    if (event->type() == QEvent::ToolTip)
    {
        // Cast the event to a QHelpEvent to get mouse coordinates
        QHelpEvent *helpEvent = static_cast<QHelpEvent *>(event);

        QTextCursor cursor = cursorForPosition(helpEvent->pos());
        QTextBlock block = cursor.block();
        // int textHeight = document()->size().height();
        QRectF blockRect = blockBoundingGeometry(block);
        blockRect.translate(contentOffset());

        // Doing this check to see if the mouse is actually over the text line
        // other wise if error is on last line and mouse is below text it still shows tooltip
        if (helpEvent->pos().y() < blockRect.top() || helpEvent->pos().y() > blockRect.bottom())
        {
            // The mouse is below (or above) the actual text line, likely in the empty space.
            QToolTip::hideText();
            event->ignore();
            return true;
        }

        int lineNumber = block.blockNumber();

        // If this line has a tooltip, show it
        // (Using .count() is a common and clean way to check for key existence)
        if (m_errorMessages.find(lineNumber) != m_errorMessages.end())
        {
            // Show our custom error tooltip
            QToolTip::showText(helpEvent->globalPos(), m_errorMessages.at(lineNumber), this);
            return true; // We handled this event!
        }
        else
        {
            // No error for this line, hide any active tooltip
            QToolTip::hideText();
            event->ignore();
            // Don't return true, let the base class handle it
            // (e.g., to show the widget's default tooltip if one is set)
        }
        return true;
    }

    // Pass all other events (including mouseMoveEvent) to the base class
    return QPlainTextEdit::event(event);
}

void Editor::setErrorMessage(int line, const QString &message)
{
    m_errorMessages.emplace(line, message);
}



// ───────────────────────── find and replace ──────────────────────────────

FindBar *Editor::findBar() const
{
    return m_findBar;
}

const FindEngine::FindResult &Editor::findResult() const
{
    return m_findResult;
}

int Editor::currentMatchIndex() const
{
    return m_currentMatch;
}

namespace
{
/// `amount` of `tint` mixed into `base`.
QColor blended(const QColor &base, const QColor &tint, double amount)
{
    return QColor::fromRgbF(base.redF() * (1 - amount) + tint.redF() * amount,
                            base.greenF() * (1 - amount) + tint.greenF() * amount,
                            base.blueF() * (1 - amount) + tint.blueF() * amount);
}
} // namespace

void Editor::refreshFindColors()
{
    const ThemeManager &theme = ThemeManager::getInstance();
    const QColor background = theme.getEditorBackgroundColor();
    const QColor accentish  = theme.getInfoColor();

    // A wash, so ordinary foreground text is as readable on a match as it is
    // on the page. The amount is not a guess: the theme harness sweeps it
    // and reports text-on-match contrast per theme. 0.32 put
    // tokyo-night-storm at 4.45, under the 4.5 gate; 0.25 leaves the worst
    // theme at 5.32 and is still a clearly visible tint.
    m_findMatchColor       = blended(background, accentish, 0.25);
    m_findCurrentColor     = accentish;
    m_findCurrentTextColor = background;
}

void Editor::updateBracketHighlight()
{
    const QTextCursor cursor = textCursor();
    // bracketAt() never looks past the end of the caret's line, so handing
    // it the caret's BLOCK gives an identical answer for a fraction of the
    // work. toPlainText() copies the whole document on every cursor move;
    // measured at 877,000 characters it cost about 4 ms per arrow key.
    const QTextBlock block     = cursor.block();
    const int        blockBase = block.position();
    EditorCommands::BracketPair pair =
        EditorCommands::bracketAt(block.text(), cursor.position() - blockBase);
    if (pair.offset >= 0)
    {
        pair.offset += blockBase;
        if (pair.partner >= 0)
            pair.partner += blockBase;
    }

    if (pair.offset < 0)
    {
        setBracketSelections({});
        return;
    }

    const ThemeManager &theme = ThemeManager::getInstance();
    QTextCharFormat matched;
    // The same wash family as a find match, so a bracket reads as a
    // highlight rather than as a second kind of selection. No new token.
    matched.setBackground(blended(theme.getEditorBackgroundColor(),
                                  theme.getEditorForegroundColor(), 0.25));
    QTextCharFormat unmatched;
    unmatched.setForeground(theme.getEditorErrorColor());
    unmatched.setFontUnderline(true);

    QList<QTextEdit::ExtraSelection> selections;
    auto add = [&](int offset, const QTextCharFormat &format)
    {
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(offset);
        selection.cursor.setPosition(offset + 1, QTextCursor::KeepAnchor);
        selection.format = format;
        selections.append(selection);
    };

    if (pair.partner < 0)
    {
        add(pair.offset, unmatched);
    }
    else
    {
        add(pair.offset, matched);
        add(pair.partner, matched);
    }
    setBracketSelections(selections);
}

void Editor::repositionFindBar()
{
    if (m_findBar && !m_findBar->isHidden())
        m_findBar->reposition(viewport());
}

void Editor::repositionOverlays()
{
    repositionFindBar();
    if (m_gotoBar && !m_gotoBar->isHidden())
        m_gotoBar->reposition(viewport());
}

GotoLineBar *Editor::gotoLineBar() const
{
    return m_gotoBar;
}

void Editor::openGotoLine()
{
    // Only one overlay at a time.
    closeFind();

    if (!m_gotoBar)
    {
        m_gotoBar = new GotoLineBar(viewport());
        connect(m_gotoBar, &GotoLineBar::closeRequested, this, [this] { closeGotoLine(); });
        connect(m_gotoBar, &GotoLineBar::rejected, this, [] {});
        connect(m_gotoBar, &GotoLineBar::accepted, this,
                [this](int line, int column)
                {
                    const QTextBlock block = document()->findBlockByNumber(line - 1);
                    if (!block.isValid())
                        return;
                    QTextCursor cursor = textCursor();
                    // A column past the end of the line lands at its end.
                    cursor.setPosition(block.position() +
                                       std::min(column - 1, block.length() - 1));
                    setTextCursor(cursor);
                    // Centred, so the line is not left against an edge.
                    centerCursor();
                    closeGotoLine();
                });
    }
    m_gotoBar->show();
    m_gotoBar->prepare(document()->blockCount());
    m_gotoBar->reposition(viewport());
}

void Editor::closeGotoLine()
{
    if (!m_gotoBar)
        return;
    m_gotoBar->hide();
    setFocus();
}

void Editor::openFind(bool withReplace)
{
    if (m_gotoBar && !m_gotoBar->isHidden())
        closeGotoLine();

    if (!m_findBar)
    {
        // Parented to the VIEWPORT, so it floats over the text and the
        // viewport's own size is the same open or closed.
        m_findBar = new FindBar(viewport());

        m_findDebounce = new QTimer(this);
        m_findDebounce->setSingleShot(true);
        m_findDebounce->setInterval(150);
        connect(m_findDebounce, &QTimer::timeout, this, [this] { runFind(true); });

        connect(m_findBar, &FindBar::patternChanged, this,
                [this] { m_findDebounce->start(); });
        connect(m_findBar, &FindBar::optionsChanged, this, [this] { runFind(true); });
        connect(m_findBar, &FindBar::findNext, this, [this] { stepMatch(true); });
        connect(m_findBar, &FindBar::findPrevious, this, [this] { stepMatch(false); });
        connect(m_findBar, &FindBar::replaceOne, this, [this] { replaceCurrentMatch(); });
        connect(m_findBar, &FindBar::replaceAll, this, [this] { replaceEveryMatch(); });
        connect(m_findBar, &FindBar::closeRequested, this, [this] { closeFind(); });
        // While the bar is open, an edit has to be re-searched.
        // The match colours are derived from the theme, so a theme change
        // while the bar is open has to rebuild them AND repaint the
        // highlights - an extra selection keeps the format it was given, so
        // without this the matches keep the old theme's colour. Measured:
        // after switching kites-dark to kites-light the wash was still the
        // dark one, 0 pixels of the new colour on a match.
        //
        // Both signals: the wash is mixed from the EDITOR background and the
        // UI theme's `info`, so either changing matters.
        const auto rebuildColours = [this](const QString &)
        {
            refreshFindColors();
            updateFindHighlights();
        };
        connect(&ThemeManager::getInstance(), &ThemeManager::themeChangedSignal, this,
                rebuildColours);
        connect(&ThemeManager::getInstance(), &ThemeManager::editorThemeChangedSignal, this,
                rebuildColours);

        connect(document(), &QTextDocument::contentsChanged, this,
                [this]
                {
                    if (m_findBar && !m_findBar->isHidden())
                    {
                        // The flag is for the three commands that act on a
                        // match; the timer is for the highlights.
                        m_findDirty = true;
                        m_findDebounce->start();
                    }
                });
    }

    refreshFindColors();

    const QTextCursor cursor = textCursor();
    // Prefill from a selection, but never a multi-line one - that is a block
    // of text, not a search term.
    if (cursor.hasSelection())
    {
        const QString selected = cursor.selectedText();
        if (!selected.contains(QChar::ParagraphSeparator) && !selected.contains(QLatin1Char('\n')))
            m_findBar->setPattern(selected);
    }

    m_findOrigin = std::min(cursor.anchor(), cursor.position());
    m_findBar->setReplaceVisible(withReplace);
    m_findBar->setReplaceEnabled(!isReadOnly());
    m_findBar->show();
    repositionFindBar();
    m_findBar->focusFind();
    runFind(true);
}

void Editor::closeFind()
{
    if (!m_findBar)
        return;
    m_findBar->hide();
    m_findResult = {};
    m_currentMatch = -1;
    clearFindSelections();
    setFocus();
}

void Editor::runFind(bool toOrigin)
{
    if (!m_findBar || m_findBar->isHidden())
        return;

    const QString pattern = m_findBar->pattern();
    m_findResult = FindEngine::findAll(toPlainText(), pattern, m_findBar->options());
    m_findDirty  = false;

    if (!m_findResult.error.isEmpty())
    {
        m_currentMatch = -1;
        m_findBar->setStatus(tr("Invalid regex: %1").arg(m_findResult.error), true);
        updateFindHighlights();
        return;
    }
    if (m_findResult.matches.isEmpty())
    {
        m_currentMatch = -1;
        m_findBar->setStatus(pattern.isEmpty() ? QString() : tr("No results"),
                             !pattern.isEmpty());
        updateFindHighlights();
        return;
    }

    int index = m_currentMatch;
    if (toOrigin || index < 0 || index >= m_findResult.matches.size())
        index = FindEngine::nextIndex(m_findResult.matches, m_findOrigin, true, true);
    goToMatch(index);
}

void Editor::refreshFindIfStale()
{
    if (!m_findDirty || !m_findBar || m_findBar->isHidden())
        return;

    m_findDirty  = false;
    m_findResult = FindEngine::findAll(toPlainText(), m_findBar->pattern(),
                                       m_findBar->options());
    // The old index means nothing against a new list, so the current match
    // becomes the first one at or after the caret - which is the match the
    // user can see.
    const QTextCursor cursor = textCursor();
    m_currentMatch = FindEngine::nextIndex(
        m_findResult.matches, std::min(cursor.anchor(), cursor.position()), true, true);
}

void Editor::goToMatch(int index)
{
    m_currentMatch = index;
    if (index < 0 || index >= m_findResult.matches.size())
    {
        updateFindHighlights();
        return;
    }

    const FindEngine::FindMatch &match = m_findResult.matches.at(index);
    QTextCursor cursor = textCursor();
    cursor.setPosition(match.start);
    cursor.setPosition(match.end(), QTextCursor::KeepAnchor);
    // Moving the caret is not an edit: the undo stack is untouched by any
    // number of finds, which a test asserts through document()->revision().
    setTextCursor(cursor);
    ensureCursorVisible();

    if (m_findBar)
    {
        QString status = tr("%1 of %2").arg(index + 1).arg(m_findResult.matches.size());
        if (m_findResult.truncated)
            status += tr(" (first %1)").arg(FindEngine::matchCap);
        m_findBar->setStatus(status);
    }
    updateFindHighlights();
}

void Editor::stepMatch(bool forward)
{
    if (!m_findBar || m_findBar->isHidden())
        return;

    refreshFindIfStale();

    if (m_findResult.matches.isEmpty())
    {
        runFind(true);
        return;
    }

    const QTextCursor cursor = textCursor();
    // Step from the far edge of what is selected, so a zero-length match
    // cannot hold the search in one place.
    const int from = forward ? std::max(cursor.position(), cursor.anchor()) + 1
                             : std::min(cursor.position(), cursor.anchor());
    goToMatch(FindEngine::nextIndex(m_findResult.matches, from, forward, true));
}

void Editor::updateFindHighlights()
{
    if (!m_findBar || m_findBar->isHidden() || m_findResult.matches.isEmpty())
    {
        clearFindSelections();
        return;
    }

    // Only what is on screen, plus a screenful either side, gets a
    // selection: a 10000-match document would otherwise hand Qt 10000
    // formats to lay out. The COUNT in the status line is the whole
    // document either way.
    const QTextBlock first = firstVisibleBlock();
    const int        margin = 200;
    const int        from   = std::max(0, first.position() - margin);
    int              to     = from + margin;
    QTextBlock block = first;
    const int height = viewport()->height();
    while (block.isValid() &&
           blockBoundingGeometry(block).translated(contentOffset()).top() < height)
    {
        to = block.position() + block.length();
        block = block.next();
    }
    to += margin;

    if (!m_findMatchColor.isValid())
        refreshFindColors();

    QTextCharFormat all;
    all.setBackground(m_findMatchColor);
    QTextCharFormat current;
    current.setBackground(m_findCurrentColor);
    current.setForeground(m_findCurrentTextColor);

    QList<QTextEdit::ExtraSelection> selections;
    for (int i = 0; i < m_findResult.matches.size(); ++i)
    {
        const FindEngine::FindMatch &match = m_findResult.matches.at(i);
        if (match.end() < from || match.start > to)
            continue;
        if (match.length == 0)
            continue; // nothing to paint

        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(document());
        selection.cursor.setPosition(match.start);
        selection.cursor.setPosition(match.end(), QTextCursor::KeepAnchor);
        selection.format = (i == m_currentMatch) ? current : all;
        selections.append(selection);
    }
    setFindSelections(selections);
}

void Editor::replaceCurrentMatch()
{
    if (isReadOnly() || !m_findBar || m_findBar->isHidden())
        return;

    // The document may have moved since the last search.
    refreshFindIfStale();
    if (m_currentMatch < 0 || m_currentMatch >= m_findResult.matches.size())
        return;

    const FindEngine::FindMatch match = m_findResult.matches.at(m_currentMatch);
    const QString text = FindEngine::expandReplacement(match, m_findBar->replacement(),
                                                       m_findBar->options());

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    cursor.setPosition(match.start);
    cursor.setPosition(match.end(), QTextCursor::KeepAnchor);
    cursor.insertText(text);
    cursor.endEditBlock();

    // The match list is stale the moment the document changes, so search
    // again and carry on from just past what was written.
    m_findResult   = FindEngine::findAll(toPlainText(), m_findBar->pattern(),
                                         m_findBar->options());
    m_currentMatch = -1;
    const int resumeAt = match.start + text.size();
    goToMatch(FindEngine::nextIndex(m_findResult.matches, resumeAt, true, true));
    if (m_findResult.matches.isEmpty())
        m_findBar->setStatus(tr("No results"), true);
}

void Editor::replaceEveryMatch()
{
    if (isReadOnly() || !m_findBar || m_findBar->isHidden())
        return;

    // The document may have moved since the last search.
    refreshFindIfStale();

    const QList<FindEngine::ReplaceEdit> edits =
        FindEngine::replaceAllEdits(m_findResult, m_findBar->replacement(),
                                    m_findBar->options());
    if (edits.isEmpty())
    {
        m_findBar->setStatus(tr("No results"), true);
        return;
    }

    // ONE edit block for the lot, so one Ctrl+Z puts every replacement back.
    // Applied back to front, so an earlier offset is never invalidated by a
    // replacement of a different length further up.
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    for (int i = edits.size() - 1; i >= 0; --i)
    {
        const FindEngine::ReplaceEdit &edit = edits.at(i);
        cursor.setPosition(edit.start);
        cursor.setPosition(edit.start + edit.length, QTextCursor::KeepAnchor);
        cursor.insertText(edit.text);
    }
    cursor.endEditBlock();

    const int replaced = edits.size();
    m_findResult   = FindEngine::findAll(toPlainText(), m_findBar->pattern(),
                                         m_findBar->options());
    m_currentMatch = -1;
    updateFindHighlights();
    m_findBar->setStatus(tr("Replaced %1").arg(replaced));
}

void Editor::resizeEvent(QResizeEvent *event)
{
    // K7: a resize re-lays the viewport out under the popup.
    closeCompletion();
    KitesEditor::resizeEvent(event);
    repositionOverlays();
}

void Editor::applyEdit(const EditorCommands::Edit &edit)
{
    if (!edit.valid)
        return;

    const std::vector<uint64_t> before = m_breakPoints;

    // The anchors cannot survive this: the whole range goes in one
    // insertText and they collapse inside it. The command's LineMap says
    // where the lines went, so that is what the breakpoints follow.
    m_remappingBreakpoints = true;

    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (!edit.isCursorOnly() || edit.length > 0)
    {
        cursor.setPosition(edit.start);
        cursor.setPosition(edit.start + edit.length, QTextCursor::KeepAnchor);
        cursor.insertText(edit.text);
    }
    cursor.endEditBlock();

    // Set after the block closes, so the cursor the user is left with is not
    // itself part of what Ctrl+Z has to put back.
    cursor.setPosition(edit.anchor);
    cursor.setPosition(edit.position, QTextCursor::KeepAnchor);
    setTextCursor(cursor);
    ensureCursorVisible();

    std::vector<uint64_t> after;
    for (uint64_t line : before)
    {
        const int moved = edit.lines.mapped(int(line) - 1);
        if (moved >= 0)
            after.push_back(uint64_t(moved + 1));
    }
    m_breakPoints = after;
    normaliseBreakpoints();
    reanchorBreakpoints();
    m_lastBlockCount       = document()->blockCount();
    m_remappingBreakpoints = false;
    viewport()->update();
}

void Editor::putLineOnClipboard(const QString &lineText)
{
    auto *mime = new QMimeData;
    mime->setText(lineText);
    mime->setData(EditorCommands::lineCopyMimeType, QByteArrayLiteral("1"));
    QGuiApplication::clipboard()->setMimeData(mime);
}

bool Editor::handleEditingKey(QKeyEvent *event)
{
    using namespace EditorCommands;

    const Qt::KeyboardModifiers mods = event->modifiers();
    const int                   key  = event->key();
    // The whole document, built only for the commands that need it. This
    // runs on EVERY key event, so building it eagerly meant a full copy of
    // the document per typed character - about 5 ms at 877,000 characters.
    // Nothing below changes the document before it is read, so caching it
    // for the duration of one key event is safe.
    QString       wholeText;
    bool          wholeLoaded = false;
    auto          whole       = [&]() -> const QString &
    {
        if (!wholeLoaded)
        {
            wholeText   = toPlainText();
            wholeLoaded = true;
        }
        return wholeText;
    };
    // An Edit computed against one block carries block-relative offsets.
    // Only the single-caret pair cases use it, and those never produce a
    // LineMap, so there is nothing else to translate.
    auto shifted = [](EditorCommands::Edit edit, int base)
    {
        if (edit.valid)
        {
            edit.start += base;
            edit.anchor += base;
            edit.position += base;
        }
        return edit;
    };
    const QTextCursor           here = textCursor();
    const int                   anchor   = here.anchor();
    const int                   position = here.position();
    const bool                  writable = !isReadOnly();
    const bool                  isReturn = key == Qt::Key_Return || key == Qt::Key_Enter;

    // ── Zoom. Shared with the Output pane through KitesEditor, and
    //    deliberately not a QAction or a QShortcut, like every other key
    //    here. Works in a read-only pane: it changes nothing in the
    //    document.
    if (handleZoomKey(event))
        return true;

    // ── Find, replace and go to line. All of them work in a read-only pane
    //    except the replace commands, which the bar itself refuses.
    if (key == Qt::Key_F && mods == Qt::ControlModifier)
    {
        openFind(false);
        return true;
    }
    if (key == Qt::Key_H && mods == Qt::ControlModifier)
    {
        // Measured on a bare KitesEditor: Ctrl+H does nothing today. It is
        // NOT Backspace on this platform's key bindings, which is worth
        // saying because it is on some.
        openFind(true);
        if (m_findBar)
            m_findBar->focusFind();
        return true;
    }
    if (key == Qt::Key_F3 && (mods == Qt::NoModifier || mods == Qt::ShiftModifier))
    {
        const bool forward = mods == Qt::NoModifier;
        if (!m_findBar || m_findBar->isHidden())
        {
            // F3 with the bar closed opens it on the last term and goes to
            // the next match; with no term it just opens.
            openFind(false);
            if (m_findBar && !m_findBar->pattern().isEmpty())
                stepMatch(forward);
        }
        else
        {
            stepMatch(forward);
        }
        return true;
    }
    if (key == Qt::Key_G && mods == Qt::ControlModifier)
    {
        openGotoLine();
        return true;
    }
    if (key == Qt::Key_Escape && mods == Qt::NoModifier && m_gotoBar && !m_gotoBar->isHidden())
    {
        closeGotoLine();
        return true;
    }
    if (key == Qt::Key_Escape && mods == Qt::NoModifier && m_findBar && !m_findBar->isHidden())
    {
        // Esc here leaves the current match selected: closeFind() clears the
        // highlights but never touches the cursor.
        closeFind();
        return true;
    }

    // ── Keys that work in a read-only pane too: they only move the caret or
    //    read the document.
    if (key == Qt::Key_Home && (mods == Qt::NoModifier || mods == Qt::ShiftModifier))
    {
        applyEdit(smartHome(whole(), anchor, position, mods == Qt::ShiftModifier));
        return true;
    }
    if (key == Qt::Key_L && mods == Qt::ControlModifier)
    {
        applyEdit(selectLine(whole(), anchor, position));
        return true;
    }
    if (key == Qt::Key_C && mods == Qt::ControlModifier && !here.hasSelection())
    {
        putLineOnClipboard(lineClip(whole(), anchor, position).clipboard);
        return true;
    }

    // ── Everything below changes text, so a read-only pane declines all of
    //    it. The Output pane is a different class and never reaches here at
    //    all; the Expanded view is this class and read-only, and this is
    //    what stops it being edited.
    if (!writable)
        return false;

    // ── Auto-closing pairs. Decided on the typed TEXT, so a layout where
    //    ( is not Shift+9 behaves the same, and only for an unmodified or
    //    shifted keystroke - Ctrl and Alt combinations carry text too and
    //    are not typing.
    if ((mods == Qt::NoModifier || mods == Qt::ShiftModifier) && !event->text().isEmpty())
    {
        // The pair rules only read the caret's own line. With no selection
        // the caret's block gives the same answer without copying the
        // document; a selection can span blocks, so that keeps the slow
        // path. Typing is the case that runs per character.
        const Edit pair =
            anchor == position
                ? shifted(autoPair(here.block().text(), position - here.block().position(),
                                   position - here.block().position(), event->text()),
                          here.block().position())
                : autoPair(whole(), anchor, position, event->text());
        if (pair.valid)
        {
            applyEdit(pair);
            return true;
        }
    }
    if (key == Qt::Key_Backspace && mods == Qt::NoModifier)
    {
        // pairBackspace() requires an empty selection, so the block path is
        // always the right one here.
        const Edit both = shifted(pairBackspace(here.block().text(),
                                                position - here.block().position(),
                                                position - here.block().position()),
                                  here.block().position());
        if (both.valid)
        {
            applyEdit(both);
            return true;
        }
    }

    if (key == Qt::Key_X && mods == Qt::ControlModifier && !here.hasSelection())
    {
        putLineOnClipboard(lineClip(whole(), anchor, position).clipboard);
        applyEdit(deleteLines(whole(), anchor, position));
        return true;
    }
    if (key == Qt::Key_V && mods == Qt::ControlModifier && !here.hasSelection())
    {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        if (mime && mime->hasFormat(lineCopyMimeType))
        {
            applyEdit(pasteLineAbove(whole(), anchor, position, mime->text()));
            return true;
        }
        return false; // an ordinary clipboard pastes the ordinary way
    }
    if (key == Qt::Key_Up && mods == Qt::AltModifier)
    {
        applyEdit(moveLines(whole(), anchor, position, -1));
        return true;
    }
    if (key == Qt::Key_Down && mods == Qt::AltModifier)
    {
        applyEdit(moveLines(whole(), anchor, position, 1));
        return true;
    }
    if (key == Qt::Key_Up && mods == (Qt::AltModifier | Qt::ShiftModifier))
    {
        applyEdit(duplicateLines(whole(), anchor, position, true));
        return true;
    }
    if (key == Qt::Key_Down && mods == (Qt::AltModifier | Qt::ShiftModifier))
    {
        applyEdit(duplicateLines(whole(), anchor, position, false));
        return true;
    }
    if (key == Qt::Key_K && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        applyEdit(deleteLines(whole(), anchor, position));
        return true;
    }
    if (isReturn && mods == Qt::ControlModifier)
    {
        applyEdit(insertLine(whole(), anchor, position, false));
        return true;
    }
    if (isReturn && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        applyEdit(insertLine(whole(), anchor, position, true));
        return true;
    }
    if (isReturn && mods == Qt::NoModifier)
    {
        applyEdit(newlineWithIndent(whole(), anchor, position));
        return true;
    }
    if (key == Qt::Key_Slash && mods == Qt::ControlModifier)
    {
        applyEdit(toggleComment(whole(), anchor, position));
        return true;
    }
    if (key == Qt::Key_BracketRight && mods == Qt::ControlModifier)
    {
        applyEdit(indentLines(whole(), anchor, position));
        return true;
    }
    if (key == Qt::Key_BracketLeft && mods == Qt::ControlModifier)
    {
        applyEdit(outdentLines(whole(), anchor, position));
        return true;
    }
    // Shift+Tab arrives as Key_Backtab. It outdents whether or not anything
    // is selected, which is what VS Code does.
    if (key == Qt::Key_Backtab)
    {
        applyEdit(outdentLines(whole(), anchor, position));
        return true;
    }
    // Tab only takes over when the selection really spans lines. With no
    // selection, or one inside a single line, it still inserts a tab - that
    // behaviour is deliberately untouched.
    if (key == Qt::Key_Tab && mods == Qt::NoModifier)
    {
        const LineRange touched = linesTouched(whole(), anchor, position);
        if (here.hasSelection() && touched.last > touched.first)
        {
            applyEdit(indentLines(whole(), anchor, position));
            return true;
        }
    }
    return false;
}

void Editor::keyPressEvent(QKeyEvent *event)
{
    // ── the suggestion list gets the keys it owns, ahead of every editing
    //    key. Esc is the one worth naming: it closes the popup and nothing
    //    else, so an open find bar stays open (K3).
    if (handleCompletionKey(event))
    {
        event->accept();
        return;
    }

    // ── Ctrl+Space opens the list at any caret position, with an empty
    //    prefix if need be. Handled HERE rather than in handleEditingKey,
    //    because a command that ran there closes the popup on the way out -
    //    which would close the one this just opened.
    //    Still an ordinary key in the editor: no QAction, no QShortcut.
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::ControlModifier)
    {
        openCompletionExplicitly();
        event->accept();
        return;
    }

    // ── K7: undo and redo close it. They are handled by QPlainTextEdit
    //    itself further down, so the list is dismissed on the way past.
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo))
        closeCompletion();

    if (handleEditingKey(event))
    {
        event->accept();
        // Every Tier 1 command, find, go-to-line and zoom close it.
        closeCompletion();
        return;
    }

    // What the key TYPED decides whether the list opens - never the fact
    // that a key arrived. A navigation key reaches here having moved the
    // caret, and must leave the list exactly as it found it.
    const QString typed = event->text();
    const bool    wordCharacter =
        typed.size() == 1 && Completion::isWordChar(typed.at(0)) &&
        (event->modifiers() == Qt::NoModifier || event->modifiers() == Qt::ShiftModifier);

    KitesEditor::keyPressEvent(event);

    if (wordCharacter)
    {
        triggerCompletionFromTyping();
        return;
    }
    if (event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
    {
        // Re-filters while open; never opens. Backspace to the word start
        // closes, which refreshCompletion() does when the prefix empties.
        refreshCompletion();
        return;
    }
    // Everything else - every arrow, Home/End, PageUp/PageDown, Ctrl+anything
    // - leaves it alone, and the caret-move handler decides whether the caret
    // has left the word.
}

// ════════════════════════ the suggestion list ════════════════════════════

void Editor::setCompletionSource(Completion::Source source)
{
    m_completionSource = std::move(source);
}

QList<Completion::Item> Editor::completionItemsForTest(const QString &prefix) const
{
    return m_completionSource ? m_completionSource(prefix) : QList<Completion::Item>{};
}

bool Editor::isCompletionVisible() const
{
    return m_completionPopup && m_completionPopup->isVisible();
}

void Editor::closeCompletion()
{
    // THE closing list. Everything that must dismiss the suggestions calls
    // this and nothing else, so a new overlay - Stage B's references and
    // rename bars - closes it by adding one call here.
    if (!m_completionPopup)
        return;
    m_completionPopup->hide();
    m_completionWordStart = -1;
}

Completion::WordRange Editor::caretWord() const
{
    const QTextCursor cursor = textCursor();
    const QTextBlock   block = cursor.block();
    const int          base  = block.position();
    Completion::WordRange range =
        Completion::wordAt(block.text(), cursor.position() - base);
    range.start += base;
    range.caret += base;
    range.end   += base;
    return range;
}

void Editor::placeCompletionPopup()
{
    if (!m_completionPopup)
        return;
    const Completion::WordRange word = caretWord();
    QTextCursor atWordStart(document());
    atWordStart.setPosition(word.start);
    const QRect wordRect  = cursorRect(atWordStart);
    const QRect caretLine = cursorRect();
    m_completionPopup->setEditorFont(font());
    m_completionPopup->placeFor(caretLine, wordRect.left(), viewport()->size());
}

void Editor::triggerCompletionFromTyping()
{
    if (!m_completionPopup || !m_completionSource || m_completionApplying)
        return;

    const Completion::WordRange word = caretWord();
    const QString prefix = toPlainText().mid(word.start, word.caret - word.start);
    if (prefix.isEmpty())
    {
        closeCompletion();
        return;
    }

    // T3: never inside a comment or a string. The same one-line scanner the
    // pair rules use, so "never" means the same thing in both places.
    const QTextBlock block = textCursor().block();
    const EditorCommands::LineContext context =
        EditorCommands::contextAt(block.text(), textCursor().position() - block.position());
    if (context.inComment || context.inString)
    {
        closeCompletion();
        return;
    }

    m_completionItems = m_completionSource(prefix);
    const QList<Completion::Match> matches = Completion::filter(m_completionItems, prefix);
    if (matches.isEmpty())
    {
        closeCompletion(); // F3
        return;
    }
    m_completionWordStart = word.start;
    m_completionPopup->setCandidates(m_completionItems, matches);
    placeCompletionPopup();
}

void Editor::openCompletionExplicitly()
{
    if (!m_completionPopup || !m_completionSource)
        return;
    const Completion::WordRange word = caretWord();
    const QString prefix = toPlainText().mid(word.start, word.caret - word.start);
    // T4: an empty prefix is allowed here and only here.
    m_completionItems = m_completionSource(prefix);
    const QList<Completion::Match> matches = Completion::filter(m_completionItems, prefix);
    if (matches.isEmpty())
    {
        closeCompletion();
        return;
    }
    m_completionWordStart = word.start;
    m_completionPopup->setCandidates(m_completionItems, matches);
    placeCompletionPopup();
}

void Editor::refreshCompletion()
{
    if (!isCompletionVisible() || !m_completionSource || m_completionApplying)
        return;
    const Completion::WordRange word = caretWord();
    // K5/K6: the caret left the word it opened on.
    if (m_completionWordStart >= 0 && word.start != m_completionWordStart)
    {
        closeCompletion();
        return;
    }
    const QString prefix = toPlainText().mid(word.start, word.caret - word.start);
    if (prefix.isEmpty())
    {
        closeCompletion();
        return;
    }
    m_completionItems = m_completionSource(prefix);
    const QList<Completion::Match> matches = Completion::filter(m_completionItems, prefix);
    if (matches.isEmpty())
    {
        closeCompletion();
        return;
    }
    m_completionPopup->setCandidates(m_completionItems, matches);
    placeCompletionPopup();
}

void Editor::acceptCompletion(const QString &label, bool replaceTail)
{
    if (!isCompletionVisible() || label.isEmpty())
        return;
    const Completion::WordRange word = caretWord();
    const int from = word.start;
    const int to   = replaceTail ? word.end : word.caret;

    m_completionApplying = true;
    QTextCursor cursor = textCursor();
    // One edit block, so one Ctrl+Z puts the typed prefix back.
    cursor.beginEditBlock();
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    cursor.insertText(label);
    cursor.endEditBlock();
    setTextCursor(cursor);
    m_completionApplying = false;
    closeCompletion();
}

bool Editor::handleCompletionKey(QKeyEvent *event)
{
    if (!isCompletionVisible())
        return false;

    const Qt::KeyboardModifiers mods = event->modifiers();
    const int                   key  = event->key();

    switch (key)
    {
    case Qt::Key_Escape: // K3
        closeCompletion();
        return true;
    case Qt::Key_Up:
        if (mods == Qt::NoModifier) { m_completionPopup->moveSelection(-1); return true; }
        break;
    case Qt::Key_Down:
        if (mods == Qt::NoModifier) { m_completionPopup->moveSelection(1); return true; }
        break;
    case Qt::Key_PageUp:
        if (mods == Qt::NoModifier) { m_completionPopup->movePage(-1); return true; }
        break;
    case Qt::Key_PageDown:
        if (mods == Qt::NoModifier) { m_completionPopup->movePage(1); return true; }
        break;
    case Qt::Key_Home:
    case Qt::Key_End: // K6: close, then let the caret move
        closeCompletion();
        return false;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (mods == Qt::NoModifier || mods == Qt::ShiftModifier)
        {
            acceptCompletion(m_completionPopup->selectedLabel(), mods == Qt::ShiftModifier);
            return true;
        }
        break;
    case Qt::Key_Tab:
        if (mods == Qt::NoModifier)
        {
            acceptCompletion(m_completionPopup->selectedLabel(), false);
            return true;
        }
        break;
    case Qt::Key_Backtab:
        if (mods == Qt::ShiftModifier)
        {
            acceptCompletion(m_completionPopup->selectedLabel(), true);
            return true;
        }
        break;
    default:
        break;
    }

    // K4: any non-word character closes it WITHOUT accepting. Assembly has no
    // commit characters, unlike VS Code - see the docs.
    const QString typed = event->text();
    if (typed.size() == 1 && !Completion::isWordChar(typed.at(0)) && !typed.at(0).isNull() &&
        (mods == Qt::NoModifier || mods == Qt::ShiftModifier))
    {
        closeCompletion();
        return false; // the character itself is still typed
    }
    return false;
}

void Editor::focusOutEvent(QFocusEvent *event)
{
    // K7. Window deactivation arrives here too, as a focus-out.
    closeCompletion();
    KitesEditor::focusOutEvent(event);
}

QStringList Editor::documentLabels() const
{
    if (!m_labelsDirty)
        return m_labelCache;

    // Rescans every line and REFRESHES what each block holds.
    //
    // The per-block data is a change DETECTOR, not the record. It cannot be
    // the record, because charsAdded is not reliable: measured,
    // setPlainText("loop:\n...") on an empty document emits a single
    // contentsChange of from=0, charsRemoved=1, charsAdded=0, so the
    // handler scanned block 0 while its text was still empty and stored
    // "no labels here" for a line that defines one. Trusting that gave an
    // empty list for a document full of labels.
    //
    // So the handler's job is only to notice that something MIGHT have
    // moved; this is where the answer is actually computed. It runs only
    // when the flag is set, which ordinary typing never does.
    QStringList labels;
    for (QTextBlock block = document()->begin(); block.isValid(); block = block.next())
    {
        const QStringList found = scanLineLabels(block.text());
        auto *stored = static_cast<BlockLabels *>(block.userData());
        if (!stored)
            block.setUserData(new BlockLabels(found));
        else
            stored->labels = found;
        labels << found;
    }
    labels.removeDuplicates();
    m_labelCache  = labels;
    m_labelsDirty = false;
    return m_labelCache;
}

} // namespace Kites
