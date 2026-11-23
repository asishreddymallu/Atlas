#ifndef PROCESSOR_DIALOG_H
#define PROCESSOR_DIALOG_H

#include "processor/processor_types.h"
#include <QDialog>

namespace Atlas
{
namespace Ui
{
class ProcessorDialog;
}

/**
 * @brief Picks one of the five models from three controls.
 *
 * Single-cycle or Pipeline, and - for a pipeline - hazard detection and
 * forwarding. The two checkboxes are live only while Pipeline is chosen;
 * on Single-cycle they are greyed and left alone, so coming back to
 * Pipeline returns the same model the user left.
 *
 * The dialog decides nothing itself. Which model three check marks mean,
 * and which check marks a model wears, are `processorTypeFor` and
 * `processorChoiceFor` in `processor/processor_choice.h`, tested without
 * a widget in `test/processor_choice_test.cpp`. This replaced a
 * QTreeWidget of five literal rows that were matched back by comparing
 * the selected row's TEXT, and preselected by indexing the tree with
 * `static_cast<int>` of the enum.
 */
class ProcessorDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit ProcessorDialog(QWidget *parent = nullptr,
                             const ProcessorType &currentVMType = ProcessorType::RVSS);
    ~ProcessorDialog();

  private:
    /// Greys or enables the two option boxes and refreshes the note.
    /// Never changes what the boxes are CHECKED to - that is the whole
    /// point of greying rather than clearing them.
    void refresh();

    /// The model the three controls currently spell out.
    ProcessorType selectedType() const;

    Ui::ProcessorDialog *ui;

  private slots:
    void on_buttonBox_accepted();

  signals:
    void vmSelected(const ProcessorType &vmType);
};
} // namespace Atlas
#endif // PROCESSOR_DIALOG_H
