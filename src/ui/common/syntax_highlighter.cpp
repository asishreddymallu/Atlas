#include "syntax_highlighter.h"
#include <QFont>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonArray>

namespace Atlas
{
namespace
{
    std::map<std::string, SyntaxStyle> stringToSyntaxStyle = {
    #define X(enumValue, stringValue) {stringValue, SyntaxStyle::enumValue},
        ENUM_FROM_STRING_LIST
        //magic!!
    #undef X
    };

}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent)
{
    setHighlightingRules();
    connect(&ThemeManager::getInstance(), &ThemeManager::editorThemeChangedSignal,
    this, &SyntaxHighlighter::themeChangedSlot);
}

QRegularExpression SyntaxHighlighter::commentPattern()
{
    // A string FIRST, so a '#' or a ';' inside one is eaten by the string
    // alternative and cannot open a comment. The lexer behaves the same
    // way: `.string "a # b"` measures as DIRECTIVE(string) STRING(a # b).
    // Only double quotes - the lexer has no single-quoted strings.
    return QRegularExpression(R"((?<string>"[^"\n]*")|[#;][^\n]*)");
}

void SyntaxHighlighter::setHighlightingRules()
{
    
    setInstructionHighlightingRules();
    setRegisterHighlightingRules();

    // setting rules for comments. BOTH '#' and ';' - the lexer takes both
    // (getNextToken: `current_char == '#' || current_char == ';'`) and the
    // highlighter knew only about '#'. See commentPattern() for why a
    // string is in the same pattern.
    m_highlightRules.append({commentPattern(), SyntaxStyle::Comment});

    // setting rules for labels
    m_highlightRules.append({QRegularExpression(R"(^\s*[A-Za-z_][A-Za-z0-9_.$]*:$)"), SyntaxStyle::Label});
    // for number literals
    m_highlightRules.append({QRegularExpression(R"((?<!\w)(?:-?(?:0x[0-9A-Fa-f]+|0b[01]+|\d+))\b)"), 
                            SyntaxStyle::NumberLiteral});
}

void SyntaxHighlighter::setInstructionHighlightingRules()
{
    QFile instructionFile(":/instructions/instructions.json");
    // yeah this instructions/instructions.json
    //is ugly but my OCD wil not allow me to put 
    // it outside of a folder
    if (!instructionFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "Failed to open instruction JSON file:" << instructionFile.errorString();
        return;
    }

    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(instructionFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        qWarning() << "Failed to parse instruction JSON:" << parseError.errorString();
        return;
    }

    QJsonObject jsonObj = jsonDoc.object();
    for (auto it = jsonObj.begin(); it != jsonObj.end(); ++it)
    {
        const QString category = it.key();
        const QJsonArray instructions = it.value().toArray();

        QStringList instructionNames;

        for (const auto& value : instructions)
        {
            instructionNames << QRegularExpression::escape(
                                    value.toString());
        }

        if (instructionNames.isEmpty())
        {
            continue;
        }

        QString regexPattern =
            "\\b(" + instructionNames.join("|") + ")\\b";

        HighlightRule rule;

        rule.pattern = QRegularExpression(regexPattern);

        rule.style = stringToSyntaxStyle[category.toStdString()];

        m_highlightRules.push_back(std::move(rule));
    }
}

void SyntaxHighlighter::setRegisterHighlightingRules()
{
    const QString registerPattern {
        "\\b(?:"
        "x(?:[0-9]|[12][0-9]|3[01])|"
        "f(?:[0-9]|[12][0-9]|3[01])|"
        "zero|ra|sp|gp|tp|"
        "t[0-6]|"
        "s(?:[0-9]|1[01])|"
        "a[0-7]"
        ")\\b"
    };

    HighlightRule rule;
    rule.pattern = QRegularExpression(registerPattern);
    rule.style = SyntaxStyle::Register;
    m_highlightRules.push_back(std::move(rule));
}

void SyntaxHighlighter::highlightBlock(const QString &text)
{
    for (const HighlightRule &rule : m_highlightRules)
    {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext())
        {
            QRegularExpressionMatch match = matchIterator.next();
            // The comment pattern also matches strings, only so that it can
            // step over them; those matches are not comments and must not
            // be coloured. There is no String style to give them - adding
            // one would mean a new theme token.
            if (match.capturedStart(QStringLiteral("string")) >= 0)
                continue;
            setFormat(match.capturedStart(), match.capturedLength(), 
            ThemeManager::getInstance().getSyntaxFormat(rule.style));
        }
    }
}

void SyntaxHighlighter::themeChangedSlot([[maybe_unused]] const QString &themeId)
{
    rehighlight();
}

} // namespace Atlas
