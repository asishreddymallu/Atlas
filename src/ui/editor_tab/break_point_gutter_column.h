#pragma once
#include "ui/common/gutter_column.h"
#include "ui/theme/theme_manager.h"
#include "main_editor.h"
#include <QPainter>
#include <QMenu>
namespace Kites
{
class BreakPointGutterColumn : public GutterColumn
{
    Q_OBJECT
public:
    explicit BreakPointGutterColumn(Editor* editor = nullptr)
    :GutterColumn(editor), m_mainEditor(editor)
    {
        // needed for the hover indicator: without it mouseMoveEvent only
        // arrives while a button is held down
        setMouseTracking(true);
    }
    QSize sizeHint() const override
    {
        return QSize(gutterWidth, 0);
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.fillRect(event->rect(),
                         ThemeManager::getInstance().getEditorBackgroundColor());

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = blockTop(block);
        int bottom = top + blockHeight(block);

        while (block.isValid() && top <= event->rect().bottom())
        {
            if (block.isVisible() && bottom >= event->rect().top())
            {
                const bool hasBreakpoint =
                    std::find(m_mainEditor->m_breakPoints.begin(),
                              m_mainEditor->m_breakPoints.end(),
                              blockNumber + 1) != m_mainEditor->m_breakPoints.end();

                // A faint ghost on the hovered line shows where a click would
                // place a breakpoint; a solid dot marks one that exists.
                if (hasBreakpoint || blockNumber == m_hoverLine)
                {
                    QColor dotColor = ThemeManager::getInstance().getEditorErrorColor();
                    if (!hasBreakpoint)
                        dotColor.setAlpha(ghostAlpha);

                    painter.setBrush(dotColor);
                    painter.setPen(Qt::NoPen);
                    const int left = (gutterWidth - dotRadius) / 2;
                    painter.drawEllipse(left, top + (fontMetrics().height() - dotRadius) / 2,
                                        dotRadius, dotRadius);
                }
            }

            block = block.next();
            top = bottom;
            bottom = top + blockHeight(block);
            ++blockNumber;
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {

        auto toggleBreakpointAtLine = [this, event]()
        {
            QPoint clickPos = m_mainEditor->viewport()->mapFromGlobal(event->globalPosition().toPoint());
            QTextCursor cursor = m_mainEditor->cursorForPosition(clickPos);

            QTextBlock block = cursor.block();

            // check if click is within the block's vertical bounds
            // cause cusorForPosition returns nearest line if clicked below text
            if (clickPos.y() >= blockTop(block) &&
                clickPos.y() <= blockTop(block) + blockHeight(block))
            {
                int lineNumber = block.blockNumber() + 1; // +1 for 1-based line numbers
                auto it = std::find(m_mainEditor->m_breakPoints.begin(), m_mainEditor->m_breakPoints.end(), lineNumber);
                if (it != m_mainEditor->m_breakPoints.end())
                {
                    m_mainEditor->m_breakPoints.erase(it);
                }
                else
                {
                    m_mainEditor->m_breakPoints.push_back(lineNumber);
                }
                update(); // Trigger a repaint to show/hide the breakpoint indicator
            }
        };

        if (event->button() == Qt::LeftButton && event->pos().x() < width())
        {
            toggleBreakpointAtLine();
        }

        if (event->button() == Qt::RightButton)
        {
            QMenu menu(this);

            QAction *action1 = menu.addAction("Toggle Breakpoint");
            QAction *action2 = menu.addAction("Remove All Breakpoints");

            QAction *selectedAction = menu.exec(event->globalPosition().toPoint());
            if (selectedAction == action1)
            {
                toggleBreakpointAtLine();
            }
            else if (selectedAction == action2)
            {
                m_mainEditor->m_breakPoints.clear();
                update(); // Trigger a repaint to remove all breakpoint indicators
            }
            return; // Prevent base class selection
        }
        
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int line = lineAt(event->position().toPoint());
        if (line != m_hoverLine)
        {
            m_hoverLine = line;
            update(); // only when the hovered line actually changes
        }
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_hoverLine != -1)
        {
            m_hoverLine = -1;
            update();
        }
        GutterColumn::leaveEvent(event);
    }
private:
    // 0-based block number under `pos`, or -1 when it is past the last line
    int lineAt(const QPoint &pos) const
    {
        const QPoint editorPos = m_mainEditor->viewport()->mapFrom(
            const_cast<BreakPointGutterColumn *>(this), pos);
        const QTextBlock block = m_mainEditor->cursorForPosition(editorPos).block();
        if (editorPos.y() < blockTop(block) ||
            editorPos.y() > blockTop(block) + blockHeight(block))
        {
            return -1;
        }
        return block.blockNumber();
    }

    // slightly wider than the old 15px so the dot is comfortable to hit
    static constexpr int gutterWidth = 20;
    static constexpr int dotRadius   = 9;
    static constexpr int ghostAlpha  = 70;

    Editor* m_mainEditor;
    int m_hoverLine = -1;
};
}//namespace Kites