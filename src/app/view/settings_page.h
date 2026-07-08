#pragma once

#include "app/view/app_page.h"

namespace AppUpdate {
class UpdateChecker;
}

class QUrl;

class SettingsPage : public AppPage
{
    Q_OBJECT

  public:
    explicit SettingsPage(QWidget *parent = nullptr);

  signals:
    void terminalRequested();
    void terminalFontChanged(const QString &family);

  private slots:
    void checkForUpdates();
    void handleUpdateCheckStarted();
    void handleUpdateCheckFinished(bool ok,
                                   bool updateAvailable,
                                   const QString &currentVersion,
                                   const QString &latestVersion,
                                   const QUrl &releaseUrl,
                                   const QString &message);

  private:
    AppUpdate::UpdateChecker *m_updateChecker = nullptr;
    FluentQt::PushSettingCard *m_updateCard = nullptr;
};
