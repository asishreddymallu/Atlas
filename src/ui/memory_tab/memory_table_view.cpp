#include "memory_table_view.h"

namespace Atlas
{
MemoryTableView::MemoryTableView(QWidget *parent) : QTableView(parent)
{
}

void MemoryTableView::wheelEvent(QWheelEvent *event)
{
    if (event->angleDelta().y() > 0)
    {
        emit scrolled(true);
    }
    else
    {
        emit scrolled(false);
    }
}

void MemoryTableView::resizeEvent(QResizeEvent *event)
{
    QTableView::resizeEvent(event);
    emit resized();
}
} // namespace Atlas
