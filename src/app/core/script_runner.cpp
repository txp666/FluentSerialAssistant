#include "app/core/script_runner.h"

#include "app/core/app_i18n.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtQml/QJSEngine>
#include <QtQml/QJSValue>

#include <utility>

namespace {

QString normalizedLineEnding(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("cr") || value == QStringLiteral("lf") || value == QStringLiteral("crlf")) {
        return value;
    }
    return QStringLiteral("none");
}

} // namespace

ScriptBridge::ScriptBridge(QObject *parent) : QObject(parent) {}

void ScriptBridge::setSendTextCallback(SendCallback callback) { m_sendTextCallback = std::move(callback); }

void ScriptBridge::setSendHexCallback(SendCallback callback) { m_sendHexCallback = std::move(callback); }

void ScriptBridge::setReceivedTextCallback(StringCallback callback) { m_receivedTextCallback = std::move(callback); }

void ScriptBridge::setReceivedHexCallback(StringCallback callback) { m_receivedHexCallback = std::move(callback); }

void ScriptBridge::setLastRxTextCallback(StringCallback callback) { m_lastRxTextCallback = std::move(callback); }

void ScriptBridge::setLastRxHexCallback(StringCallback callback) { m_lastRxHexCallback = std::move(callback); }

void ScriptBridge::setRecordsCallback(RecordsCallback callback) { m_recordsCallback = std::move(callback); }

void ScriptBridge::setStopCallback(StopCallback callback) { m_stopCallback = std::move(callback); }

bool ScriptBridge::sendText(const QString &payload, const QString &lineEnding)
{
    return sendWithCallback(m_sendTextCallback, payload, lineEnding);
}

bool ScriptBridge::sendHex(const QString &payload, const QString &lineEnding)
{
    return sendWithCallback(m_sendHexCallback, payload, lineEnding);
}

QString ScriptBridge::receivedText() const { return stringFromCallback(m_receivedTextCallback); }

QString ScriptBridge::receivedHex() const { return stringFromCallback(m_receivedHexCallback); }

QString ScriptBridge::lastRxText() const { return stringFromCallback(m_lastRxTextCallback); }

QString ScriptBridge::lastRxHex() const { return stringFromCallback(m_lastRxHexCallback); }

QVariantList ScriptBridge::records() const { return m_recordsCallback ? m_recordsCallback() : QVariantList(); }

void ScriptBridge::log(const QString &message)
{
    m_lastError.clear();
    emit logMessage(message);
}

void ScriptBridge::sleep(int milliseconds)
{
    const int duration = qBound(0, milliseconds, 600000);
    QElapsedTimer timer;
    timer.start();
    while (!stopRequested() && timer.elapsed() < duration) {
        const int remaining = duration - static_cast<int>(timer.elapsed());
        QThread::msleep(static_cast<unsigned long>(qMin(remaining, 50)));
    }
}

bool ScriptBridge::stopRequested() const { return m_stopCallback ? m_stopCallback() : false; }

QString ScriptBridge::lastError() const { return m_lastError; }

bool ScriptBridge::sendWithCallback(const SendCallback &callback, const QString &payload, const QString &lineEnding)
{
    if (stopRequested()) {
        m_lastError = AppI18n::text("脚本已停止");
        emit logMessage(QStringLiteral("[ERROR] %1").arg(m_lastError));
        return false;
    }
    if (!callback) {
        m_lastError = AppI18n::text("脚本发送接口不可用");
        emit logMessage(QStringLiteral("[ERROR] %1").arg(m_lastError));
        return false;
    }

    QString error;
    const bool ok = callback(payload, normalizedLineEnding(lineEnding), &error);
    if (!ok) {
        m_lastError = error.isEmpty() ? AppI18n::text("发送失败") : error;
        emit logMessage(QStringLiteral("[ERROR] %1").arg(m_lastError));
        return false;
    }
    m_lastError.clear();
    return true;
}

QString ScriptBridge::stringFromCallback(const StringCallback &callback) const { return callback ? callback() : QString(); }

ScriptRunner::ScriptRunner(QObject *parent) : QObject(parent) {}

ScriptRunner::~ScriptRunner() { requestStop(); }

void ScriptRunner::setSendTextCallback(ScriptBridge::SendCallback callback) { m_sendTextCallback = std::move(callback); }

void ScriptRunner::setSendHexCallback(ScriptBridge::SendCallback callback) { m_sendHexCallback = std::move(callback); }

void ScriptRunner::setReceivedTextCallback(ScriptBridge::StringCallback callback)
{
    m_receivedTextCallback = std::move(callback);
}

void ScriptRunner::setReceivedHexCallback(ScriptBridge::StringCallback callback)
{
    m_receivedHexCallback = std::move(callback);
}

void ScriptRunner::setLastRxTextCallback(ScriptBridge::StringCallback callback)
{
    m_lastRxTextCallback = std::move(callback);
}

void ScriptRunner::setLastRxHexCallback(ScriptBridge::StringCallback callback)
{
    m_lastRxHexCallback = std::move(callback);
}

void ScriptRunner::setRecordsCallback(ScriptBridge::RecordsCallback callback) { m_recordsCallback = std::move(callback); }

void ScriptRunner::requestStop()
{
    m_stopRequested.store(true);
    QMutexLocker locker(&m_engineMutex);
    if (m_engine) {
        m_engine->setInterrupted(true);
    }
}

void ScriptRunner::runScript(const QString &script, const QString &fileName)
{
    m_stopRequested.store(false);
    emit started();

    if (script.trimmed().isEmpty()) {
        emit finished(false, AppI18n::text("脚本为空"));
        return;
    }

    QJSEngine engine;
    {
        QMutexLocker locker(&m_engineMutex);
        m_engine = &engine;
    }

    ScriptBridge bridge;
    bridge.setSendTextCallback(m_sendTextCallback);
    bridge.setSendHexCallback(m_sendHexCallback);
    bridge.setReceivedTextCallback(m_receivedTextCallback);
    bridge.setReceivedHexCallback(m_receivedHexCallback);
    bridge.setLastRxTextCallback(m_lastRxTextCallback);
    bridge.setLastRxHexCallback(m_lastRxHexCallback);
    bridge.setRecordsCallback(m_recordsCallback);
    bridge.setStopCallback([this]() { return stopRequested(); });
    connect(&bridge, &ScriptBridge::logMessage, this, &ScriptRunner::logMessage);

    engine.globalObject().setProperty(QStringLiteral("serial"), engine.newQObject(&bridge));

    QStringList stackTrace;
    const QString effectiveName = fileName.trimmed().isEmpty() ? QStringLiteral("script.js") : fileName;
    const QJSValue result = engine.evaluate(script, effectiveName, 1, &stackTrace);
    {
        QMutexLocker locker(&m_engineMutex);
        m_engine = nullptr;
    }

    if (stopRequested()) {
        emit finished(false, AppI18n::text("脚本已停止"));
        return;
    }
    if (result.isError()) {
        emit finished(false, errorText(result, stackTrace));
        return;
    }

    emit finished(true, AppI18n::text("脚本执行完成"));
}

bool ScriptRunner::stopRequested() const { return m_stopRequested.load(); }

QString ScriptRunner::errorText(const QJSValue &value, const QStringList &stackTrace) const
{
    const int line = value.property(QStringLiteral("lineNumber")).toInt();
    QString message = line > 0 ? AppI18n::text("第 %1 行：%2").arg(line).arg(value.toString()) : value.toString();
    if (!stackTrace.isEmpty()) {
        message.append(QStringLiteral("\n"));
        message.append(stackTrace.join(QLatin1Char('\n')));
    }
    return message;
}
