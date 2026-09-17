#include "app/core/update_manager.h"
#include "app/core/app_i18n.h"
#include "app/core/update_checker.h"
#include "app/core/update_downloader.h"
#include "app/core/update_installer.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtCore/QUuid>

namespace AppUpdate {

UpdateManager::UpdateManager(QObject *parent)
    : QObject(parent), m_checker(new UpdateChecker(this)), m_downloader(new UpdateDownloader(this)),
      m_install(launchUpdateInstaller)
{
    connect(m_checker, &UpdateChecker::checkFinished, this,
            [this](bool ok, bool available, const QString &, const QString &, const QUrl &, const QString &message) {
                if (!ok) {
                    setState(State::Failed, message);
                    return;
                }
                m_release = m_checker->latestRelease();
                if (available) {
                    setState(State::Available, AppI18n::text("发现新版本 %1").arg(m_release.version));
                    // Finding a release never authorizes downloading it. Only the confirmation button does.
                    emit updateFound();
                } else {
                    setState(State::UpToDate, message);
                }
            });
    connect(m_downloader, &UpdateDownloader::progress, this, [this](qint64 received, qint64 total) {
        if (!m_downloadAuthorized) {
            return;
        }
        m_received = received;
        emit progressChanged(received, total);
    });
    connect(m_downloader, &UpdateDownloader::verificationStarted, this, [this]() {
        if (m_downloadAuthorized) {
            setState(State::Verifying, AppI18n::text("正在校验更新文件..."));
        }
    });
    connect(m_downloader, &UpdateDownloader::phaseChanged, this, [this](const QString &phase) {
        if (m_downloadAuthorized) {
            setState(m_state, phase);
        }
    });
    connect(m_downloader, &UpdateDownloader::finished, this, &UpdateManager::handleDownloaded);
    connect(m_downloader, &UpdateDownloader::failed, this, [this](const QString &message) {
        m_downloadAuthorized = false;
        clearDownload();
        setState(State::Failed, message);
    });
    connect(m_downloader, &UpdateDownloader::cancelled, this, [this]() {
        m_downloadAuthorized = false;
        clearDownload();
        setState(State::Cancelled, AppI18n::text("下载已取消"));
    });
}

UpdateManager::~UpdateManager()
{
    if (m_installThread) {
        m_installThread->requestInterruption();
        m_installThread->wait();
    }
    // Stop the writer before removing its per-attempt directory. The installer
    // owns its own durable copy after acknowledging the handoff.
    delete m_downloader;
    m_downloader = nullptr;
    clearDownload();
}

void UpdateManager::clearDownload()
{
    if (!m_downloadDirectory.isEmpty()) {
        QDir(m_downloadDirectory).removeRecursively();
        m_downloadDirectory.clear();
        m_downloadPath.clear();
    }
}

UpdateManager::State UpdateManager::state() const { return m_state; }
const ReleaseInfo &UpdateManager::release() const { return m_release; }
QString UpdateManager::statusMessage() const { return m_message; }
qint64 UpdateManager::receivedBytes() const { return m_received; }
qint64 UpdateManager::totalBytes() const { return m_release.size; }
bool UpdateManager::isAutomaticCheck() const { return m_automatic; }
bool UpdateManager::isBusy() const
{
    return m_state == State::Checking || m_state == State::Downloading || m_state == State::Verifying ||
           m_state == State::Installing || m_downloader->isRunning() ||
           (m_installThread && m_installThread->isRunning());
}

void UpdateManager::setState(State state, const QString &message)
{
    m_state = state;
    m_message = message;
    emit stateChanged(state);
}

void UpdateManager::checkForUpdates(bool automatic)
{
    if (isBusy()) {
        return;
    }
    m_automatic = automatic;
    m_downloadAuthorized = false;
    m_release = {};
    m_received = 0;
    setState(State::Checking, AppI18n::text("正在检查更新..."));
    m_checker->checkLatestRelease();
}

void UpdateManager::downloadUpdate()
{
    if (isBusy() || m_release.downloadUrl.isEmpty() ||
        (m_state != State::Available && m_state != State::Cancelled && m_state != State::Failed)) {
        return;
    }
    m_automatic = false;
    clearDownload();
    const QString cache =
        m_cacheRoot.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) : m_cacheRoot;
    if (cache.isEmpty()) {
        setState(State::Failed, AppI18n::text("无法创建更新下载目录"));
        return;
    }
    const QString directory =
        QDir(cache).filePath(QStringLiteral("updates/%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().mkpath(directory) ||
        !QFile::setPermissions(directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        setState(State::Failed, AppI18n::text("无法创建更新下载目录"));
        return;
    }
    m_downloadPath = QDir(directory).filePath(m_release.assetName);
    m_downloadDirectory = directory;
    m_received = 0;
    m_downloadAuthorized = true;
    setState(State::Downloading, AppI18n::text("正在下载更新..."));
    emit progressChanged(0, m_release.size);
    m_downloader->start(m_release.downloadUrl, m_release.size, m_release.sha256, m_downloadPath);
}

void UpdateManager::cancelDownload()
{
    if (m_state != State::Downloading && m_state != State::Verifying) {
        return;
    }
    m_downloadAuthorized = false;
    m_downloader->cancel();
    setState(State::Cancelled, AppI18n::text("正在取消下载..."));
}

void UpdateManager::handleDownloaded(const QString &path)
{
    if (!m_downloadAuthorized || path != m_downloadPath) {
        return;
    }
    m_downloadAuthorized = false;
    setState(State::Installing, AppI18n::text("下载校验完成，正在准备安装..."));
    // Package preparation and system authorization may take time. Keep the
    // event loop responsive while the installer acknowledges the handoff.
    const auto install = m_install;
    m_installThread = QThread::create([this, path, install]() {
        QString error;
        const bool ready = install(path, &error);
        QMetaObject::invokeMethod(
            this,
            [this, ready, error]() {
                if (!ready) {
                    setState(State::Failed, error.isEmpty() ? AppI18n::text("无法启动更新安装程序") : error);
                    return;
                }
                emit installationReady();
            },
            Qt::QueuedConnection);
    });
    m_installThread->setParent(this);
    QThread *thread = m_installThread;
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_installThread == thread) {
            m_installThread = nullptr;
        }
        thread->deleteLater();
    });
    m_installThread->start();
}

} // namespace AppUpdate
