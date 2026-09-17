#pragma once

#include "app/core/update_release.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

class QNetworkAccessManager;
class QNetworkReply;
class UpdateManagerTest;

namespace AppUpdate {

class UpdateChecker : public QObject
{
    Q_OBJECT

  public:
    explicit UpdateChecker(QObject *parent = nullptr);
    UpdateChecker(const QUrl &releaseApiUrl, QObject *parent);

    bool isChecking() const;
    const ReleaseInfo &latestRelease() const;
    void checkLatestRelease();

  signals:
    void checkStarted();
    void checkFinished(bool ok, bool updateAvailable, const QString &currentVersion, const QString &latestVersion,
                       const QUrl &releaseUrl, const QString &message);

  private:
    friend class ::UpdateManagerTest;
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager *m_network = nullptr;
    QUrl m_releaseApiUrl;
    ReleaseInfo m_release;
    QByteArray m_payload;
    bool m_responseTooLarge = false;
    bool m_checking = false;
};

} // namespace AppUpdate
