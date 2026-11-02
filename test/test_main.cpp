#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <QApplication>
#include <QLoggingCategory>

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
    // Headless Qt: lets QGraphicsScene-based classes (circuit scenes) be
    // constructed in tests without a display server.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
