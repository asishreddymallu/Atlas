#pragma once
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include "ui/theme/theme_manager.h"
namespace Atlas
{
class SyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
  public:
    /**
     * @brief The pattern the comment rule uses.
     *
     * Public and static so a test can ask what the rule covers without a
     * theme: kites_test loads no theme resources, getSyntaxFormat()
     * returns an empty QTextCharFormat, and Qt adds no format range for
     * one - so the rendering cannot be checked there at all.
     *
     * It matches a double-quoted string as well as a comment, in a named
     * group `string`, purely so that a '#' or a ';' INSIDE a string is
     * consumed by the string alternative and never starts a comment. The
     * caller skips any match where that group took part.
     */
    static QRegularExpression commentPattern();

    explicit SyntaxHighlighter(QTextDocument *parent = nullptr);

  protected:
    void highlightBlock(const QString &text) override;
  public slots:
    void themeChangedSlot([[maybe_unused]] const QString &themeId);
    
  private:

    void setHighlightingRules();
    void setInstructionHighlightingRules();
    void setRegisterHighlightingRules();
    struct HighlightRule
    {
      QRegularExpression pattern;
      SyntaxStyle style;
    };

    QVector<HighlightRule> m_highlightRules;

    // format rules for comments
    //  QRegularExpression m_singleLineCommentPattern;
    //  QTextCharFormat m_singleLineCommentFormat;
};
} // namespace Atlas
