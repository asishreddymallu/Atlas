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
    IconCount
};

class IconManager //: QObject
{
    //Q_OBJECT
public:
    static IconManager &getInstance();
    ~IconManager() = default;
    // Default matches the icon size the toolbar and sidebar request, so the
    // rasterised pixmap never has to be scaled up at paint time.
    QIcon getIcon(Icon icon, QSize size = QSize(20, 20)) const;
private:
    IconManager();
    // using size for futue proofing this fucntion.
    void setUpIconPaths();
    std::array<QString, toIndex(Icon::IconCount)> m_iconPaths;
    // keyed by "icon:size:dpr:color" - the device pixel ratio has to be part of
    // the key, otherwise a pixmap rasterised for a 1x screen is reused on a
    // HiDPI one and the icon comes back soft
    mutable QHash<QString, QPixmap> m_pixmapCache;
};

// Tags `action` with `icon` and applies the tinted pixmap. Prefer this over
// QAction::setIcon(): it lets the icon be re-tinted on a theme change by
// reading a stable property, instead of matching the action's user-visible
// text - which breaks the moment a label is reworded or translated.
void setThemedIcon(QAction *action, Icon icon);

// The Icon last set by setThemedIcon(), or Icon::IconCount when the action
// carries no tag.
Icon themedIconOf(const QAction *action);
}//namespace Kites
