/**
 * @file version_test.cpp
 * @brief The version string: 1.0.0, plus the commit, and never a tag.
 *
 * It used to be `git describe`, which picked up the old demo-candidate tags
 * ("demo-candidate-5-22-g..."). It is now CMake's project(VERSION) plus
 * "+<short commit>" from `git rev-parse --short HEAD`.
 */
#include "cache_lab/lab_record.h"
#include "common/version.h"
#include "ui/dialogs/about_dialog.h"

#include <gtest/gtest.h>

#include <regex>

using namespace Atlas;

TEST(Version, IsOnePointZeroPointZeroWithAnOptionalCommit)
{
    const std::string v = atlasVersion();
    EXPECT_TRUE(std::regex_match(v, std::regex(R"(1\.0\.0(\+[0-9a-f]{4,40})?)"))) << v;
    EXPECT_EQ(v.find("demo"), std::string::npos) << v;
    EXPECT_EQ(v.find("candidate"), std::string::npos) << v;
}

TEST(Version, TheLabRecordAndTheAboutDialogShowTheSameVersion)
{
    LabRecordInput in;
    in.atlasVersion = atlasVersion();
    const std::string json = buildLabRecordJson(in);
    EXPECT_NE(json.find("\"atlasVersion\": \"" + atlasVersion() + "\""), std::string::npos) << json;
    EXPECT_TRUE(AboutDialog::lines().contains(QStringLiteral("Version ") +
                                              QString::fromStdString(atlasVersion())));
}
