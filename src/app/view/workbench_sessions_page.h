#pragma once

#include "app/control/session_control.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

class WorkbenchPage;

class WorkbenchSessionsPage : public QWidget
{
    Q_OBJECT

  public:
    struct ControlSessionEntry
    {
        QString id;
        QString title;
        AppControl::SessionControl *control = nullptr;
        bool current = false;
    };

    explicit WorkbenchSessionsPage(QWidget *parent = nullptr);

    void saveSettings() const;
    void installTitleBarTabs(FluentQt::FluentTitleBar *titleBar);
    void setTitleBarTabsVisible(bool visible);
    QList<ControlSessionEntry> controlSessions() const;
    AppControl::SessionControl *controlSession(const QString &id = QString()) const;
    bool selectControlSession(const QString &id);

  public slots:
    void setTerminalFontFamily(const QString &family);

  signals:
    void settingsRequested();

  private:
    WorkbenchPage *addSession(WorkbenchPage *source = nullptr, bool restoreSavedSession = false);
    WorkbenchPage *currentSession() const;
    void closeSession(int index);
    void updateTitleBarTabMetrics();
    QString nextRouteKey() const;
    QString nextTitle() const;

    FluentQt::TabWidget *m_tabs = nullptr;
    FluentQt::FluentTitleBar *m_titleBar = nullptr;
    int m_nextSession = 1;
};
