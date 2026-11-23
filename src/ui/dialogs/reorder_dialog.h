/**
 * @file reorder_dialog.h
 * @brief "Suggest reordering": the original and the suggested program side
 *        by side, with the cycles MEASURED on the selected processor, and
 *        Apply / Cancel.
 *
 * Shown only for a suggestion the safety gate (scheduler_gate/
 * reorder_gate.h) offered. The reordering scheduler is by Nandan Nihal
 * (CS25BTECH11014); the footer says so.
 */
#pragma once

#include <QDialog>
#include <QString>

class QPlainTextEdit;
class QPushButton;
class QLabel;

namespace Atlas
{
/// "Reordering scheduler by Nandan Nihal (CS25BTECH11014)".
QString reorderSchedulerCredit();

/// Replaces the whole document as ONE undoable edit: a single undo()
/// restores the previous text exactly.
void replaceDocumentAsOneEdit(QPlainTextEdit &editor, const QString &text);

class ReorderDialog : public QDialog
{
    Q_OBJECT
  public:
    /**
     * @param canApply False while the editor is read-only (a program is
     *                 running); Apply is then disabled and says why.
     */
    ReorderDialog(const QString &original, const QString &suggested, const QString &processorName,
                  unsigned int originalCycles, unsigned int suggestedCycles, bool canApply,
                  QWidget *parent = nullptr);

    QString summaryText() const;
    QString originalTitle() const;
    QString suggestedTitle() const;
    QString footerText() const;
    QString suggestedSource() const;
    QPushButton *applyButton() const { return m_applyButton; }

  private:
    QLabel         *m_summary{nullptr};
    QLabel         *m_originalTitle{nullptr};
    QLabel         *m_suggestedTitle{nullptr};
    QLabel         *m_footer{nullptr};
    QPlainTextEdit *m_suggestedView{nullptr};
    QPushButton    *m_applyButton{nullptr};
};
} // namespace Atlas
