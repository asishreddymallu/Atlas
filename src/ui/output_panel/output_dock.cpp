#include "output_dock.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

namespace Kites
{
OutputDock::OutputDock(QWidget *parent) : QDockWidget("Output", parent)
{
    setObjectName("outputDock");
    auto *body   = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(4, 4, 4, 4);

    m_text = new QPlainTextEdit(body);
    m_text->setObjectName("programOutput");
    m_text->setReadOnly(true);
    m_text->setPlaceholderText("Program output appears here.");
    layout->addWidget(m_text);

    m_input = new QLineEdit(body);
    m_input->setObjectName("programInput");
    m_input->setPlaceholderText("The program reads input here when it asks: type and press Enter");
    m_input->setEnabled(false);
    layout->addWidget(m_input);

    setWidget(body);

    connect(m_input, &QLineEdit::returnPressed, this,
            [this]()
            {
                if (!m_input->isEnabled())
                    return;
                const QString line = m_input->text();
                m_input->clear();
                endInput();
                appendOutput(line + "\n"); // the echo a terminal would show
                emit inputSubmitted(line);
            });
}

QString OutputDock::text() const
{
    return m_text->toPlainText();
}

void OutputDock::appendOutput(const QString &text)
{
    QTextCursor cursor(m_text->document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(text);
    m_text->setTextCursor(cursor);
    m_text->ensureCursorVisible();
}

void OutputDock::clearOutput()
{
    m_text->clear();
    endInput();
}

void OutputDock::requestInput()
{
    show();
    raise();
    m_input->setEnabled(true);
    m_input->setFocus();
}

void OutputDock::endInput()
{
    m_input->setEnabled(false);
}
} // namespace Kites
