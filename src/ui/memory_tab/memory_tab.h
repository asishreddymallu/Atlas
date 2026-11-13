#ifndef MEMORYTAB_H
#define MEMORYTAB_H

#include "ui/common/kites_tab.h"
#include "memory_model.h"
namespace Kites
{

namespace Ui
{
class MemoryTab;
}

class MemoryTab : public KitesTab
{
    Q_OBJECT

  public:
    explicit MemoryTab(QWidget *parent = nullptr, MemoryController *memoryController = nullptr);
    void changeMemoryController(MemoryController *memoryController);
    ~MemoryTab();

    /// Presses the tab's own Search button, so the search runs through the
    /// one path it always has. Added so the ribbon can offer the same
    /// command without knowing how the search is built.
    void triggerSearch();

    /// Selects a display base by driving the tab's own combo box. The combo
    /// stays the state; this only moves it, and its existing handler does
    /// the rest. An unknown base is ignored.
    void setDisplayBase(Base base);

    /// The base the combo box currently shows.
    Base displayBase() const;

  signals:
    /// Emitted whenever the combo box lands on a new base, whoever moved it,
    /// so a second view of the same choice can follow it.
    void displayBaseChangedSignal(Base base);

  private:
    Ui::MemoryTab *ui;
    MemoryModel *m_memoryModel = nullptr;
};
} // namespace Kites
#endif // MEMORYTAB_H
