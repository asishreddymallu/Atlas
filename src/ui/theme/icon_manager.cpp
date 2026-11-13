#include "icon_manager.h"
#include "theme_manager.h"
#include <QAction>
#include <QApplication>
#include <QPixmap>
#include <QPainter>
#include <QVariant>
namespace Kites
{
namespace
{
// dynamic property carrying the Icon enum value an action was tagged with
constexpr const char *themedIconProperty = "kitesIconId";
// and the logical size its pixmap was rasterised at, so a re-tint can rebuild
// it at that size instead of silently dropping back to the default
constexpr const char *themedIconSizeProperty = "kitesIconSize";
constexpr int defaultThemedIconExtent = 20;
} // namespace

IconManager::IconManager()
{
   setUpIconPaths();
}

IconManager &IconManager::getInstance()
{
    static IconManager instance;
    return instance;
}

QIcon IconManager::getIcon(Icon icon, QSize size) const
{
    const ThemeManager &themeManager = ThemeManager::getInstance();
    QColor iconColor = themeManager.getIconColor();

    // rasterise at the screen's real pixel density; `size` stays in logical px
    const qreal devicePixelRatio = qApp ? qApp->devicePixelRatio() : 1.0;

    const QString cacheKey = QString("%1:%2x%3:%4:%5")
                                  .arg(toIndex(icon))
                                  .arg(size.width())
                                  .arg(size.height())
                                  .arg(devicePixelRatio)
                                  .arg(iconColor.name());

    auto cached = m_pixmapCache.constFind(cacheKey);
    if (cached != m_pixmapCache.constEnd())
    {
        QIcon result(cached.value());
        result.addPixmap(m_disabledCache.value(cacheKey), QIcon::Disabled, QIcon::Off);
        result.addPixmap(m_disabledCache.value(cacheKey), QIcon::Disabled, QIcon::On);
        return result;
    }

    QIcon baseIcon(m_iconPaths[toIndex(icon)]);

    auto tinted = [&](const QColor &colour)
    {
        QPixmap  pixmap = baseIcon.pixmap(size, devicePixelRatio);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), colour);
        painter.end();
        return pixmap;
    };

    const QPixmap pixmap = tinted(iconColor);

    // A disabled icon is tinted explicitly rather than left to Qt, which
    // fades the normal pixmap towards the background and lands well under
    // 3:1. mutedForeground is the theme's own "present but quiet" colour and
    // clears 4.5:1 against the window in all four themes.
    QIcon result(pixmap);
    result.addPixmap(tinted(themeManager.getEditorMutedForegroundColor()), QIcon::Disabled,
                     QIcon::Off);
    result.addPixmap(tinted(themeManager.getEditorMutedForegroundColor()), QIcon::Disabled,
                     QIcon::On);

    m_pixmapCache.insert(cacheKey, pixmap);
    m_disabledCache.insert(cacheKey, tinted(themeManager.getEditorMutedForegroundColor()));
    return result;
}

void setThemedIcon(QAction *action, Icon icon, QSize size)
{
    if (!action)
        return;
    if (size.isEmpty())
        size = QSize(defaultThemedIconExtent, defaultThemedIconExtent);
    action->setProperty(themedIconProperty, static_cast<int>(toIndex(icon)));
    action->setProperty(themedIconSizeProperty, size);
    action->setIcon(IconManager::getInstance().getIcon(icon, size));
}

QSize themedIconSizeOf(const QAction *action)
{
    const QSize fallback(defaultThemedIconExtent, defaultThemedIconExtent);
    if (!action)
        return fallback;

    const QVariant stored = action->property(themedIconSizeProperty);
    if (!stored.isValid())
        return fallback;

    const QSize size = stored.toSize();
    return size.isEmpty() ? fallback : size;
}

Icon themedIconOf(const QAction *action)
{
    if (!action)
        return Icon::IconCount;

    bool ok = false;
    const int index = action->property(themedIconProperty).toInt(&ok);
    if (!ok || index < 0 || index >= static_cast<int>(toIndex(Icon::IconCount)))
        return Icon::IconCount;

    return static_cast<Icon>(index);
}

void IconManager::setUpIconPaths()
{
    m_iconPaths[toIndex(Icon::Play)]      = ":/icons/play.svg";
    m_iconPaths[toIndex(Icon::Stop)]      = ":/icons/stop.svg";
    m_iconPaths[toIndex(Icon::Pause)]     = ":/icons/pause.svg";
    m_iconPaths[toIndex(Icon::Resume)]    = ":/icons/resume.svg";
    m_iconPaths[toIndex(Icon::Step)]      = ":/icons/step.svg";
    m_iconPaths[toIndex(Icon::Debug)]     = ":/icons/debug.svg";
    m_iconPaths[toIndex(Icon::Undo)]      = ":/icons/undo-dot.svg";
    m_iconPaths[toIndex(Icon::Redo)]      = ":/icons/redo-dot.svg";

    m_iconPaths[toIndex(Icon::Editor)]    = ":/icons/editor.svg";
    m_iconPaths[toIndex(Icon::Memory)]    = ":/icons/memory.svg";
    m_iconPaths[toIndex(Icon::Processor)] = ":/icons/processor.svg";
    m_iconPaths[toIndex(Icon::Cache)]     = ":/icons/cache.svg";
    m_iconPaths[toIndex(Icon::Compiler)]  = ":/icons/compiler.svg";
    m_iconPaths[toIndex(Icon::Profiler)]  = ":/icons/profiler.svg";
    m_iconPaths[toIndex(Icon::Open)]      = ":/icons/open.svg";
    m_iconPaths[toIndex(Icon::Save)]      = ":/icons/save.svg";
    m_iconPaths[toIndex(Icon::Settings)]  = ":/icons/settings.svg";
    m_iconPaths[toIndex(Icon::About)]     = ":/icons/about.svg";
}
}//namespace Kites
