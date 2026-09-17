#include "app/core/update_downloader.h"
#include "app/core/app_i18n.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QRegularExpression>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <atomic>
#include <vector>

namespace AppUpdate {

namespace {

constexpr qint64 kChunkSize = 256 * 1024;
constexpr int kTransferTimeoutMs = 30000;
constexpr int kOverallTimeoutMs = 30 * 60 * 1000;

bool allowedUrl(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()) {
        return false;
    }
    if (url.scheme() == QStringLiteral("https")) {
        return true;
    }
    return url.scheme() == QStringLiteral("http") &&
           (url.host().compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0 ||
            QHostAddress(url.host()).isLoopback());
}

bool validHash(const QByteArray &hash)
{
    if (hash.size() != 64) {
        return false;
    }
    for (const char character : hash) {
        if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') ||
              (character >= 'A' && character <= 'F'))) {
            return false;
        }
    }
    return true;
}

} // namespace

class DownloadWorker : public QObject
{
    Q_OBJECT

  public:
    DownloadWorker()
    {
        m_overallTimer = new QTimer(this);
        m_overallTimer->setSingleShot(true);
        connect(m_overallTimer, &QTimer::timeout, this,
                [this]() { fail(AppI18n::text("更新下载超过最长等待时间，请重试")); });
    }

    ~DownloadWorker() override { shutdown(); }

    void begin(quint64 id, const QUrl &url, qint64 expectedSize, const QByteArray &sha256, const QString &destination,
               std::shared_ptr<std::atomic_bool> cancellation)
    {
        shutdown();
        m_id = id;
        m_active = true;
        m_cancellation = std::move(cancellation);
        m_url = url;
        m_expectedSize = expectedSize;
        m_sha256 = sha256.toLower();
        m_destination = QFileInfo(destination).absoluteFilePath();
        m_received = 0;
        m_pendingDownloads = 0;
        m_progressClock.start();
        if (wasCancelled()) {
            cancel(id);
            return;
        }
        if (!allowedUrl(url)) {
            fail(AppI18n::text("更新下载地址必须使用 HTTPS（本机测试地址除外）"));
            return;
        }
        if (expectedSize <= 0 || !validHash(sha256) || destination.isEmpty()) {
            fail(AppI18n::text("更新文件大小、SHA256 或保存路径无效"));
            return;
        }
        const QString directory = QFileInfo(m_destination).absolutePath();
        if (!QDir().mkpath(directory)) {
            fail(AppI18n::text("无法创建更新下载目录"));
            return;
        }
        m_temporaryDirectory =
            std::make_unique<QTemporaryDir>(QDir(directory).filePath(QStringLiteral(".fluent-update-XXXXXX")));
        if (!m_temporaryDirectory->isValid()) {
            fail(AppI18n::text("无法创建更新下载临时目录"));
            return;
        }
        if (!m_network) {
            m_network = new QNetworkAccessManager(this);
        }
        m_overallTimer->start(kOverallTimeoutMs);
        emit progress(id, 0, expectedSize);
        emit phaseChanged(id, AppI18n::text("正在检测下载服务器"));
        request(Kind::Probe, 0, 0);
    }

    void cancel(quint64 id)
    {
        if (!m_active || id != m_id) {
            return;
        }
        m_active = false;
        clearTransfers();
        emit cancelled(id);
    }

    void shutdown()
    {
        m_active = false;
        clearTransfers();
    }

    void discardCompleted(quint64 id, const QString &path)
    {
        if (id == m_id && !m_active) {
            QFile::remove(path);
            emit cancelled(id);
        }
    }

  signals:
    void progress(quint64 id, qint64 received, qint64 total);
    void phaseChanged(quint64 id, const QString &phase);
    void verificationStarted(quint64 id);
    void finished(quint64 id, const QString &path);
    void failed(quint64 id, const QString &message);
    void cancelled(quint64 id);

  private:
    enum class Kind
    {
        Probe,
        Range,
        Single
    };

    struct Transfer
    {
        Kind kind = Kind::Probe;
        QNetworkReply *reply = nullptr;
        std::unique_ptr<QFile> file;
        qint64 first = 0;
        qint64 last = 0;
        qint64 received = 0;
        bool headersValidated = false;
        bool completed = false;

        qint64 size() const { return last - first + 1; }
    };

    bool current(quint64 id) const { return m_active && m_id == id; }
    bool wasCancelled() const { return m_cancellation && m_cancellation->load(std::memory_order_relaxed); }

    void retireReply(Transfer *transfer)
    {
        if (transfer->reply) {
            transfer->reply->disconnect(this);
            transfer->reply->abort();
            transfer->reply->deleteLater();
            transfer->reply = nullptr;
        }
    }

    void clearTransfers()
    {
        m_overallTimer->stop();
        for (const auto &transfer : m_transfers) {
            retireReply(transfer.get());
            if (transfer->file) {
                transfer->file->close();
            }
        }
        m_transfers.clear();
        m_temporaryDirectory.reset();
    }

    void fail(const QString &message)
    {
        if (!m_active) {
            return;
        }
        if (wasCancelled()) {
            cancel(m_id);
            return;
        }
        m_active = false;
        clearTransfers();
        emit failed(m_id, message);
    }

    void request(Kind kind, qint64 first, qint64 last)
    {
        if (wasCancelled()) {
            cancel(m_id);
            return;
        }
        auto transfer = std::make_unique<Transfer>();
        transfer->kind = kind;
        transfer->first = first;
        transfer->last = last;
        if (kind != Kind::Probe) {
            transfer->file = std::make_unique<QFile>(
                m_temporaryDirectory->filePath(QStringLiteral("part-%1").arg(m_transfers.size())));
            if (!transfer->file->open(QIODevice::WriteOnly)) {
                fail(AppI18n::text("无法写入更新临时文件：%1").arg(transfer->file->errorString()));
                return;
            }
        }
        QNetworkRequest networkRequest(m_url);
        networkRequest.setRawHeader("User-Agent", "FluentSerialAssistant-Updater");
        networkRequest.setRawHeader("Accept-Encoding", "identity");
        networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                    QNetworkRequest::UserVerifiedRedirectPolicy);
        networkRequest.setMaximumRedirectsAllowed(5);
        networkRequest.setTransferTimeout(kTransferTimeoutMs);
        if (kind != Kind::Single) {
            networkRequest.setRawHeader("Range", "bytes=" + QByteArray::number(first) + '-' + QByteArray::number(last));
        }
        transfer->reply = m_network->get(networkRequest);
        transfer->reply->setReadBufferSize(kChunkSize);
        Transfer *item = transfer.get();
        m_transfers.push_back(std::move(transfer));
        const quint64 id = m_id;
        connect(item->reply, &QNetworkReply::redirected, this, [this, id, item](const QUrl &url) {
            if (!current(id)) {
                return;
            }
            const QUrl target = item->reply->url().resolved(url);
            if (!allowedUrl(target) ||
                (m_url.scheme() == QStringLiteral("https") && target.scheme() != QStringLiteral("https"))) {
                fail(AppI18n::text("更新服务器重定向到不安全的下载地址"));
                return;
            }
            item->reply->redirectAllowed();
        });
        connect(item->reply, &QNetworkReply::metaDataChanged, this, [this, id, item]() {
            if (current(id)) {
                validateHeaders(item);
            }
        });
        connect(item->reply, &QIODevice::readyRead, this, [this, id, item]() {
            if (current(id) && validateHeaders(item)) {
                consume(item);
            }
        });
        connect(item->reply, &QNetworkReply::finished, this, [this, id, item]() {
            if (!current(id)) {
                return;
            }
            if (wasCancelled()) {
                cancel(id);
                return;
            }
            if (item->reply->error() != QNetworkReply::NoError) {
                fail(AppI18n::text("更新下载失败：%1").arg(item->reply->errorString()));
                return;
            }
            if (!validateHeaders(item)) {
                if (current(id) && !item->completed) {
                    fail(AppI18n::text("更新服务器未返回有效的下载响应"));
                }
                return;
            }
            if (!consume(item)) {
                return;
            }
            if (item->received != item->size()) {
                fail(AppI18n::text("更新文件下载不完整，请重试"));
                return;
            }
            retireReply(item);
            item->completed = true;
            if (item->kind == Kind::Probe) {
                startRanges();
                return;
            }
            if (!item->file->flush()) {
                fail(AppI18n::text("无法保存更新临时文件：%1").arg(item->file->errorString()));
                return;
            }
            item->file->close();
            if (--m_pendingDownloads == 0) {
                finishDownload();
            }
        });
    }

    bool validateHeaders(Transfer *item)
    {
        if (wasCancelled()) {
            cancel(m_id);
            return false;
        }
        if (item->headersValidated) {
            return true;
        }
        const int status = item->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0 || (status >= 300 && status < 400)) {
            return false;
        }
        if (!allowedUrl(item->reply->url())) {
            fail(AppI18n::text("更新服务器返回了不安全的下载地址"));
            return false;
        }
        const QByteArray encoding = item->reply->rawHeader("Content-Encoding").trimmed().toLower();
        if (!encoding.isEmpty() && encoding != QByteArrayLiteral("identity")) {
            fail(AppI18n::text("更新服务器返回了不支持的压缩编码"));
            return false;
        }
        if (item->kind == Kind::Probe && status == 200) {
            // Abort on headers. Do not download the entire installer as a probe.
            retireReply(item);
            item->completed = true;
            const quint64 id = m_id;
            QTimer::singleShot(0, this, [this, id]() {
                if (!current(id)) {
                    return;
                }
                emit phaseChanged(id, AppI18n::text("服务器不支持分段下载，正在使用单连接"));
                m_pendingDownloads = 1;
                request(Kind::Single, 0, m_expectedSize - 1);
            });
            return false;
        }
        const int expectedStatus = item->kind == Kind::Single ? 200 : 206;
        if (status != expectedStatus) {
            fail(AppI18n::text("更新服务器返回 HTTP %1，预期 HTTP %2").arg(status).arg(expectedStatus));
            return false;
        }
        if (item->kind != Kind::Single) {
            static const QRegularExpression pattern(QStringLiteral("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$"),
                                                    QRegularExpression::CaseInsensitiveOption);
            const auto match = pattern.match(QString::fromLatin1(item->reply->rawHeader("Content-Range").trimmed()));
            bool firstOk = false;
            bool lastOk = false;
            bool totalOk = false;
            const qint64 first = match.captured(1).toLongLong(&firstOk);
            const qint64 last = match.captured(2).toLongLong(&lastOk);
            const qint64 total = match.captured(3).toLongLong(&totalOk);
            if (!match.hasMatch() || !firstOk || !lastOk || !totalOk || first != item->first || last != item->last ||
                total != m_expectedSize) {
                fail(AppI18n::text("更新服务器返回的分段范围或文件大小不匹配"));
                return false;
            }
        }
        const QByteArray lengthHeader = item->reply->rawHeader("Content-Length");
        if (!lengthHeader.isEmpty()) {
            bool lengthOk = false;
            const qint64 length = lengthHeader.toLongLong(&lengthOk);
            if (!lengthOk || length != item->size()) {
                fail(AppI18n::text("更新服务器返回的文件长度不匹配"));
                return false;
            }
        }
        item->headersValidated = true;
        return true;
    }

    bool consume(Transfer *item)
    {
        while (item->reply->bytesAvailable() > 0) {
            if (wasCancelled()) {
                cancel(m_id);
                return false;
            }
            const QByteArray data = item->reply->read(kChunkSize);
            if (data.isEmpty()) {
                break;
            }
            if (data.size() > item->size() - item->received) {
                fail(AppI18n::text("更新服务器发送的数据超过预期文件长度"));
                return false;
            }
            if (item->file && item->file->write(data) != data.size()) {
                fail(AppI18n::text("无法写入更新临时文件：%1").arg(item->file->errorString()));
                return false;
            }
            item->received += data.size();
            if (item->kind != Kind::Probe) {
                m_received += data.size();
                if (m_progressClock.elapsed() >= 50 || m_received == m_expectedSize) {
                    emit progress(m_id, m_received, m_expectedSize);
                    m_progressClock.restart();
                }
            }
        }
        return true;
    }

    void startRanges()
    {
        const int connections = static_cast<int>(qMin<qint64>(4, m_expectedSize));
        emit phaseChanged(m_id, AppI18n::text("正在使用 %1 路连接下载").arg(connections));
        m_pendingDownloads = connections;
        const qint64 base = m_expectedSize / connections;
        const qint64 remainder = m_expectedSize % connections;
        qint64 first = 0;
        for (int index = 0; index < connections && m_active; ++index) {
            const qint64 size = base + (index < remainder ? 1 : 0);
            request(Kind::Range, first, first + size - 1);
            first += size;
        }
    }

    bool mergeAndVerify(QString *error)
    {
        QSaveFile output(m_destination);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly)) {
            *error = AppI18n::text("无法保存更新文件：%1").arg(output.errorString());
            return false;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        qint64 size = 0;
        for (const auto &transfer : m_transfers) {
            if (!transfer->file) {
                continue;
            }
            QFile input(transfer->file->fileName());
            if (!input.open(QIODevice::ReadOnly)) {
                *error = AppI18n::text("无法读取更新临时文件：%1").arg(input.errorString());
                return false;
            }
            while (!input.atEnd()) {
                // Hashing/merging deliberately does not depend on the worker's
                // event loop for cancellation, so the UI can interrupt it.
                if (wasCancelled()) {
                    return false;
                }
                const QByteArray data = input.read(kChunkSize);
                if (data.isEmpty() && input.error() != QFileDevice::NoError) {
                    *error = AppI18n::text("无法读取更新临时文件：%1").arg(input.errorString());
                    return false;
                }
                if (output.write(data) != data.size()) {
                    *error = AppI18n::text("无法保存更新文件：%1").arg(output.errorString());
                    return false;
                }
                hash.addData(data);
                size += data.size();
            }
        }
        if (size != m_expectedSize || m_received != m_expectedSize) {
            *error = AppI18n::text("更新文件大小校验失败");
            return false;
        }
        if (hash.result().toHex() != m_sha256) {
            *error = AppI18n::text("更新文件 SHA256 校验失败，请重试");
            return false;
        }
        if (wasCancelled()) {
            return false;
        }
        if (!output.commit()) {
            *error = AppI18n::text("无法完成更新文件保存：%1").arg(output.errorString());
            return false;
        }
        return true;
    }

    void finishDownload()
    {
        emit verificationStarted(m_id);
        emit phaseChanged(m_id, AppI18n::text("正在合并并校验更新文件"));
        QString error;
        if (!mergeAndVerify(&error)) {
            fail(error);
            return;
        }
        m_active = false;
        clearTransfers();
        emit finished(m_id, m_destination);
    }

    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_overallTimer = nullptr;
    quint64 m_id = 0;
    bool m_active = false;
    std::shared_ptr<std::atomic_bool> m_cancellation;
    QUrl m_url;
    qint64 m_expectedSize = 0;
    QByteArray m_sha256;
    QString m_destination;
    qint64 m_received = 0;
    int m_pendingDownloads = 0;
    QElapsedTimer m_progressClock;
    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
    std::vector<std::unique_ptr<Transfer>> m_transfers;
};

class UpdateDownloader::Private
{
  public:
    QThread thread;
    DownloadWorker *worker = nullptr;
    quint64 id = 0;
    bool running = false;
    std::shared_ptr<std::atomic_bool> cancellation;
};

UpdateDownloader::UpdateDownloader(QObject *parent) : QObject(parent), d(std::make_unique<Private>())
{
    d->worker = new DownloadWorker;
    d->worker->moveToThread(&d->thread);
    connect(&d->thread, &QThread::finished, d->worker, &QObject::deleteLater);
    connect(d->worker, &DownloadWorker::progress, this, [this](quint64 id, qint64 received, qint64 total) {
        if (d->running && id == d->id) {
            emit progress(received, total);
        }
    });
    connect(d->worker, &DownloadWorker::phaseChanged, this, [this](quint64 id, const QString &phase) {
        if (d->running && id == d->id) {
            emit phaseChanged(phase);
        }
    });
    connect(d->worker, &DownloadWorker::verificationStarted, this, [this](quint64 id) {
        if (d->running && id == d->id && !d->cancellation->load(std::memory_order_relaxed)) {
            emit verificationStarted();
        }
    });
    connect(d->worker, &DownloadWorker::finished, this, [this](quint64 id, const QString &path) {
        if (d->running && id == d->id) {
            if (d->cancellation->load(std::memory_order_relaxed)) {
                QMetaObject::invokeMethod(
                    d->worker, [worker = d->worker, id, path]() { worker->discardCompleted(id, path); },
                    Qt::QueuedConnection);
                return;
            }
            d->running = false;
            emit finished(path);
        }
    });
    connect(d->worker, &DownloadWorker::failed, this, [this](quint64 id, const QString &message) {
        if (d->running && id == d->id) {
            d->running = false;
            emit failed(message);
        }
    });
    connect(d->worker, &DownloadWorker::cancelled, this, [this](quint64 id) {
        if (d->running && id == d->id) {
            d->running = false;
            emit cancelled();
        }
    });
    d->thread.start();
}

UpdateDownloader::~UpdateDownloader()
{
    if (d->cancellation) {
        d->cancellation->store(true, std::memory_order_relaxed);
    }
    QMetaObject::invokeMethod(d->worker, [worker = d->worker]() { worker->shutdown(); }, Qt::BlockingQueuedConnection);
    d->thread.quit();
    d->thread.wait();
}

void UpdateDownloader::start(const QUrl &url, qint64 expectedSize, const QByteArray &sha256, const QString &destination)
{
    if (d->running) {
        return;
    }
    d->running = true;
    const quint64 id = ++d->id;
    d->cancellation = std::make_shared<std::atomic_bool>(false);
    QMetaObject::invokeMethod(
        d->worker,
        [worker = d->worker, id, url, expectedSize, sha256, destination, cancellation = d->cancellation]() {
            worker->begin(id, url, expectedSize, sha256, destination, cancellation);
        },
        Qt::QueuedConnection);
}

void UpdateDownloader::cancel()
{
    if (!d->running) {
        return;
    }
    d->cancellation->store(true, std::memory_order_relaxed);
    QMetaObject::invokeMethod(
        d->worker, [worker = d->worker, id = d->id]() { worker->cancel(id); }, Qt::QueuedConnection);
}

bool UpdateDownloader::isRunning() const { return d->running; }

} // namespace AppUpdate

#include "update_downloader.moc"
