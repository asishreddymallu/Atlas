#pragma once
#include <QTableView>
#include <QWheelEvent>
namespace Atlas
{
class MemoryTableView : public QTableView
{
    Q_OBJECT

  public:
    MemoryTableView(QWidget *parent = nullptr);
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  signals:
    void scrolled(bool dir);
    void resized();
};
} // namespace Atlas
