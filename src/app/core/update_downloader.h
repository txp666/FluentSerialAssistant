#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <memory>

namespace AppUpdate {

// Network I/O, disk writes, assembly and hashing run on a private worker thread.
// The public API and signals belong to the thread that creates this object.
class UpdateDownloader : public QObject
{
    Q_OBJECT

  public:
    explicit UpdateDownloader(QObject *parent = nullptr);
    ~UpdateDownloader() override;

    // sha256 is the 64-character hexadecimal digest from release metadata.
    // A start while a download is running is ignored; retry after a terminal signal.
    void start(const QUrl &url, qint64 expectedSize, const QByteArray &sha256, const QString &destination);
    void cancel();
    bool isRunning() const;

  signals:
    void progress(qint64 received, qint64 total);
    void phaseChanged(const QString &phase);
    void verificationStarted();
    void finished(const QString &path);
    void failed(const QString &message);
    void cancelled();

  private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace AppUpdate
