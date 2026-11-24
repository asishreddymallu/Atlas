/**
 * @file cli_smoke_test.cpp
 * @brief atlas-cli runs (known issues 87 and 86): the built executable,
 *        started as a user would start it, with no display.
 *
 * 87: the CLI made a QCoreApplication, and every processor builds a
 * QGraphicsScene, so it crashed before reading a command. 86: a pipelined
 * Run stopped on the breakpoint LoadProgram plants past the last
 * instruction, so a batch run on a five-stage processor never ended.
 * The cycle counts are the ones docs/scheduler.md and the scheduler tests
 * pin for this program (11 on rv5-h-f, 14 on rv5-h-nf).
 */
#include <gtest/gtest.h>

#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace
{
struct CliResult
{
    int     exitCode{-1};
    bool    finished{false};
    QString out;
};

/// Runs atlas-cli in a fresh directory (its vm_state/ goes there), with no
/// display and no QT_QPA_PLATFORM, so the CLI must choose offscreen itself.
CliResult runCli(const QStringList &args, const QByteArray &stdinText = {})
{
    QTemporaryDir dir;
    QProcess      p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_QPA_PLATFORM"));
    env.remove(QStringLiteral("DISPLAY"));
    env.remove(QStringLiteral("WAYLAND_DISPLAY"));
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(dir.path());
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(QStringLiteral(ATLAS_CLI_PATH), args);
    CliResult r;
    if (!p.waitForStarted(10000))
        return r;
    if (!stdinText.isEmpty())
        p.write(stdinText);
    p.closeWriteChannel();
    r.finished = p.waitForFinished(30000);
    if (!r.finished)
        p.kill();
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out      = QString::fromLocal8Bit(p.readAll());
    return r;
}

QString example()
{
    return QFileInfo(QStringLiteral("../examples/scheduler/load_use_original.s")).absoluteFilePath();
}

int cycles(const QString &out)
{
    const auto m = QRegularExpression(QStringLiteral("Cycles\\s+->\\s+(\\d+)")).match(out);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}
} // namespace

TEST(CliSmoke, ABatchRunOnRv5HfFinishesIn11Cycles)
{
    const CliResult r = runCli({example(), QStringLiteral("--vm"), QStringLiteral("rv5-h-f")});
    ASSERT_TRUE(r.finished) << r.out.toStdString();
    EXPECT_EQ(r.exitCode, 0) << r.out.toStdString();
    EXPECT_TRUE(r.out.contains(QStringLiteral("Run completed."))) << r.out.toStdString();
    EXPECT_EQ(cycles(r.out), 11) << r.out.toStdString();
}

TEST(CliSmoke, ABatchRunOnRv5HnfFinishesIn14Cycles)
{
    const CliResult r = runCli({example(), QStringLiteral("--vm"), QStringLiteral("rv5-h-nf")});
    ASSERT_TRUE(r.finished) << r.out.toStdString();
    EXPECT_EQ(r.exitCode, 0) << r.out.toStdString();
    EXPECT_EQ(cycles(r.out), 14) << r.out.toStdString();
}

TEST(CliSmoke, EveryProcessorCompletesABatchRun)
{
    for (const char *vm : {"rvss", "rv5-nh-nf", "rv5-h-nf", "rv5-nh-f", "rv5-h-f"})
    {
        const CliResult r = runCli({example(), QStringLiteral("--vm"), QString::fromLatin1(vm)});
        ASSERT_TRUE(r.finished) << vm << "\n" << r.out.toStdString();
        EXPECT_EQ(r.exitCode, 0) << vm << "\n" << r.out.toStdString();
        EXPECT_TRUE(r.out.contains(QStringLiteral("Run completed."))) << vm;
        EXPECT_GT(cycles(r.out), 0) << vm;
    }
}

TEST(CliSmoke, TheReplLoadsAndRunsOnAPipeline)
{
    const QByteArray script = "load " + example().toLocal8Bit() + " --vm rv5-h-f\nrun\ninfo\nexit\n";
    const CliResult  r      = runCli({}, script);
    ASSERT_TRUE(r.finished) << r.out.toStdString();
    EXPECT_EQ(r.exitCode, 0) << r.out.toStdString();
    EXPECT_TRUE(QRegularExpression(QStringLiteral("Cycles:\\s+11\\b")).match(r.out).hasMatch())
        << r.out.toStdString();
}
