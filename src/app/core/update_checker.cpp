#include "app/core/update_checker.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace {

QUrl latestReleaseApiUrl()
{
    return QUrl(QStringLiteral("https://api.github.com/repos/txp666/FluentSerialAssistant/releases/latest"));
}

QString normalizedVersion(QString version)
{
    version = version.trimmed();
    if(version.startsWith(QLatin1Char('v'), Qt::CaseInsensitive)) {
        version.remove(0, 1);
    }
    return version;
}

QList<int> versionParts(const QString &version)
{
    QList<int> parts;
    const QRegularExpression numberPattern(QStringLiteral("(\\d+)"));
    auto matchIterator = numberPattern.globalMatch(normalizedVersion(version));
    while(matchIterator.hasNext()) {
        parts.append(matchIterator.next().captured(1).toInt());
    }
    return parts;
}

int compareVersions(const QString &left, const QString &right)
{
    const QList<int> leftParts = versionParts(left);
    const QList<int> rightParts = versionParts(right);
    const int count = qMax(leftParts.size(), rightParts.size());
    for(int i = 0; i < count; ++i) {
        const int leftValue = i < leftParts.size() ? leftParts.at(i) : 0;
        const int rightValue = i < rightParts.size() ? rightParts.at(i) : 0;
        if(leftValue != rightValue) {
            return leftValue < rightValue ? -1 : 1;
        }
    }
    return 0;
}

QString currentApplicationVersion()
{
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral("0.0.0") : version;
}

} // namespace

namespace AppUpdate {

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

bool UpdateChecker::isChecking() const
{
    return m_checking;
}

void UpdateChecker::checkLatestRelease()
{
    if(m_checking) {
        return;
    }

    m_checking = true;
    emit checkStarted();

    QNetworkRequest request(latestReleaseApiUrl());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "FluentSerialAssistant");

    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::handleReply(QNetworkReply *reply)
{
    const QString currentVersion = currentApplicationVersion();
    m_checking = false;

    if(reply->error() != QNetworkReply::NoError) {
        emit checkFinished(false,
                           false,
                           currentVersion,
                           QString(),
                           QUrl(),
                           reply->errorString());
        return;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if(statusCode < 200 || statusCode >= 300) {
        emit checkFinished(false,
                           false,
                           currentVersion,
                           QString(),
                           QUrl(),
                           QStringLiteral("GitHub 返回 HTTP %1").arg(statusCode));
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if(!document.isObject()) {
        emit checkFinished(false,
                           false,
                           currentVersion,
                           QString(),
                           QUrl(),
                           QStringLiteral("更新信息格式无效"));
        return;
    }

    const QJsonObject release = document.object();
    const QString latestVersion = normalizedVersion(release.value(QStringLiteral("tag_name")).toString());
    const QUrl releaseUrl(release.value(QStringLiteral("html_url")).toString());
    if(latestVersion.isEmpty() || !releaseUrl.isValid()) {
        emit checkFinished(false,
                           false,
                           currentVersion,
                           QString(),
                           QUrl(),
                           QStringLiteral("未找到有效的发布版本"));
        return;
    }

    const bool updateAvailable = compareVersions(latestVersion, currentVersion) > 0;
    emit checkFinished(true,
                       updateAvailable,
                       currentVersion,
                       latestVersion,
                       releaseUrl,
                       updateAvailable ? QStringLiteral("发现新版本") : QStringLiteral("当前已是最新版本"));
}

} // namespace AppUpdate
