#include "app/core/update_checker.h"
#include "app/core/app_i18n.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QTimer>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace AppUpdate {

namespace {
constexpr qint64 kMaximumReleaseBytes = 2 * 1024 * 1024;
}

UpdateChecker::UpdateChecker(QObject *parent)
    : UpdateChecker(QUrl(QStringLiteral("https://api.github.com/repos/txp666/FluentSerialAssistant/releases/latest")),
                    parent)
{
}

UpdateChecker::UpdateChecker(const QUrl &releaseApiUrl, QObject *parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)), m_releaseApiUrl(releaseApiUrl)
{
}

bool UpdateChecker::isChecking() const { return m_checking; }

const ReleaseInfo &UpdateChecker::latestRelease() const { return m_release; }

void UpdateChecker::checkLatestRelease()
{
    if (m_checking) {
        return;
    }
    m_checking = true;
    m_release = {};
    m_payload.clear();
    m_responseTooLarge = false;
    emit checkStarted();

    QNetworkRequest request(m_releaseApiUrl);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "FluentSerialAssistant");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);
    auto *reply = m_network->get(request);
    reply->setReadBufferSize(kMaximumReleaseBytes + 1);
    QTimer::singleShot(45000, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply]() {
        m_payload.append(reply->readAll());
        if (m_payload.size() > kMaximumReleaseBytes) {
            m_responseTooLarge = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
        reply->deleteLater();
    });
}

void UpdateChecker::handleReply(QNetworkReply *reply)
{
    QString currentVersion = QCoreApplication::applicationVersion();
    if (currentVersion.isEmpty()) {
        currentVersion = QStringLiteral("0.0.0");
    }
    m_checking = false;
    const auto fail = [this, &currentVersion](const QString &message) {
        emit checkFinished(false, false, currentVersion, {}, {}, message);
    };
    if (m_responseTooLarge) {
        fail(AppI18n::text("更新信息超过大小限制"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(reply->errorString());
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 200) {
        fail(AppI18n::text("GitHub 返回 HTTP %1").arg(status));
        return;
    }
    m_payload.append(reply->readAll());
    if (m_payload.size() > kMaximumReleaseBytes) {
        fail(AppI18n::text("更新信息超过大小限制"));
        return;
    }
    bool available = false;
    QString error;
    if (!parseRelease(m_payload, currentPlatformKey(), currentVersion, &m_release, &available, &error)) {
        fail(error);
        return;
    }
    emit checkFinished(true, available, currentVersion, m_release.version, m_release.releaseUrl,
                       available ? AppI18n::text("发现新版本") : AppI18n::text("当前已是最新版本"));
}

} // namespace AppUpdate
