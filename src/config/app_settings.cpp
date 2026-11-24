#include "app_settings.h"

namespace Atlas
{

namespace
{
constexpr const char *kThemeIdKey = "theme/id";
constexpr const char *kEditorThemeIdKey = "theme/editorId";
constexpr const char *kEditorFontFamilyKey = "editor/fontFamily";
constexpr const char *kEditorFontSizeKey = "editor/fontSize";
constexpr const char *kCompilerOutputBorderStyleKey = "compilerOutput/borderStyle";
constexpr const char *kTimingOverlayHeightKey = "timingOverlay/height";
constexpr const char *kProblemsDockFloatingKey = "problemsDock/floating";
constexpr const char *kProblemsDockGeometryKey = "problemsDock/geometry";

constexpr int kDefaultEditorFontSize = 11;
} // namespace

AppSettings::AppSettings() : m_settings("Atlas", "Atlas")
{
}

AppSettings &AppSettings::getInstance()
{
    static AppSettings instance;
    return instance;
}

QString AppSettings::themeId() const
{
    return m_settings.value(kThemeIdKey).toString();
}

void AppSettings::setThemeId(const QString &id)
{
    m_settings.setValue(kThemeIdKey, id);
}

QString AppSettings::editorThemeId() const
{
    return m_settings.value(kEditorThemeIdKey).toString();
}

void AppSettings::setEditorThemeId(const QString &id)
{
    m_settings.setValue(kEditorThemeIdKey, id);
}

QString AppSettings::editorFontFamily() const
{
    return m_settings.value(kEditorFontFamilyKey).toString();
}

void AppSettings::setEditorFontFamily(const QString &family)
{
    m_settings.setValue(kEditorFontFamilyKey, family);
}

int AppSettings::editorFontSize() const
{
    return m_settings.value(kEditorFontSizeKey, kDefaultEditorFontSize).toInt();
}

void AppSettings::setEditorFontSize(int size)
{
    m_settings.setValue(kEditorFontSizeKey, size);
}

int AppSettings::timingOverlayHeight() const
{
    return m_settings.value(kTimingOverlayHeightKey, 0).toInt();
}

void AppSettings::setTimingOverlayHeight(int height)
{
    m_settings.setValue(kTimingOverlayHeightKey, height);
}

bool AppSettings::problemsDockFloating() const
{
    return m_settings.value(kProblemsDockFloatingKey, false).toBool();
}

void AppSettings::setProblemsDockFloating(bool floating)
{
    m_settings.setValue(kProblemsDockFloatingKey, floating);
}

QRect AppSettings::problemsDockGeometry() const
{
    return m_settings.value(kProblemsDockGeometryKey, QRect()).toRect();
}

void AppSettings::setProblemsDockGeometry(const QRect &geometry)
{
    m_settings.setValue(kProblemsDockGeometryKey, geometry);
}

BorderStyle AppSettings::compilerOutputBorderStyle() const
{
    return static_cast<BorderStyle>(
        m_settings.value(kCompilerOutputBorderStyleKey, static_cast<int>(BorderStyle::Subtle)).toInt());
}

void AppSettings::setCompilerOutputBorderStyle(BorderStyle style)
{
    m_settings.setValue(kCompilerOutputBorderStyleKey, static_cast<int>(style));
    emit compilerOutputBorderStyleChangedSignal(style);
}

} // namespace Atlas
