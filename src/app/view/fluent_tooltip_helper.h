#pragma once

#include <FluentQtWidgets/Widgets/ToolTip.h>

#include <QtCore/QEvent>
#include <QtCore/QString>
#include <QtCore/Qt>
#include <QtWidgets/QWidget>

namespace AppUi {

class ToolTipSuppressor final : public QObject
{
  public:
    explicit ToolTipSuppressor(QWidget *widget) : QObject(widget)
    {
        widget->installEventFilter(this);
        clearToolTip(widget);
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ToolTipChange) {
            clearToolTip(static_cast<QWidget *>(watched));
        } else if (event->type() == QEvent::ToolTip) {
            event->accept();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    static void clearToolTip(QWidget *widget)
    {
        if (!widget->toolTip().isEmpty()) {
            widget->setAccessibleName(widget->toolTip());
            widget->setToolTip(QString());
        }
    }
};

inline void suppressFluentToolTip(QWidget *widget)
{
    if (widget && !widget->property("appToolTipSuppressed").toBool()) {
        widget->setProperty("appToolTipSuppressed", true);
        new ToolTipSuppressor(widget);
    }
}

inline void installFluentToolTip(QWidget *widget, FluentQt::ToolTipPosition position = FluentQt::ToolTipPosition::Top,
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
                             FluentQt::ToolTipPosition position = FluentQt::ToolTipPosition::Top, int delayMs = 300)
{
    if (!widget) {
        return;
    }
    widget->setToolTip(text);
    installFluentToolTip(widget, position, delayMs);
}

} // namespace AppUi
