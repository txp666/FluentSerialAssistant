#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace AppUpdate {

class UpdateChecker : public QObject
{
    Q_OBJECT

  public:
    explicit UpdateChecker(QObject *parent = nullptr);

    bool isChecking() const;
    void checkLatestRelease();

  signals:
    void checkStarted();
    void checkFinished(bool ok,
                       bool updateAvailable,
                       const QString &currentVersion,
                       const QString &latestVersion,
                       const QUrl &releaseUrl,
                       const QString &message);

  private:
    void handleReply(QNetworkReply *reply);

    QNetworkAccessManager *m_network = nullptr;
    bool m_checking = false;
};

} // namespace AppUpdate
