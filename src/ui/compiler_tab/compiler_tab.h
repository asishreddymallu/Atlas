#ifndef COMPILERTAB_H
#define COMPILERTAB_H

#include "ui/common/kites_tab.h"
#include <QWidget>


namespace Kites
{
namespace Ui
{
class CompilerTab;
}

class CompilerTab : public KitesTab
{
    Q_OBJECT

  public:
    explicit CompilerTab(QWidget *parent = nullptr);
    ~CompilerTab();
    QString getUsersCompilerOptions();
    void onCopyToMainEditorClicked();
    void onConvertToAssemblyClicked();

    /// Whether the tab's Compile button can be pressed. False while a
    /// compile is in flight, which is the whole reason this is public: a
    /// second view of the command must not offer what the tab is refusing.
    bool isCompileEnabled() const;

  signals:
    /// Emitted whenever the Compile button's enabled state changes, whoever
    /// changed it. Driven by watching the button itself rather than by the
    /// compile code announcing it, so a future caller that disables the
    /// button cannot forget to.
    void compileEnabledChangedSignal(bool enabled);

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private slots:
    void applyOutputBorderStyle();

  private:
    Ui::CompilerTab *ui;
    QString cleanAssembly(const QString &rawAssembly);
};
} // namespace Kites
#endif // COMPILERTAB_H
