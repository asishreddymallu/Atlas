#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

namespace
{
/**
 * @brief Points this process's settings at a throwaway directory.
 *
 * The product reads and writes QSettings("Kites", "Kites"), which on Linux
 * is ~/.config/Kites/Kites.conf - the developer's REAL file. Several tests
 * call FontManager::setSize() and ThemeManager::setTheme(), and those write
 * straight through. That is not hypothetical: a harness sweep once left
 * editor/fontSize=3 behind, and the next run of kites_test read it and
 * EditorZoom.ItClampsAtBothEnds failed, because at 3 pt the zoom floor
 * really is 0. The suite was reading the developer's configuration.
 *
 * Two mechanisms, deliberately both:
 *
 *  - XDG_CONFIG_HOME / XDG_DATA_HOME / XDG_CACHE_HOME are redirected. This
 *    is what actually protects: QSettings' UserScope path on Unix and
 *    QStandardPaths::AppDataLocation (which ThemeManager uses for its
 *    themes directory) are both derived from them.
 *  - QSettings::setPath() pins the same directory explicitly. It is
 *    belt-and-braces: it covers a Qt build or platform where the native
 *    settings path is not taken from XDG_CONFIG_HOME, and it fails loudly
 *    in the sense that the seeded file would simply not be found.
 *
 * QStandardPaths::setTestModeEnabled() was considered and NOT used: it
 * appends a "qttest" element to the paths, which would move them away from
 * the directory seeded below, so the suite would start from an empty
 * config rather than a known one. Redirecting XDG gives the same isolation
 * AND lets the starting state be chosen.
 *
 * The directory is per-process and removed when main() returns, so two
 * runs never share state either.
 */
bool seedSandboxConfig(const QString &configHome)
{
    const QString appDir = configHome + "/Kites";
    if (!QDir().mkpath(appDir))
        return false;

    // The known starting point. Every test that depends on the configured
    // font size or theme starts from exactly this, on every machine.
    QFile file(appDir + "/Kites.conf");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream out(&file);
    out << "[editor]\n"
        << "fontSize=11\n"
        << "\n"
        << "[theme]\n"
        << "id=tokyo-night\n";
    return true;
}
} // namespace

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
    // Headless Qt: lets QGraphicsScene-based classes (circuit scenes) be
    // constructed in tests without a display server.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    // ── Settings sandbox. Before QApplication, and before anything can
    //    construct the AppSettings singleton.
    QTemporaryDir sandbox(QDir::tempPath() + "/kites-test-config-XXXXXX");
    if (!sandbox.isValid())
    {
        QTextStream(stderr) << "could not create a settings sandbox; refusing to "
                               "run against the real configuration\n";
        return 2;
    }
    const QString configHome = sandbox.path() + "/config";
    const QString dataHome   = sandbox.path() + "/data";
    const QString cacheHome  = sandbox.path() + "/cache";
    QDir().mkpath(dataHome);
    QDir().mkpath(cacheHome);
    if (!seedSandboxConfig(configHome))
    {
        QTextStream(stderr) << "could not seed the settings sandbox; refusing to "
                               "run against the real configuration\n";
        return 2;
    }
    qputenv("XDG_CONFIG_HOME", configHome.toUtf8());
    qputenv("XDG_DATA_HOME", dataHome.toUtf8());
    qputenv("XDG_CACHE_HOME", cacheHome.toUtf8());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, configHome);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, configHome);

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
