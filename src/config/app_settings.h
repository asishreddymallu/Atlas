#pragma once
#include <QObject>
#include <QRect>
#include <QSettings>
#include <QString>

namespace Atlas
{

enum class BorderStyle
{
    None,
    Subtle,
    Full
};

/**
 * @brief AppSettings persists user-facing UI preferences (theme, font, panel
 * styling) across application restarts using QSettings.
 */
class AppSettings : public QObject
{
    Q_OBJECT
  public:
    static AppSettings &getInstance();

    QString themeId() const;
    void setThemeId(const QString &id);

    // empty string = editor follows the global theme
    QString editorThemeId() const;
    void setEditorThemeId(const QString &id);

    QString editorFontFamily() const;
    void setEditorFontFamily(const QString &family);

    int editorFontSize() const;
    void setEditorFontSize(int size);

    /// The Pipeline timing overlay's height in pixels; 0 when never set.
    int timingOverlayHeight() const;
    void setTimingOverlayHeight(int height);

    /// Whether the Problems dock was left floating, and where (final/polish).
    bool  problemsDockFloating() const;
    void  setProblemsDockFloating(bool floating);
    /// Its floating geometry; null when never floated.
    QRect problemsDockGeometry() const;
    void  setProblemsDockGeometry(const QRect &geometry);

    BorderStyle compilerOutputBorderStyle() const;
    void setCompilerOutputBorderStyle(BorderStyle style);

  signals:
    void compilerOutputBorderStyleChangedSignal(BorderStyle style);

  private:
    AppSettings();

    QSettings m_settings;
};

} // namespace Atlas
