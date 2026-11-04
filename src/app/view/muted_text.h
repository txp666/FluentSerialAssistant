#pragma once

#include <FluentQtWidgets/Widgets/Label.h>

#include <QtGui/QColor>

namespace AppUi {

// The grey of hints, captions and status lines. FluentQtWidgets' CaptionLabel uses the text colour, as Python's
// does, so a label that should read as secondary text sets this.
inline void setMutedTextColor(FluentQt::FluentLabelBase *label)
{
    label->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
}

} // namespace AppUi
