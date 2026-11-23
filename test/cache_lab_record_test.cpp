/**
 * @file cache_lab_record_test.cpp
 * @brief The lab record, schema "atlas.lab-record/1": the Qt-free builder,
 *        the capture from live caches, and the record the Cache tab exports.
 */
#include "cache_lab_test_support.h"
#include "cache_window_harness.h"

#include "cache_lab/lab_record.h"
#include "cache_lab/lab_record_capture.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

using namespace Atlas;
using namespace Atlas::test_support;
using namespace Atlas::test_support::cache_lab;

namespace
{
QJsonObject parse(const std::string &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(json), &error);
    EXPECT_EQ(error.error, QJsonParseError::NoError) << error.errorString().toStdString() << "\n"
                                                    << json;
    return doc.object();
}

LabRecordInput sampleInput()
{
    LabRecordInput in;
    in.atlasVersion        = "v-test";
    in.timestamp           = "2026-11-24T10:00:00Z";
    in.programName         = "odd \"name\"\n.s";
    in.programSha256       = std::string(64, 'a');
    in.processor           = "Single-cycle";
    in.cycles              = 42;
    in.instructionsRetired = 40;
    in.completed           = true;
    LabRecordCache l1;
    l1.level = "l1";
    l1.setsExponent = 3; l1.waysExponent = 1; l1.wordsExponent = 2;
    l1.sets = 8; l1.ways = 2; l1.lineBytes = 16; l1.totalBytes = 256;
    l1.writeHit = "write-through"; l1.writeMiss = "no-write-allocate"; l1.replacement = "lru";
    l1.hits = 3; l1.misses = 1; l1.evictions = 0; l1.writeBacks = 0;
    in.caches = {l1};
    return in;
}
} // namespace

TEST(CacheLabRecord, TheBuilderWritesEveryFieldOfTheSchema)
{
    const QJsonObject r = parse(buildLabRecordJson(sampleInput()));
    EXPECT_EQ(r["schema"].toString(), "atlas.lab-record/1");
    EXPECT_EQ(r["atlasVersion"].toString(), "v-test");
    EXPECT_EQ(r["timestamp"].toString(), "2026-11-24T10:00:00Z");
    EXPECT_EQ(r["program"]["name"].toString(), "odd \"name\"\n.s") << "escaped and read back";
    EXPECT_EQ(r["program"]["sha256"].toString(), QString(64, 'a'));
    EXPECT_EQ(r["processor"].toString(), "Single-cycle");
    EXPECT_TRUE(r["preset"].isNull()) << "no preset: null";
    EXPECT_EQ(r["cycles"].toInteger(), 42);
    EXPECT_EQ(r["instructionsRetired"].toInteger(), 40);
    EXPECT_TRUE(r["completed"].toBool());

    const QJsonArray caches = r["caches"].toArray();
    ASSERT_EQ(caches.size(), 1);
    const QJsonObject c = caches[0].toObject();
    EXPECT_EQ(c["level"].toString(), "l1");
    const QJsonObject g = c["geometry"].toObject();
    EXPECT_EQ(g["setsExponent"].toInt(), 3);
    EXPECT_EQ(g["waysExponent"].toInt(), 1);
    EXPECT_EQ(g["wordsPerLineExponent"].toInt(), 2);
    EXPECT_EQ(g["sets"].toInt(), 8);
    EXPECT_EQ(g["ways"].toInt(), 2);
    EXPECT_EQ(g["lineBytes"].toInt(), 16);
    EXPECT_EQ(g["totalBytes"].toInt(), 256);
    EXPECT_EQ(c["policies"]["writeHit"].toString(), "write-through");
    EXPECT_EQ(c["policies"]["writeMiss"].toString(), "no-write-allocate");
    EXPECT_EQ(c["policies"]["replacement"].toString(), "lru");
    const QJsonObject res = c["results"].toObject();
    EXPECT_EQ(res["accesses"].toInteger(), 4);
    EXPECT_EQ(res["hits"].toInteger(), 3);
    EXPECT_EQ(res["misses"].toInteger(), 1);
    EXPECT_EQ(res["evictions"].toInteger(), 0);
    EXPECT_EQ(res["writeBacks"].toInteger(), 0);
    EXPECT_DOUBLE_EQ(res["hitRate"].toDouble(), 0.75);
}

TEST(CacheLabRecord, APresetIsNamedAndAStoppedRunIsNotCompleted)
{
    LabRecordInput in = sampleInput();
    in.preset         = "2w256-lru";
    in.completed      = false;
    in.caches.front().hits = 0;
    in.caches.front().misses = 0;
    const QJsonObject r = parse(buildLabRecordJson(in));
    EXPECT_EQ(r["preset"].toString(), "2w256-lru");
    EXPECT_FALSE(r["completed"].toBool());
    EXPECT_DOUBLE_EQ(r["caches"][0]["results"]["hitRate"].toDouble(), 0.0) << "no accesses";
}

TEST(CacheLabRecord, NoCachesIsStillValidJson)
{
    LabRecordInput in = sampleInput();
    in.caches.clear();
    EXPECT_TRUE(parse(buildLabRecordJson(in))["caches"].toArray().isEmpty());
}

TEST(CacheLabRecord, TheVersionIsCompiledIn)
{
    EXPECT_FALSE(atlasVersion().empty());
}

TEST(CacheLabRecord, MatmulIjkOnTwoWay256LruGivesThePinnedNumbers)
{
    // The pinned 2w256 LRU matmul_ijk result (cache_tab_gui_test.cpp):
    // 827 hits, 389 misses, 181 evictions.
    const LabRun run = runOnRvss("matmul_ijk", twoWay256Lru(), false);
    LabRecordInput in = sampleInput();
    in.caches = {captureLabRecordCache("l1", *run.l1())};
    const QJsonObject l1 = parse(buildLabRecordJson(in))["caches"][0].toObject();
    EXPECT_EQ(l1["geometry"]["sets"].toInt(), 8);
    EXPECT_EQ(l1["geometry"]["ways"].toInt(), 2);
    EXPECT_EQ(l1["geometry"]["lineBytes"].toInt(), 16);
    EXPECT_EQ(l1["geometry"]["totalBytes"].toInt(), 256);
    EXPECT_EQ(l1["geometry"]["setsExponent"].toInt(), 3);
    EXPECT_EQ(l1["geometry"]["waysExponent"].toInt(), 1);
    EXPECT_EQ(l1["geometry"]["wordsPerLineExponent"].toInt(), 2);
    EXPECT_EQ(l1["policies"]["replacement"].toString(), "lru");
    const QJsonObject res = l1["results"].toObject();
    EXPECT_EQ(res["hits"].toInteger(), 827);
    EXPECT_EQ(res["misses"].toInteger(), 389);
    EXPECT_EQ(res["evictions"].toInteger(), 181);
    EXPECT_EQ(res["accesses"].toInteger(), 1216);
    EXPECT_DOUBLE_EQ(res["hitRate"].toDouble(), 0.680099);
}

TEST(CacheLabRecord, TheSha256IsTheSourcesOwn)
{
    // FIPS 180-2 test vector.
    EXPECT_EQ(sha256Hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(CacheLabRecord, TheCacheTabRecordDescribesTheRun)
{
    CacheWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RVSS);
    ASSERT_TRUE(w.cacheTab->applyLabPreset(QStringLiteral("2w256-lru")));
    EXPECT_FALSE(w.cacheTab->labRecordInput().completed) << "nothing has run yet";

    const QString source = readCacheLabExample(QStringLiteral("matmul_ijk"));
    ASSERT_TRUE(w.runSource(source));
    const QJsonObject r = parse(buildLabRecordJson(w.cacheTab->labRecordInput()));
    EXPECT_EQ(r["preset"].toString(), "2w256-lru");
    EXPECT_TRUE(r["completed"].toBool());
    EXPECT_EQ(r["program"]["sha256"].toString(),
              QString::fromLatin1(
                  QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex()));
    EXPECT_GT(r["cycles"].toInteger(), 0);
    EXPECT_GT(r["instructionsRetired"].toInteger(), 0);
    EXPECT_FALSE(r["processor"].toString().isEmpty());

    const QJsonArray caches = r["caches"].toArray();
    ASSERT_EQ(caches.size(), 3);
    EXPECT_EQ(caches[0]["level"].toString(), "instruction");
    EXPECT_EQ(caches[1]["level"].toString(), "l1");
    EXPECT_EQ(caches[2]["level"].toString(), "l2");
    EXPECT_EQ(caches[1]["results"]["hits"].toInteger(), 827);
    EXPECT_EQ(caches[1]["results"]["misses"].toInteger(), 389);
    EXPECT_EQ(caches[1]["results"]["evictions"].toInteger(), 181);
}

TEST(CacheLabRecord, ARunStoppedEarlyIsNotCompleted)
{
    CacheWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RVSS);
    w.editor->setPlainText(readCacheLabExample(QStringLiteral("matmul_ijk")));
    QApplication::processEvents();
    w.manager->setStepDelay(5); // slow enough to stop in the middle
    w.run->trigger();
    ASSERT_TRUE(waitUntilWindow([&w] { return w.run->text() != "Run"; }, 5000));
    spinWindow(100);
    w.run->trigger(); // Run reads "Stop" while running
    ASSERT_TRUE(waitUntilWindow([&w] { return w.debug->isEnabled() && w.run->text() == "Run"; }));
    EXPECT_FALSE(w.cacheTab->labRecordInput().completed);
}
