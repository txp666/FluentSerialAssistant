#pragma once

#include <FluentQtWidgets/FluentQtWidgets.h>

class QVBoxLayout;

class AppPage : public FluentQt::ScrollArea
{
    Q_OBJECT

  public:
    AppPage(const QString &title, const QString &subtitle, QWidget *parent = nullptr, bool showHeader = true);

    QVBoxLayout *contentLayout() const;
    QWidget *addSection(const QString &title, QWidget *content, int stretch = 0);
    void addHeaderAction(QWidget *action);

  private:
    FluentQt::HeaderCardWidget *m_headerCard = nullptr;
    QWidget *m_view = nullptr;
    QVBoxLayout *m_viewLayout = nullptr;
};
