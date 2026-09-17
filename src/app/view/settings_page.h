#pragma once

#include "app/core/update_manager.h"
#include "app/view/app_page.h"

class SettingsPage : public AppPage
{
    Q_OBJECT

  public:
    explicit SettingsPage(QWidget *parent = nullptr, AppUpdate::UpdateManager *updateManager = nullptr);

  signals:
    void terminalRequested();
    void terminalFontChanged(const QString &family);
    void updateDialogRequested();

  private slots:
    void checkForUpdates();
    void handleUpdateStateChanged(AppUpdate::UpdateManager::State state);

  private:
    void refreshUpdateCard();

    AppUpdate::UpdateManager *m_updateManager = nullptr;
    AppUpdate::UpdateManager::State m_lastUpdateState = AppUpdate::UpdateManager::State::Idle;
    FluentQt::PushSettingCard *m_updateCard = nullptr;
};
