#pragma once
#include <QToolBar>
#include <QWidget>

namespace Atlas
{
class AtlasTab : public QWidget
{
  public:
    AtlasTab(QWidget *parent = nullptr);
  protected:
    QToolBar *m_toolbar = nullptr;
};
} // namespace Atlas
