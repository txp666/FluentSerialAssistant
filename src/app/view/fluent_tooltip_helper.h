#pragma once

#include <FluentQtWidgets/Widgets/ToolTip.h>

#include <QtCore/QString>
#include <QtCore/Qt>
#include <QtWidgets/QWidget>

namespace AppUi {

inline void installFluentToolTip(QWidget *widget,
                                 FluentQt::ToolTipPosition position = FluentQt::ToolTipPosition::Top,
                                 int delayMs = 300)
{
    if (!widget) {
        return;
    }
    if (widget->findChild<FluentQt::ToolTipFilter *>(QString(), Qt::FindDirectChildrenOnly)) {
        return;
    }
    widget->installEventFilter(new FluentQt::ToolTipFilter(widget, delayMs, position));
}

inline void setFluentToolTip(QWidget *widget, const QString &text,
                             FluentQt::ToolTipPosition position = FluentQt::ToolTipPosition::Top,
                             int delayMs = 300)
{
    if (!widget) {
        return;
    }
    widget->setToolTip(text);
    installFluentToolTip(widget, position, delayMs);
}

} // namespace AppUi
