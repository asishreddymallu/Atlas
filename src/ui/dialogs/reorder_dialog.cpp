#include "ui/dialogs/reorder_dialog.h"

#include "ui/theme/font_manager.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QVBoxLayout>

namespace Atlas
{
namespace
{
QString cycles(unsigned int count)
{
    return count == 1 ? QStringLiteral("1 cycle") : QStringLiteral("%1 cycles").arg(count);
}

/// A small muted caption, styled by the theme's QLabel[paneTitle="true"].
QLabel *paneTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("paneTitle", true);
    return label;
}

QPlainTextEdit *codeView(const QString &text, QWidget *parent)
{
    auto *view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setLineWrapMode(QPlainTextEdit::NoWrap);
    view->setFont(FontManager::getInstance().currentFont());
    view->setPlainText(text);
    return view;
}
} // namespace

QString reorderSchedulerCredit()
{
    return QStringLiteral("Reordering scheduler by Nandan Nihal (CS25BTECH11014)");
}

void replaceDocumentAsOneEdit(QPlainTextEdit &editor, const QString &text)
{
    QTextCursor cursor(editor.document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
}

ReorderDialog::ReorderDialog(const QString &original, const QString &suggested,
                             const QString &processorName, unsigned int originalCycles,
                             unsigned int suggestedCycles, bool canApply, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Suggest reordering"));

    const unsigned int saved = originalCycles > suggestedCycles ? originalCycles - suggestedCycles : 0;
    m_summary = new QLabel(QStringLiteral("Measured on %1: %2 instead of %3 (%4 fewer). "
                                          "Final registers and memory are identical.")
                               .arg(processorName, cycles(suggestedCycles), cycles(originalCycles),
                                    cycles(saved)),
                           this);
    m_summary->setWordWrap(true);

    m_originalTitle  = paneTitle(QStringLiteral("Original - %1").arg(cycles(originalCycles)), this);
    m_suggestedTitle = paneTitle(QStringLiteral("Suggested - %1").arg(cycles(suggestedCycles)), this);
    m_suggestedView  = codeView(suggested, this);

    auto *columns = new QHBoxLayout;
    auto *left    = new QVBoxLayout;
    left->addWidget(m_originalTitle);
    left->addWidget(codeView(original, this));
    auto *right = new QVBoxLayout;
    right->addWidget(m_suggestedTitle);
    right->addWidget(m_suggestedView);
    columns->addLayout(left);
    columns->addLayout(right);

    m_footer = paneTitle(reorderSchedulerCredit(), this);
    m_footer->setObjectName(QStringLiteral("reorderCredit"));

    auto *buttons = new QDialogButtonBox(this);
    m_applyButton = buttons->addButton(QStringLiteral("Apply"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);
    m_applyButton->setEnabled(canApply);
    if (!canApply)
        m_applyButton->setToolTip(QStringLiteral("The editor is read-only while a program runs"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_summary);
    layout->addLayout(columns, 1);
    layout->addWidget(m_footer);
    layout->addWidget(buttons);
    resize(900, 520);
}

QString ReorderDialog::summaryText() const { return m_summary->text(); }
QString ReorderDialog::originalTitle() const { return m_originalTitle->text(); }
QString ReorderDialog::suggestedTitle() const { return m_suggestedTitle->text(); }
QString ReorderDialog::footerText() const { return m_footer->text(); }
QString ReorderDialog::suggestedSource() const { return m_suggestedView->toPlainText(); }
} // namespace Atlas
