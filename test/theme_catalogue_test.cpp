/**
 * @file theme_catalogue_test.cpp
 * @brief T8: the ten built-in themes (four house themes, six VS Code ones).
 *
 * kites_test links no Qt resources, so the themes are read from the source
 * tree (ctest runs in test/) and, where ThemeManager itself is under test,
 * installed into this test's own settings sandbox (test_main.cpp; ctest
 * runs every test in a fresh process).
 *
 * Thresholds:
 *   - main text (foreground, icons) 4.5:1 on background, surface, current
 *     line and selection;
 *   - secondary text 3:1 on background, surface and current line;
 *   - control borders (buttons, fields, combo and spin boxes) 3:1 on
 *     background and surface. The faint separator line (`border`) is
 *     decorative and exempt (WCAG 1.4.11), as the user decided in T8;
 *   - timing grid: every stage tint visibly off the surface, the text on it
 *     dark on a light theme and light on a dark one, the flush cross 3:1 and
 *     the bubble hatching 1.5:1 on every tint, and the five stage colours
 *     apart from each other.
 */
#include "processor/processor_manager.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/theme/theme_manager.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <set>

using namespace Atlas;

namespace
{
const QString kDir = QStringLiteral("../resources/themes");

const QStringList kNewThemes = {"cobalt2-theme-official", "night-owl", "monokai-dark-soda",
                                "remedy", "zed-theme", "cpp-light-theme"};
const QStringList kHouseThemes = {"kites-dark", "kites-light", "tokyo-night", "tokyo-night-storm"};

struct Theme
{
    QString     file, id, name, appearance;
    QJsonObject colors;
    QColor      c(const char *key) const { return QColor(colors[key].toString()); }
    bool        dark() const { return appearance != "light"; }
    bool        isNew() const { return kNewThemes.contains(id); }
};

QList<Theme> themes()
{
    QList<Theme> out;
    QDir dir(kDir);
    for (const QString &name : dir.entryList({"*.json"}, QDir::Files))
    {
        QFile f(dir.filePath(name));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        out.push_back({name, root["id"].toString(), root["name"].toString(), root["appearance"].toString(),
                       root["colors"].toObject()});
    }
    return out;
}

/// CIE76 colour difference (L*a*b*, D65).
double deltaE(const QColor &a, const QColor &b)
{
    auto lab = [](const QColor &c) {
        auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        const double r = lin(c.redF()), g = lin(c.greenF()), bl = lin(c.blueF());
        const double x = (0.4124 * r + 0.3576 * g + 0.1805 * bl) / 0.95047;
        const double y = 0.2126 * r + 0.7152 * g + 0.0722 * bl;
        const double z = (0.0193 * r + 0.1192 * g + 0.9505 * bl) / 1.08883;
        auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116; };
        return std::array<double, 3>{116 * f(y) - 16, 500 * (f(x) - f(y)), 200 * (f(y) - f(z))};
    };
    const auto p = lab(a), q = lab(b);
    return std::sqrt((p[0] - q[0]) * (p[0] - q[0]) + (p[1] - q[1]) * (p[1] - q[1]) + (p[2] - q[2]) * (p[2] - q[2]));
}

double luminance(const QColor &c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

QColor blendOver(const QColor &top, const QColor &bottom, double alpha)
{
    return QColor::fromRgbF(top.redF() * alpha + bottom.redF() * (1 - alpha),
                            top.greenF() * alpha + bottom.greenF() * (1 - alpha),
                            top.blueF() * alpha + bottom.blueF() * (1 - alpha));
}

QString text(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

/// Every built-in theme in this test's ThemeManager (its user-theme folder).
void installAll()
{
    for (const Theme &t : themes())
    {
        QString error;
        ASSERT_TRUE(ThemeManager::getInstance().installTheme(QDir(kDir).filePath(t.file), error))
            << t.file.toStdString() << ": " << error.toStdString();
    }
}
} // namespace

TEST(ThemeCatalogue, TenThemesShipWithTheApp)
{
    QStringList ids;
    for (const Theme &t : themes())
        ids << t.id;
    for (const QString &id : kHouseThemes + kNewThemes)
        EXPECT_TRUE(ids.contains(id)) << id.toStdString();
    EXPECT_EQ(ids.size(), 10);
    // Each is compiled in.
    const QString qrc = text(kDir + "/themes.qrc");
    for (const Theme &t : themes())
        EXPECT_TRUE(qrc.contains("<file>" + t.file + "</file>")) << t.file.toStdString();
    // The names are the VS Code themes' own.
    QStringList names;
    for (const Theme &t : themes())
        names << t.name;
    for (const char *n : {"Cobalt2 Theme Official", "Night Owl", "Monokai Dark Soda", "Remedy", "Zed Theme",
                          "C++ Light Theme"})
        EXPECT_TRUE(names.contains(n)) << n;
}

TEST(ThemeCatalogue, EveryThemeDefinesEveryToken)
{
    const char *core[] = {"background", "surface", "border", "foreground", "mutedForeground", "accent",
                          "selection", "currentLine", "iconColor", "cacheHit", "cacheMiss", "success",
                          "warning", "error", "info"};
    const char *syntax[] = {"r32Instruction", "r64Instruction", "mInstruction", "fInstruction",
                            "dInstruction", "pseudoInstruction", "register", "comment", "label",
                            "numberLiteral"};
    const char *diagram[] = {"background", "component", "componentBorder", "label", "wire", "wireActive"};
    for (const Theme &t : themes())
    {
        const std::string n = t.id.toStdString();
        EXPECT_FALSE(t.id.isEmpty() || t.name.isEmpty()) << t.file.toStdString();
        EXPECT_TRUE(t.appearance == "light" || t.appearance == "dark") << n;
        for (const char *k : core)
            EXPECT_TRUE(t.c(k).isValid()) << n << " " << k;
        for (const char *k : syntax)
            EXPECT_TRUE(QColor(t.colors["syntax"].toObject()[k].toString()).isValid()) << n << " syntax." << k;
        if (!t.isNew())
            continue; // the house themes keep their T7 look: no control border or diagram tokens
        EXPECT_TRUE(t.c("controlBorder").isValid()) << n;
        for (const char *k : diagram)
            EXPECT_TRUE(QColor(t.colors["diagram"].toObject()[k].toString()).isValid()) << n << " diagram." << k;
    }
}

TEST(ThemeCatalogue, TemplateLeavesNoUnreplacedToken)
{
    const QString tmpl = text(kDir + "/template.qss");
    ASSERT_FALSE(tmpl.isEmpty());
    ASSERT_TRUE(tmpl.contains("{{controlBorder}}"));
    for (const Theme &t : themes())
    {
        const QString qss = ThemeManager::renderStylesheet(tmpl, t.colors);
        EXPECT_FALSE(qss.contains("{{")) << t.id.toStdString() << ": "
                                         << qss.mid(qss.indexOf("{{"), 30).toStdString();
    }
}

TEST(ThemeCatalogue, OldThemesRenderTheSameStylesheet)
{
    // controlBorder falls back to border: a house theme's QSS is the one it
    // had before the token existed.
    const QString tmpl = text(kDir + "/template.qss");
    QString       before = tmpl;
    before.replace("{{controlBorder}}", "{{border}}");
    for (const Theme &t : themes())
        if (!t.isNew())
            EXPECT_EQ(ThemeManager::renderStylesheet(tmpl, t.colors), ThemeManager::renderStylesheet(before, t.colors))
                << t.id.toStdString();
}

TEST(ThemeCatalogue, MainTextMeetsFourAndAHalf)
{
    for (const Theme &t : themes())
        for (const char *ground : {"background", "surface", "currentLine", "selection"})
        {
            EXPECT_GE(contrastRatio(t.c("foreground"), t.c(ground)), 4.5) << t.id.toStdString() << " on " << ground;
            if (QString(ground) != "selection")
                EXPECT_GE(contrastRatio(t.c("iconColor"), t.c(ground)), 4.5) << t.id.toStdString() << " icons on "
                                                                             << ground;
        }
}

TEST(ThemeCatalogue, SecondaryTextAndControlBordersMeetThree)
{
    for (const Theme &t : themes())
    {
        for (const char *ground : {"background", "surface", "currentLine"})
            EXPECT_GE(contrastRatio(t.c("mutedForeground"), t.c(ground)), 3.0)
                << t.id.toStdString() << " secondary text on " << ground;
        if (!t.isNew())
        {
            // House themes draw controls with their faint `border` (1.1-1.4:1);
            // they are kept byte-identical to T7 (a known exception, T8).
            EXPECT_FALSE(t.colors.contains("controlBorder")) << t.id.toStdString();
            continue;
        }
        for (const char *ground : {"background", "surface"})
            EXPECT_GE(contrastRatio(t.c("controlBorder"), t.c(ground)), 3.0)
                << t.id.toStdString() << " control border on " << ground;
        EXPECT_GE(contrastRatio(t.c("accent"), t.c("background")), 3.0) << t.id.toStdString();
    }
}

TEST(ThemeCatalogue, NewThemesSyntaxColoursMeetThree)
{
    for (const Theme &t : themes())
    {
        if (!t.isNew())
            continue;
        const QJsonObject syntax = t.colors["syntax"].toObject();
        for (auto it = syntax.begin(); it != syntax.end(); ++it)
            for (const char *ground : {"surface", "currentLine"})
                EXPECT_GE(contrastRatio(QColor(it.value().toString()), t.c(ground)), 3.0)
                    << t.id.toStdString() << " syntax." << it.key().toStdString() << " on " << ground;
    }
}

TEST(ThemeCatalogue, TimingGridCellsAreDistinctAndTheirMarksVisible)
{
    for (const Theme &t : themes())
    {
        const std::string n       = t.id.toStdString();
        const QColor      surface = t.c("surface");
        const QColor      text    = t.c("foreground");
        QList<QColor>     accents, fills;
        for (int s = 0; s < 5; ++s)
        {
            const QColor           accent(t.colors[timingStageToken(static_cast<TraceStage>(s))].toString());
            const TimingCellColors c = timingCellColorsFrom(accent, surface, text);
            accents << accent;
            fills << c.fill;
            EXPECT_GE(deltaE(c.fill, surface), 10.0) << n << " stage " << s << " tint vs surface";
            EXPECT_GE(contrastRatio(c.text, c.fill), 4.5) << n << " stage " << s;
            // Dark text on pale cells (light themes), light on dark cells.
            EXPECT_EQ(luminance(c.text) < luminance(c.fill), !t.dark()) << n << " stage " << s;
            // The flush cross and the bubble hatching, as TimingGridView draws them.
            EXPECT_GE(contrastRatio(t.c("error"), c.fill), 3.0) << n << " flush cross on stage " << s;
            EXPECT_GE(contrastRatio(blendOver(t.c("warning"), surface, 0.55), c.fill), 1.5)
                << n << " bubble hatching on stage " << s;
        }
        // The stage colours (the cells' accent bars) apart from each other.
        for (int a = 0; a < 5; ++a)
            for (int b = a + 1; b < 5; ++b)
            {
                // Known (T8): three house themes give ID and MEM the same
                // colour (success == cacheHit); the cell labels tell them apart.
                const bool knownSame = !t.isNew() && a == 1 && b == 3 && t.id != "kites-dark";
                if (knownSame)
                    EXPECT_EQ(accents[a], accents[b]) << n << " (the known exception no longer holds: update it)";
                else
                    EXPECT_GE(deltaE(accents[a], accents[b]), 15.0) << n << " stages " << a << " and " << b;
            }
    }
}

TEST(ThemeCatalogue, DiagramColoursAreReadable)
{
    for (const Theme &t : themes())
    {
        if (!t.colors.contains("diagram"))
            continue; // house themes keep the black diagram
        const QJsonObject d = t.colors["diagram"].toObject();
        auto c = [&](const char *k) { return QColor(d[k].toString()); };
        const std::string n = t.id.toStdString();
        EXPECT_GE(contrastRatio(c("label"), c("component")), 4.5) << n;
        EXPECT_GE(contrastRatio(c("wireActive"), c("background")), 4.5) << n;
        EXPECT_GE(contrastRatio(c("wire"), c("background")), 2.0) << n;
        EXPECT_GE(deltaE(c("wire"), c("wireActive")), 15.0) << n << " a lit wire must not look idle";
    }
}

// The theme lists: dark themes first, then light, each by name.
TEST(ThemeCatalogue, ThemesAreListedDarkThenLightByName)
{
    installAll();
    const QVector<ThemeData> list = ThemeManager::getInstance().availableThemes();
    ASSERT_EQ(list.size(), 10);
    bool seenLight = false;
    for (int i = 0; i < list.size(); ++i)
    {
        const bool light = list[i].appearance == ThemeAppearance::Light;
        EXPECT_FALSE(seenLight && !light) << "dark theme after a light one: " << list[i].id.toStdString();
        seenLight = seenLight || light;
        if (i > 0 && (list[i - 1].appearance == list[i].appearance))
            EXPECT_LT(QString::compare(list[i - 1].name, list[i].name, Qt::CaseInsensitive), 0)
                << list[i - 1].name.toStdString() << " / " << list[i].name.toStdString();
    }
}

TEST(ThemeCatalogue, PreferencesAppliesSavesAndRestores)
{
    installAll();
    SettingsDialog dialog;
    QComboBox     *global = nullptr;
    for (QComboBox *box : dialog.findChildren<QComboBox *>())
        if (box->findData("night-owl") >= 0 && box->findData(QString()) < 0)
            global = box;
    ASSERT_NE(global, nullptr);
    // The two groups are split by a separator.
    int separators = 0;
    for (int i = 0; i < global->count(); ++i)
        if (global->itemData(i, Qt::AccessibleDescriptionRole).toString() == "separator")
            ++separators;
    EXPECT_EQ(separators, 1);

    QStringList ids;
    for (const ThemeData &t : ThemeManager::getInstance().availableThemes())
        ids << t.id;
    for (const QString &id : kNewThemes)
    {
        const int index = global->findData(id);
        ASSERT_GE(index, 0) << id.toStdString();
        global->setCurrentIndex(index);
        QApplication::processEvents();
        EXPECT_EQ(ThemeManager::getInstance().currentThemeId(), id);
        // Saved in Kites.conf, as a restart reads it.
        QSettings saved("Kites", "Kites");
        saved.sync();
        EXPECT_EQ(saved.value("theme/id").toString(), id);
        // ... and a restart restores it.
        EXPECT_EQ(ThemeManager::startupThemeId(saved.value("theme/id").toString(), ids), id);
    }
    // Saved names from before T8 still work; the default is unchanged.
    for (const QString &old : kHouseThemes)
        EXPECT_EQ(ThemeManager::startupThemeId(old, ids), old);
    EXPECT_EQ(ThemeManager::startupThemeId(QString(), ids), "tokyo-night");
    EXPECT_EQ(ThemeManager::startupThemeId("no-such-theme", ids), "tokyo-night");
}
