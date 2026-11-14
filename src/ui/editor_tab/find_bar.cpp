#include "find_bar.h"

#include "goto_line_bar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>
#include <QStyle>
#include <QVBoxLayout>
#include <algorithm>

namespace Kites
{
namespace
{
QToolButton *makeToggle(QWidget *parent, const QString &label, const QString &objectName,
                        const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(label);
    button->setCheckable(true);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus); // the field keeps the caret
    return button;
}

QToolButton *makeButton(QWidget *parent, const QString &label, const QString &objectName,
                        const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(label);
    button->setToolTip(tip);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}
} // namespace

FindBar::FindBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("findBar");
    // Opaque, so the text behind it does not show through the overlay.
    setAutoFillBackground(true);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(4);

    auto *findRow = new QHBoxLayout;
    findRow->setContentsMargins(0, 0, 0, 0);
    findRow->setSpacing(4);

    m_find = new QLineEdit(this);
    m_find->setObjectName("findInput");
    m_find->setPlaceholderText(tr("Find"));
    m_find->setClearButtonEnabled(false);
    m_find->installEventFilter(this);
    findRow->addWidget(m_find, 1);

    m_caseButton  = makeToggle(this, "Aa", "findCaseButton", tr("Match case  (Alt+C)"));
    m_wordButton  = makeToggle(this, "ab", "findWordButton", tr("Whole word  (Alt+W)"));
    m_regexButton = makeToggle(this, ".*", "findRegexButton", tr("Regular expression  (Alt+R)"));
    for (QToolButton *button : {m_caseButton, m_wordButton, m_regexButton})
    {
        findRow->addWidget(button);
        connect(button, &QToolButton::toggled, this, &FindBar::optionsChanged);
    }

    m_status = new QLabel(this);
    m_status->setObjectName("findStatus");
    findRow->addWidget(m_status);

    QToolButton *previous = makeButton(this, "<", "findPreviousButton", tr("Previous  (Shift+F3)"));
    QToolButton *next     = makeButton(this, ">", "findNextButton", tr("Next  (F3)"));
    QToolButton *close    = makeButton(this, "x", "findCloseButton", tr("Close  (Esc)"));
    findRow->addWidget(previous);
    findRow->addWidget(next);
    findRow->addWidget(close);
    connect(previous, &QToolButton::clicked, this, &FindBar::findPrevious);
    connect(next, &QToolButton::clicked, this, &FindBar::findNext);
    connect(close, &QToolButton::clicked, this, &FindBar::closeRequested);

    column->addLayout(findRow);

    // ── the replace row, hidden until Ctrl+H
    m_replaceRow = new QWidget(this);
    m_replaceRow->setObjectName("findReplaceRow");
    auto *replaceRow = new QHBoxLayout(m_replaceRow);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(4);

    m_replace = new QLineEdit(m_replaceRow);
    m_replace->setObjectName("replaceInput");
    m_replace->setPlaceholderText(tr("Replace with"));
    m_replace->installEventFilter(this);
    replaceRow->addWidget(m_replace, 1);

    m_replaceOne = makeButton(m_replaceRow, tr("Replace"), "replaceOneButton",
                              tr("Replace this match  (Enter)"));
    m_replaceAll = makeButton(m_replaceRow, tr("All"), "replaceAllButton",
                              tr("Replace every match  (Ctrl+Alt+Enter)"));
    replaceRow->addWidget(m_replaceOne);
    replaceRow->addWidget(m_replaceAll);
    connect(m_replaceOne, &QToolButton::clicked, this, &FindBar::replaceOne);
    connect(m_replaceAll, &QToolButton::clicked, this, &FindBar::replaceAll);

    column->addWidget(m_replaceRow);
    m_replaceRow->hide();

    connect(m_find, &QLineEdit::textChanged, this, &FindBar::patternChanged);

    hide();
}

FindEngine::FindOptions FindBar::options() const
{
    FindEngine::FindOptions options;
    options.caseSensitive = m_caseButton->isChecked();
    options.wholeWord     = m_wordButton->isChecked();
    options.regex         = m_regexButton->isChecked();
    return options;
}

QString FindBar::pattern() const
{
    return m_find->text();
}

QString FindBar::replacement() const
{
    return m_replace->text();
}

void FindBar::setPattern(const QString &pattern)
{
    m_find->setText(pattern);
}

void FindBar::focusFind()
{
    m_find->setFocus();
    m_find->selectAll();
}

void FindBar::focusReplace()
{
    m_replace->setFocus();
    m_replace->selectAll();
}

void FindBar::setReplaceVisible(bool visible)
{
    m_replaceRow->setVisible(visible);
    adjustSize();
}

bool FindBar::isReplaceVisible() const
{
    return !m_replaceRow->isHidden();
}

void FindBar::setStatus(const QString &text, bool isError)
{
    m_status->setText(text);
    // A property rather than a colour, so the stylesheet decides what an
    // error looks like in each theme.
    m_status->setProperty("findError", isError);
    m_status->style()->unpolish(m_status);
    m_status->style()->polish(m_status);
    m_find->setProperty("findError", isError);
    m_find->style()->unpolish(m_find);
    m_find->style()->polish(m_find);
}

QString FindBar::status() const
{
    return m_status->text();
}

void FindBar::setReplaceEnabled(bool enabled)
{
    m_replace->setEnabled(enabled);
    m_replaceOne->setEnabled(enabled);
    m_replaceAll->setEnabled(enabled);
}

void FindBar::reposition(const QWidget *viewport)
{
    // Shared with the goto bar, so the two overlays cannot drift apart.
    repositionOverlay(this, viewport);
}

bool FindBar::handleBarKey(QKeyEvent *event)
{
    const Qt::KeyboardModifiers mods = event->modifiers();
    const int                   key  = event->key();
    const bool isReturn = key == Qt::Key_Return || key == Qt::Key_Enter;

    if (key == Qt::Key_Escape && mods == Qt::NoModifier)
    {
        emit closeRequested();
        return true;
    }
    if (isReturn && mods == (Qt::ControlModifier | Qt::AltModifier))
    {
        emit replaceAll();
        return true;
    }
    if (isReturn && mods == Qt::ShiftModifier)
    {
        emit findPrevious();
        return true;
    }
    if (isReturn && mods == Qt::NoModifier)
    {
        // Enter in the replace field replaces; in the find field it moves on.
        if (m_replace->hasFocus() && m_replace->isEnabled())
            emit replaceOne();
        else
            emit findNext();
        return true;
    }
    if (key == Qt::Key_F3)
    {
        if (mods == Qt::ShiftModifier)
            emit findPrevious();
        else
            emit findNext();
        return true;
    }
    // Alt+C / Alt+W / Alt+R, which in the editor would type a letter.
    if (mods == Qt::AltModifier)
    {
        QToolButton *toggle = key == Qt::Key_C   ? m_caseButton
                              : key == Qt::Key_W ? m_wordButton
                              : key == Qt::Key_R ? m_regexButton
                                                 : nullptr;
        if (toggle)
        {
            toggle->toggle();
            return true;
        }
    }
    return false;
}

bool FindBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && handleBarKey(static_cast<QKeyEvent *>(event)))
        return true;
    return QWidget::eventFilter(watched, event);
}

void FindBar::keyPressEvent(QKeyEvent *event)
{
    if (handleBarKey(event))
    {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
} // namespace Kites
