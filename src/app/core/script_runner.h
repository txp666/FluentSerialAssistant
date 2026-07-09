#pragma once

#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>

#include <atomic>
#include <functional>

class QJSEngine;
class QJSValue;

class ScriptBridge : public QObject
{
    Q_OBJECT

  public:
    using SendCallback = std::function<bool(const QString &payload, const QString &lineEnding, QString *error)>;
    using StringCallback = std::function<QString()>;
    using RecordsCallback = std::function<QVariantList()>;
    using StopCallback = std::function<bool()>;

    explicit ScriptBridge(QObject *parent = nullptr);

    void setSendTextCallback(SendCallback callback);
    void setSendHexCallback(SendCallback callback);
    void setReceivedTextCallback(StringCallback callback);
    void setReceivedHexCallback(StringCallback callback);
    void setLastRxTextCallback(StringCallback callback);
    void setLastRxHexCallback(StringCallback callback);
    void setRecordsCallback(RecordsCallback callback);
    void setStopCallback(StopCallback callback);

    Q_INVOKABLE bool sendText(const QString &payload, const QString &lineEnding = QString());
    Q_INVOKABLE bool sendHex(const QString &payload, const QString &lineEnding = QString());
    Q_INVOKABLE QString receivedText() const;
    Q_INVOKABLE QString receivedHex() const;
    Q_INVOKABLE QString lastRxText() const;
    Q_INVOKABLE QString lastRxHex() const;
    Q_INVOKABLE QVariantList records() const;
    Q_INVOKABLE void log(const QString &message);
    Q_INVOKABLE void sleep(int milliseconds);
    Q_INVOKABLE bool stopRequested() const;
    Q_INVOKABLE QString lastError() const;

  signals:
    void logMessage(const QString &message);

  private:
    bool sendWithCallback(const SendCallback &callback, const QString &payload, const QString &lineEnding);
    QString stringFromCallback(const StringCallback &callback) const;

    SendCallback m_sendTextCallback;
    SendCallback m_sendHexCallback;
    StringCallback m_receivedTextCallback;
    StringCallback m_receivedHexCallback;
    StringCallback m_lastRxTextCallback;
    StringCallback m_lastRxHexCallback;
    RecordsCallback m_recordsCallback;
    StopCallback m_stopCallback;
    QString m_lastError;
};

class ScriptRunner : public QObject
{
    Q_OBJECT

  public:
    explicit ScriptRunner(QObject *parent = nullptr);
    ~ScriptRunner() override;

    void setSendTextCallback(ScriptBridge::SendCallback callback);
    void setSendHexCallback(ScriptBridge::SendCallback callback);
    void setReceivedTextCallback(ScriptBridge::StringCallback callback);
    void setReceivedHexCallback(ScriptBridge::StringCallback callback);
    void setLastRxTextCallback(ScriptBridge::StringCallback callback);
    void setLastRxHexCallback(ScriptBridge::StringCallback callback);
    void setRecordsCallback(ScriptBridge::RecordsCallback callback);
    void requestStop();

  public slots:
    void runScript(const QString &script, const QString &fileName);

  signals:
    void started();
    void logMessage(const QString &message);
    void finished(bool ok, const QString &message);

  private:
    bool stopRequested() const;
    QString errorText(const QJSValue &value, const QStringList &stackTrace) const;

    ScriptBridge::SendCallback m_sendTextCallback;
    ScriptBridge::SendCallback m_sendHexCallback;
    ScriptBridge::StringCallback m_receivedTextCallback;
    ScriptBridge::StringCallback m_receivedHexCallback;
    ScriptBridge::StringCallback m_lastRxTextCallback;
    ScriptBridge::StringCallback m_lastRxHexCallback;
    ScriptBridge::RecordsCallback m_recordsCallback;
    mutable QMutex m_engineMutex;
    QJSEngine *m_engine = nullptr;
    std::atomic_bool m_stopRequested = false;
};
