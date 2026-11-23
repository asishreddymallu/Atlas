/**
 * @file about_dialog.h
 * @brief Help > About: what Atlas is, credits, and where the licence and
 *        origin are recorded.
 */
#pragma once

#include <QDialog>
#include <QStringList>

namespace Atlas
{
class AboutDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit AboutDialog(QWidget *parent = nullptr);

    /// The dialog's text, one entry per line, in display order.
    static QStringList lines();
};
} // namespace Atlas
