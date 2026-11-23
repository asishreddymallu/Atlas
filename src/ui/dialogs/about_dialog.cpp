#include "ui/dialogs/about_dialog.h"

#include "ui/dialogs/reorder_dialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>

namespace Atlas
{
QStringList AboutDialog::lines()
{
    return {QStringLiteral("Atlas: a RISC-V assembly simulator"), reorderSchedulerCredit(),
            QStringLiteral("Licence and origin: see NOTICE.md and LICENSE.md")};
}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("About Atlas"));

    auto *layout = new QVBoxLayout(this);
    for (const QString &line : lines())
    {
        auto *label = new QLabel(line, this);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(label);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}
} // namespace Atlas
