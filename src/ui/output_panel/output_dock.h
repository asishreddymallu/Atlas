#pragma once
#include <QDockWidget>

class QLineEdit;
class QPlainTextEdit;

namespace Atlas
{
/**
 * @brief What the running program prints, and the line an ecall read
 *        asks for (known_issues 140).
 *
 * Read-only text ("programOutput") with an input line ("programInput")
 * under it. The input line is disabled until a read asks for input; Enter
 * hands the line on through inputSubmitted() and echoes it into the text,
 * as a terminal would. The window clears the panel on every Run, Debug
 * Run and reload.
 */
class OutputDock : public QDockWidget
{
    Q_OBJECT
  public:
    explicit OutputDock(QWidget *parent = nullptr);

    QString text() const;

  public slots:
    /// Appends exactly what the program printed; no newline is added.
    void appendOutput(const QString &text);
    void clearOutput();
    /// Enables the input line and focuses it.
    void requestInput();
    /// Disables the input line (the read was served, or the run ended).
    void endInput();

  signals:
    void inputSubmitted(const QString &line);

  private:
    QPlainTextEdit *m_text{nullptr};
    QLineEdit      *m_input{nullptr};
};
} // namespace Atlas
