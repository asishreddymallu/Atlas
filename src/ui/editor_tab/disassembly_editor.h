#pragma once
#include "ui/common/kites_editor.h"
#include "ui/common/line_number_gutter_column.h"
#include "ui/theme/theme_manager.h"
#include <set>
#include <QPainter>
namespace Kites
{
class DisassemblyEditor : public KitesEditor
{
public:
    DisassemblyEditor(QWidget *parent = nullptr) 
    : KitesEditor(parent)
    {
        setReadOnly(true);
        addLeftGutterColumn(new LineNumberGutterColumn(this));
        // KitesEditor's constructor sizes the viewport margins before this
        // gutter exists, so it has to be recomputed here or the gutter paints
        // over the first characters (same call Editor and ProfilerEditor make)
        updateViewPortMargins();
    }
    //yes i am repeating code
    // i dont care i dont want to make a new class just for this highlighting logic
    void setLinesToHighlight(const std::vector<std::pair<int,std::string>> &linesToHighlight)
    {
        m_LinesToHighlight = linesToHighlight;
        viewport()->update();
    }

    void clearHighlights()
    {
        m_LinesToHighlight.clear();
        viewport()->update(); 
    }
protected:
    void paintEvent(QPaintEvent *event) override
    {
        std::set<int> drawnLines;
        QPlainTextEdit::paintEvent(event);

        QPainter painter(viewport());

        painter.setRenderHint(QPainter::Antialiasing);

        for (const auto &[lineNumber, instructionStage] : m_LinesToHighlight)
        {
            if (lineNumber < 0 || lineNumber >= this->blockCount() ||
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
    }
private:
    std::vector<std::pair<int, std::string>> m_LinesToHighlight;
};
}//namespace Kites