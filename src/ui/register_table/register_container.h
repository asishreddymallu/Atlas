#ifndef REGISTERCONTAINER_H
#define REGISTERCONTAINER_H

#include "register_model.h"
#include <QWidget>
#include <memory>

class QSortFilterProxyModel;

namespace Kites
{
namespace Ui
{
class RegisterContainer;
}
class CollapsibleSection;

class RegisterContainer : public QWidget
{
    Q_OBJECT

  public:
    explicit RegisterContainer(QWidget *parent = nullptr, RegisterFile *regfile = nullptr);
    ~RegisterContainer();
    void setRegisterFile(RegisterFile *regfile);

    /// Selects a display base by driving this panel's own combo box. The
    /// combo stays the state; this only moves it, and its existing handler
    /// does the rest. A base the combo does not offer is ignored.
    void setDisplayBase(Base base);

    /// The base the combo box currently shows.
    Base displayBase() const;

  signals:
    /// Emitted whenever the combo box lands on a new base, whoever moved it,
    /// so a second view of the same choice can follow it.
    void displayBaseChangedSignal(Base base);

  public:
    // private slots:
    //     void updateRegisterValue(size_t regIndex, uint64_t value);
  private:
    void setupRegisterTable();
    QSortFilterProxyModel *makeRangeProxy(int firstRow, int lastRowExclusive);

    Ui::RegisterContainer *ui;
    RegisterModel *m_registerModel;
};
} // namespace Kites
#endif // REGISTERCONTAINER_H
