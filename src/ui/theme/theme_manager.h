#pragma once
#include <QPalette>
#include <QWidget>
#include <QString>
#include <QColor>
#include <QVector>
#include <QJsonObject>
#include <array>
#include <QTextCharFormat>
#include "utils/to_index.h"
namespace Kites
{

enum class ThemeAppearance
{
    Light,
    Dark
};

#define ENUM_FROM_STRING_LIST \
X(R32Instruction, "R32Instruction")\
X(R64Instruction, "R64Instruction")\
X(MInstruction, "MInstruction")\
X(FInstruction, "FInstruction")\
X(DInstruction, "DInstruction")\
X(PseudoInstruction, "PseudoInstruction")\

enum class SyntaxStyle
{
    #define X(enumValue, stringValue) enumValue,
    ENUM_FROM_STRING_LIST
    //wow this magic!!
    #undef X
    Register,
    Comment,
    Label,
    NumberLiteral,
    SyntaxStyleCount
};


/// The processor diagram's colours. The defaults are the original black
/// diagram's; an invalid label colour leaves the labels' own colour.
struct DiagramColors
{
    QColor background      = Qt::black;
    QColor component       = Qt::gray;
    QColor componentBorder = Qt::white;
    QColor label;
    QColor wire            = Qt::gray;
    QColor wireActive      = Qt::yellow;
};

struct ThemeData
{
    QString id;
    QString name;
    ThemeAppearance appearance;

    QPalette palette;

    QColor iconColor;
    QColor textColor;
    QColor currentLineColor;
    QColor surfaceColor;
    QColor mutedForegroundColor;

    QColor cacheHitColor;
    QColor cacheMissColor;

    // semantic status colors, guaranteed >= 4.5:1 against background and surface
    QColor successColor;
    QColor warningColor;
    QColor errorColor;
    QColor infoColor;

    DiagramColors diagram;
    QString stylesheet; // fully rendered QSS (template + this theme's colors)

    std::array<QTextCharFormat, toIndex(SyntaxStyle::SyntaxStyleCount)> syntaxFormats;
};

/**
 * @brief ThemeManager is a singleton class that manages the application's theme
 * catalogue (built-in + user-installed JSON themes) and the active theme/palette.
 */
class ThemeManager : public QObject
{
    Q_OBJECT
public:
    ~ThemeManager() = default;

    static ThemeManager &getInstance();
    /// The template with a theme's colour tokens (and the type scale) filled in.
    static QString renderStylesheet(const QString &qssTemplate, const QJsonObject &colors);
    /// The theme to start with: the saved one if it still exists, else the default.
    static QString startupThemeId(const QString &saved, const QStringList &available);
    static constexpr const char *kDefaultThemeId = "tokyo-night";

    QString currentThemeId() const;
    void setTheme(const QString &id);
    QVector<ThemeData> availableThemes() const;

    // Editor theme: syntax colors + editor surface can differ from the global
    // UI theme (like Zed's separate UI/syntax theming). Empty id = follow global.
    QString currentEditorThemeId() const; // resolved id (never empty)
    QString configuredEditorThemeId() const; // raw setting ("" = follow global)
    void setEditorTheme(const QString &id);
    QColor getEditorBackgroundColor() const;
    QColor getEditorForegroundColor() const;
    QColor getEditorMutedForegroundColor() const;
    QColor getEditorErrorColor() const; // for squiggly diagnostics; reuses the theme's cacheMiss red

    // Semantic status colors. Use these instead of literal reds/greens so a
    // status reads the same way in every theme.
    QColor getCacheHitColor() const;
    QColor getCacheMissColor() const;
    QColor getSuccessColor() const;
    QColor getWarningColor() const;
    QColor getErrorColor() const;
    QColor getInfoColor() const;

    // Validates, copies into the user theme directory and installs the theme.
    // Returns true on success; on failure `errorOut` is populated.
    bool installTheme(const QString &sourceFilePath, QString &errorOut);

    QColor getIconColor()const;
    /// The current (global) theme's processor-diagram colours.
    DiagramColors diagramColors() const;
    /// A theme's diagram colours from its JSON "colors" (defaults when absent).
    static DiagramColors diagramColorsFrom(const QJsonObject &colors);
    QColor getTextColor()const;
    QColor getCurrentLineColor()const;
    QTextCharFormat getSyntaxFormat(SyntaxStyle style)const;
signals:
    void themeChangedSignal(const QString &themeId);
    // emitted whenever the effective editor theme changes (either directly or
    // because the global theme changed while the editor follows it)
    void editorThemeChangedSignal(const QString &themeId);

private:
    ThemeManager();

    QString userThemesDirectory() const;
    void loadBuiltinThemes();
    void loadUserThemes();
    bool parseThemeFile(const QString &path, ThemeData &themeOut, QString &errorOut) const;
    void applyTheme(const ThemeData &theme);
    QString renderStylesheet(const QJsonObject &colors) const;
    int indexOfTheme(const QString &id) const;

    const ThemeData *editorTheme() const;

    QString m_currentThemeId;
    QString m_editorThemeId; // empty = follow global
    QVector<ThemeData> m_themes;
    QString m_qssTemplate;

};
}//namespace Kites
