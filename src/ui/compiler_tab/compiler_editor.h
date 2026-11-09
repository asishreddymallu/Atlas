#include "ui/common/kites_editor.h"
#include "ui/common/line_number_gutter_column.h"
namespace Kites
{
class CompilerEditor : public KitesEditor
{
    Q_OBJECT
public:
    explicit CompilerEditor(QWidget *parent = nullptr)
    : KitesEditor(parent)
    {
        addLeftGutterColumn(new LineNumberGutterColumn(this));
        // KitesEditor's constructor sizes the viewport margins before this
        // gutter exists, so without this the gutter overlaps the first couple
        // of characters until the block count next changes (same call that
        // Editor and ProfilerEditor already make after adding theirs).
        updateViewPortMargins();
    }
    ~CompilerEditor() {};
};
} // namespace Kites
