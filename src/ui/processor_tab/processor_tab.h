#ifndef PROCESSORTAB_H
#define PROCESSORTAB_H

#include "processor/processor_manager.h"
#include "ui/common/atlas_tab.h"

namespace Atlas
{
namespace Ui
{
class ProcessorTab;
}

class ProcessorTab : public AtlasTab
{
    Q_OBJECT

  public:
    // we are passing vmManager instead of just circuuit scene
    // because later we might want  to have acces to stuff like
    // cpi ipc etc that are in vmManager
    explicit ProcessorTab(QWidget *parent = nullptr, ProcessorManager *vmManager = nullptr);
    ~ProcessorTab();
    void setWiresStayActive(bool stayActive);

  private:
    Ui::ProcessorTab *ui;
    ProcessorManager *m_vmManager = nullptr;

  public slots:
    void onVMChanged();
};
} // namespace Atlas
#endif // PROCESSORTAB_H
