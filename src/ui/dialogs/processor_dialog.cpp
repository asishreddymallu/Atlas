#include "processor_dialog.h"
#include "processor/processor_choice.h"
#include "ui_processor_dialog.h"

namespace Kites
{
ProcessorDialog::ProcessorDialog(QWidget *parent, const ProcessorType &currentVMType)
    : QDialog(parent), ui(new Ui::ProcessorDialog)
{
    ui->setupUi(this);

    // The dialog opens on the processor that is loaded, read through the
    // mapping rather than by indexing anything with the enum's value.
    const ProcessorChoice choice = processorChoiceFor(currentVMType);
    ui->pipelineRadio->setChecked(choice.pipeline);
    ui->singleCycleRadio->setChecked(!choice.pipeline);
    ui->hazardCheck->setChecked(choice.hazard);
    ui->forwardingCheck->setChecked(choice.forwarding);

    // Only the Pipeline radio changes what is enabled, but all three
    // change the note, so all three come here.
    connect(ui->pipelineRadio, &QRadioButton::toggled, this, &ProcessorDialog::refresh);
    connect(ui->hazardCheck, &QCheckBox::toggled, this, &ProcessorDialog::refresh);
    connect(ui->forwardingCheck, &QCheckBox::toggled, this, &ProcessorDialog::refresh);

    refresh();
}

void ProcessorDialog::refresh()
{
    const bool pipeline = ui->pipelineRadio->isChecked();

    // setEnabled, not setChecked: greying leaves the two boxes holding
    // whatever the user last chose, so Pipeline -> Single-cycle ->
    // Pipeline comes back to the same model.
    ui->hazardCheck->setEnabled(pipeline);
    ui->forwardingCheck->setEnabled(pipeline);

    ui->noteLabel->setText(QString::fromUtf8(processorNote(
        pipeline, ui->hazardCheck->isChecked(), ui->forwardingCheck->isChecked())));
}

ProcessorType ProcessorDialog::selectedType() const
{
    return processorTypeFor(ui->pipelineRadio->isChecked(), ui->hazardCheck->isChecked(),
                            ui->forwardingCheck->isChecked());
}

ProcessorDialog::~ProcessorDialog()
{
    delete ui;
}

void ProcessorDialog::on_buttonBox_accepted()
{
    emit vmSelected(selectedType());
}

} // namespace Kites
