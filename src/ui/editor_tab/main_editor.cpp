#include "main_editor.h"
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
bool isCompletionWordChar(QChar c)
{
    // default Qt word boundaries exclude '.'/'$', which real RISC-V syntax needs
    // (fadd.s, and labels can contain '.'/'$')
    return c.isLetterOrNumber() || c == '_' || c == '.' || c == '$';
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

    m_autoCompleter = new QCompleter(this);
    m_autoCompleter->setModel(new QStringListModel(m_autoCompleter));
    m_autoCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    m_autoCompleter->setCompletionMode(QCompleter::PopupCompletion);
    m_autoCompleter->setWidget(this);
    connect(m_autoCompleter, QOverload<const QString &>::of(&QCompleter::activated), this,
            &Editor::insertCompletion);

    m_lastBlockCount = document()->blockCount();
    connect(document(), &QTextDocument::contentsChange, this,
            [this](int from, int charsRemoved, int charsAdded)
            {
                // applyEdit() remaps its own change through the command's
                // LineMap; anchors collapse inside a replaced range.
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

QString Editor::textUnderCursor() const
{
    const QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    const QString blockText = block.text();
    const int posInBlock = cursor.position() - block.position();

    int start = posInBlock;
    while (start > 0 && isCompletionWordChar(blockText[start - 1]))
        --start;
    int end = posInBlock;
    while (end < blockText.size() && isCompletionWordChar(blockText[end]))
        ++end;

    return blockText.mid(start, end - start);
}

void Editor::insertCompletion(const QString &completion)
{
    if (m_autoCompleter->widget() != this)
        return;

    QTextCursor cursor = textCursor();
    const int prefixLength = m_autoCompleter->completionPrefix().length();
    cursor.setPosition(cursor.position() - prefixLength);
    cursor.setPosition(cursor.position() + prefixLength, QTextCursor::KeepAnchor);
    cursor.insertText(completion);
    setTextCursor(cursor);
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
    const QString               text = toPlainText();
    const QTextCursor           here = textCursor();
    const int                   anchor   = here.anchor();
    const int                   position = here.position();
    const bool                  writable = !isReadOnly();
    const bool                  isReturn = key == Qt::Key_Return || key == Qt::Key_Enter;

    // ── Keys that work in a read-only pane too: they only move the caret or
    //    read the document.
    if (key == Qt::Key_Home && (mods == Qt::NoModifier || mods == Qt::ShiftModifier))
    {
        applyEdit(smartHome(text, anchor, position, mods == Qt::ShiftModifier));
        return true;
    }
    if (key == Qt::Key_L && mods == Qt::ControlModifier)
    {
        applyEdit(selectLine(text, anchor, position));
        return true;
    }
    if (key == Qt::Key_C && mods == Qt::ControlModifier && !here.hasSelection())
    {
        putLineOnClipboard(lineClip(text, anchor, position).clipboard);
        return true;
    }

    // ── Everything below changes text, so a read-only pane declines all of
    //    it. The Output pane is a different class and never reaches here at
    //    all; the Expanded view is this class and read-only, and this is
    //    what stops it being edited.
    if (!writable)
        return false;

    if (key == Qt::Key_X && mods == Qt::ControlModifier && !here.hasSelection())
    {
        putLineOnClipboard(lineClip(text, anchor, position).clipboard);
        applyEdit(deleteLines(text, anchor, position));
        return true;
    }
    if (key == Qt::Key_V && mods == Qt::ControlModifier && !here.hasSelection())
    {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        if (mime && mime->hasFormat(lineCopyMimeType))
        {
            applyEdit(pasteLineAbove(text, anchor, position, mime->text()));
            return true;
        }
        return false; // an ordinary clipboard pastes the ordinary way
    }
    if (key == Qt::Key_Up && mods == Qt::AltModifier)
    {
        applyEdit(moveLines(text, anchor, position, -1));
        return true;
    }
    if (key == Qt::Key_Down && mods == Qt::AltModifier)
    {
        applyEdit(moveLines(text, anchor, position, 1));
        return true;
    }
    if (key == Qt::Key_Up && mods == (Qt::AltModifier | Qt::ShiftModifier))
    {
        applyEdit(duplicateLines(text, anchor, position, true));
        return true;
    }
    if (key == Qt::Key_Down && mods == (Qt::AltModifier | Qt::ShiftModifier))
    {
        applyEdit(duplicateLines(text, anchor, position, false));
        return true;
    }
    if (key == Qt::Key_K && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        applyEdit(deleteLines(text, anchor, position));
        return true;
    }
    if (isReturn && mods == Qt::ControlModifier)
    {
        applyEdit(insertLine(text, anchor, position, false));
        return true;
    }
    if (isReturn && mods == (Qt::ControlModifier | Qt::ShiftModifier))
    {
        applyEdit(insertLine(text, anchor, position, true));
        return true;
    }
    if (isReturn && mods == Qt::NoModifier)
    {
        applyEdit(newlineWithIndent(text, anchor, position));
        return true;
    }
    if (key == Qt::Key_Slash && mods == Qt::ControlModifier)
    {
        applyEdit(toggleComment(text, anchor, position));
        return true;
    }
    if (key == Qt::Key_BracketRight && mods == Qt::ControlModifier)
    {
        applyEdit(indentLines(text, anchor, position));
        return true;
    }
    if (key == Qt::Key_BracketLeft && mods == Qt::ControlModifier)
    {
        applyEdit(outdentLines(text, anchor, position));
        return true;
    }
    // Shift+Tab arrives as Key_Backtab. It outdents whether or not anything
    // is selected, which is what VS Code does.
    if (key == Qt::Key_Backtab)
    {
        applyEdit(outdentLines(text, anchor, position));
        return true;
    }
    // Tab only takes over when the selection really spans lines. With no
    // selection, or one inside a single line, it still inserts a tab - that
    // behaviour is deliberately untouched.
    if (key == Qt::Key_Tab && mods == Qt::NoModifier)
    {
        const LineRange touched = linesTouched(text, anchor, position);
        if (here.hasSelection() && touched.last > touched.first)
        {
            applyEdit(indentLines(text, anchor, position));
            return true;
        }
    }
    return false;
}

void Editor::keyPressEvent(QKeyEvent *event)
{
    if (m_autoCompleter && m_autoCompleter->popup()->isVisible())
    {
        // let the completer's own popup handle these - it has an event filter
        // installed on this widget that already intercepts most of them, this
        // guard covers the rest (matches Qt's own QCompleter usage pattern)
        //
        // Only the UNMODIFIED keys, which are the ones the popup actually
        // uses. Ctrl+Enter and Ctrl+Shift+Enter mean "new line below/above"
        // and the popup has no use for them; guarding on the key alone sent
        // them nowhere whenever a completion happened to be showing.
        const bool bare    = event->modifiers() == Qt::NoModifier;
        const bool backtab = event->key() == Qt::Key_Backtab &&
                             event->modifiers() == Qt::ShiftModifier;
        switch (event->key())
        {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            if (bare || backtab)
            {
                event->ignore();
                return;
            }
            break;
        default:
            break;
        }
    }

    if (handleEditingKey(event))
    {
        event->accept();
        if (m_autoCompleter)
            m_autoCompleter->popup()->hide();
        return;
    }

    KitesEditor::keyPressEvent(event);

    if (!m_autoCompleter)
        return;

    const QString prefix = textUnderCursor();
    if (prefix.isEmpty())
    {
        m_autoCompleter->popup()->hide();
        return;
    }

    const QStringList matches = LanguageService::completions(prefix, toPlainText());
    if (matches.isEmpty())
    {
        m_autoCompleter->popup()->hide();
        return;
    }

    auto *model = qobject_cast<QStringListModel *>(m_autoCompleter->model());
    model->setStringList(matches);

    m_autoCompleter->setCompletionPrefix(prefix);
    m_autoCompleter->popup()->setCurrentIndex(m_autoCompleter->completionModel()->index(0, 0));

    QRect completionRect = cursorRect();
    completionRect.setWidth(m_autoCompleter->popup()->sizeHintForColumn(0) +
                            m_autoCompleter->popup()->verticalScrollBar()->sizeHint().width());
    m_autoCompleter->complete(completionRect);
}
} // namespace Kites
