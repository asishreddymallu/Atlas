#include "table_font.h"
#include "ui/theme/font_manager.h"
#include "ui/theme/type_scale.h"
#include <QObject>

namespace Kites
{
namespace
{
struct TableFontCache
{
    QFont font;

    TableFontCache()
    {
        refresh();
        // follow the user's configured editor font
        QObject::connect(&FontManager::getInstance(), &FontManager::fontChangedSignal,
                         &FontManager::getInstance(), [this](const QFont &) { refresh(); });
    }

    void refresh()
    {
        font = FontManager::getInstance().currentFont();
        font.setPixelSize(tableFontPixelSize);
    }
};
} // namespace

const QFont &tableNumericFont()
{
    // function-local static: constructed on first use, never at static-init time
    static TableFontCache cache;
    return cache.font;
}
} // namespace Kites
