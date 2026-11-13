#pragma once
//#include <QObject>
#include <QIcon>
#include <QHash>
#include <QPixmap>
#include <array>
#include "utils/to_index.h"

class QAction;

namespace Kites
{
enum class Icon
{
    Play,
    Stop,
    Pause,
    Resume,
    Step,
    Debug,
    Undo,
    Redo,
    Editor,
    Memory,
    Processor,
    Cache,
    Compiler,
    Profiler,
    Open,
    Save,
    Settings,
    About,
    Search,
    Problems,
    Copy,
    Wires,
    IconCount
};

// The chrome's icon sizes, in logical pixels, in ONE place.
//
// Three names rather than two numbers, so each site says which role it is
// asking for rather than repeating a literal. They happen to be 20, 20 and
// 32 today; a site that wants something else should get a name here instead
// of a number of its own.
//
// Every icon in resources/icons is a 24-unit viewBox whose ink fills the
// same fraction of it (the out-of-tree iconmetrics harness checks that), so
// equal nominal sizes really do look equal.
inline constexpr int tabStripIconSize    = 20;
inline constexpr int smallRibbonIconSize = 20;
inline constexpr int largeRibbonIconSize = 32;

class IconManager //: QObject
{
    //Q_OBJECT
public:
    static IconManager &getInstance();
    ~IconManager() = default;
    // Default matches the icon size the toolbar and sidebar request, so the
    // rasterised pixmap never has to be scaled up at paint time.
    QIcon getIcon(Icon icon, QSize size = QSize(smallRibbonIconSize, smallRibbonIconSize)) const;
private:
    IconManager();
    // using size for futue proofing this fucntion.
    void setUpIconPaths();
    std::array<QString, toIndex(Icon::IconCount)> m_iconPaths;
    // keyed by "icon:size:dpr:color" - the device pixel ratio has to be part of
    // the key, otherwise a pixmap rasterised for a 1x screen is reused on a
    // HiDPI one and the icon comes back soft
    mutable QHash<QString, QPixmap> m_pixmapCache;
    // The same pixmaps tinted for the disabled state, under the same keys.
    mutable QHash<QString, QPixmap> m_disabledCache;
};

// Tags `action` with `icon` and applies the tinted pixmap. Prefer this over
// QAction::setIcon(): it lets the icon be re-tinted on a theme change by
// reading a stable property, instead of matching the action's user-visible
// text - which breaks the moment a label is reworded or translated.
//
// `size` is the logical size the pixmap is rasterised at, and is remembered
// alongside the icon so a re-tint can rebuild it at the same size. It only
// needs passing for an action shown larger than the chrome default: a ribbon
// button that draws at 32 px would otherwise upscale a 20 px pixmap. An
// action shown at several sizes should be tagged with the largest, which the
// smaller sites then render scaled down rather than blown up.
void setThemedIcon(QAction *action, Icon icon,
                   QSize size = QSize(smallRibbonIconSize, smallRibbonIconSize));

// The Icon last set by setThemedIcon(), or Icon::IconCount when the action
// carries no tag.
Icon themedIconOf(const QAction *action);

// The size setThemedIcon() last rasterised this action's icon at, or the
// 20 px default when the action carries no tag.
QSize themedIconSizeOf(const QAction *action);
}//namespace Kites
