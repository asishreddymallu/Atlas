#pragma once
#include "gutter_column.h"

namespace Atlas
{
class LineNumberGutterColumn : public GutterColumn
{
    Q_OBJECT
public:
    explicit LineNumberGutterColumn(AtlasEditor* editor = nullptr);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
};
}//namespace Atlas