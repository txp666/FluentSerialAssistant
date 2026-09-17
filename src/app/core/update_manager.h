#pragma once

#include "app/core/update_release.h"

#include <QtCore/QObject>

#include <functional>

class UpdateManagerTest;
class UpdateUiTest;
class QThread;

namespace AppUpdate {

class UpdateChecker;
class UpdateDownloader;

class UpdateManager : public QObject
{
    Q_OBJECT

  public:
    enum class State
    {
        Idle,
        Checking,
        Available,
        Downloading,
        Verifying,
        Installing,
        UpToDate,
        Cancelled,
        Failed
    };
    Q_ENUM(State)

    explicit UpdateManager(QObject *parent = nullptr);
    ~UpdateManager() override;

    State state() const;
    const ReleaseInfo &release() const;
    QString statusMessage() const;
    qint64 receivedBytes() const;
    qint64 totalBytes() const;
    bool isAutomaticCheck() const;
    bool isBusy() const;

  public slots:
    void checkForUpdates(bool automatic = false);
    void downloadUpdate();
    void cancelDownload();

  signals:
    void stateChanged(AppUpdate::UpdateManager::State state);
    void progressChanged(qint64 received, qint64 total);
    void updateFound();
    void installationReady();

  private:
    friend class ::UpdateManagerTest;
    friend class ::UpdateUiTest;
    void setState(State state, const QString &message);
    void handleDownloaded(const QString &path);
    void clearDownload();

    UpdateChecker *m_checker = nullptr;
    UpdateDownloader *m_downloader = nullptr;
    ReleaseInfo m_release;
    State m_state = State::Idle;
    QString m_message;
    QString m_downloadPath;
    QString m_downloadDirectory;
    QString m_cacheRoot;
    qint64 m_received = 0;
    bool m_automatic = false;
    bool m_downloadAuthorized = false;
    QThread *m_installThread = nullptr;
    std::function<bool(const QString &, QString *)> m_install;
};

} // namespace AppUpdate
