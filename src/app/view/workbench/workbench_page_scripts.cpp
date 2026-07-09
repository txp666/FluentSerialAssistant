#include "app/view/workbench/workbench_page_internal.h"

#include "app/core/script_runner.h"

#include <QtCore/QMetaObject>
#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtCore/QTime>
#include <QtCore/QVariantMap>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

QString normalizedScriptLineEnding(QString value)
{
    value = value.trimmed().toLower();
    if (value == QStringLiteral("cr") || value == QStringLiteral("lf") || value == QStringLiteral("crlf")) {
        return value;
    }
    return QStringLiteral("none");
}

QString scriptFileFilter()
{
    return AppI18n::text("JavaScript 文件 (*.js);;所有文件 (*)");
}

QString scriptExample()
{
    return QStringLiteral(
        "// serial.sendText(text, lineEnding)  lineEnding: none | cr | lf | crlf\n"
        "// serial.sendHex(hex, lineEnding)\n"
        "// serial.receivedText(), serial.receivedHex(), serial.lastRxText(), serial.records()\n"
        "serial.log('records: ' + serial.records().length);\n"
        "serial.sendText('AT', 'crlf');\n"
        "serial.sleep(200);\n"
        "serial.log('last rx: ' + serial.lastRxText());\n");
}

QString timestampedScriptLog(const QString &message)
{
    return QStringLiteral("[%1] %2").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), message);
}

} // namespace

void WorkbenchPage::updateScriptActionState()
{
    const bool running = m_scriptRunning || m_scriptRunner != nullptr;
    const bool hasScript = m_scriptEdit && !m_scriptEdit->toPlainText().trimmed().isEmpty();

    if (m_scriptEdit) {
        m_scriptEdit->setEnabled(!running);
    }
    if (m_scriptLoadButton) {
        m_scriptLoadButton->setEnabled(!running);
    }
    if (m_scriptSaveButton) {
        m_scriptSaveButton->setEnabled(!running && hasScript);
    }
    if (m_scriptExampleButton) {
        m_scriptExampleButton->setEnabled(!running);
    }
    if (m_scriptRunButton) {
        m_scriptRunButton->setEnabled(!running && hasScript);
    }
    if (m_scriptStopButton) {
        m_scriptStopButton->setEnabled(running);
    }
    if (m_scriptClearLogButton) {
        m_scriptClearLogButton->setEnabled(m_scriptLogEdit && !m_scriptLogEdit->toPlainText().isEmpty());
    }
}

void WorkbenchPage::loadScriptFile()
{
    const QString path = QFileDialog::getOpenFileName(this, AppI18n::text("载入脚本"),
                                                      m_scriptFilePath.isEmpty() ? defaultExportFolder()
                                                                                 : QFileInfo(m_scriptFilePath).path(),
                                                      scriptFileFilter());
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(AppI18n::text("载入脚本失败"), file.errorString());
        return;
    }

    m_scriptEdit->setPlainText(QString::fromUtf8(file.readAll()));
    m_scriptFilePath = path;
    if (m_scriptStatusLabel) {
        m_scriptStatusLabel->setText(QFileInfo(path).fileName());
    }
    appendScriptLog(AppI18n::text("已载入脚本：%1").arg(path));
    updateScriptActionState();
}

void WorkbenchPage::saveScriptFile()
{
    if (!m_scriptEdit) {
        return;
    }

    QString path = m_scriptFilePath;
    if (path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, AppI18n::text("保存脚本"), defaultExportFolder(), scriptFileFilter());
        if (path.isEmpty()) {
            return;
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        showError(AppI18n::text("保存脚本失败"), file.errorString());
        return;
    }

    file.write(m_scriptEdit->toPlainText().toUtf8());
    if (file.error() != QFile::NoError) {
        showError(AppI18n::text("保存脚本失败"), file.errorString());
        return;
    }

    m_scriptFilePath = path;
    if (m_scriptStatusLabel) {
        m_scriptStatusLabel->setText(QFileInfo(path).fileName());
    }
    appendScriptLog(AppI18n::text("已保存脚本：%1").arg(path));
}

void WorkbenchPage::insertScriptExample()
{
    if (!m_scriptEdit || m_scriptRunning) {
        return;
    }
    m_scriptEdit->setPlainText(scriptExample());
    m_scriptFilePath.clear();
    if (m_scriptStatusLabel) {
        m_scriptStatusLabel->setText(AppI18n::text("示例脚本"));
    }
    updateScriptActionState();
}

void WorkbenchPage::startScript()
{
    if (!m_scriptEdit || m_scriptRunner || m_scriptRunning) {
        return;
    }

    const QString script = m_scriptEdit->toPlainText();
    if (script.trimmed().isEmpty()) {
        showWarning(AppI18n::text("无法运行脚本"), AppI18n::text("脚本为空"));
        updateScriptActionState();
        return;
    }

    auto *thread = new QThread(this);
    auto *runner = new ScriptRunner;
    const QString fileName = m_scriptFilePath.isEmpty() ? QStringLiteral("script.js") : m_scriptFilePath;
    QPointer<WorkbenchPage> page(this);

    runner->setSendTextCallback([page](const QString &payload, const QString &lineEnding, QString *error) {
        if (!page) {
            if (error) {
                *error = AppI18n::text("脚本窗口已关闭");
            }
            return false;
        }

        bool ok = false;
        QString localError;
        QMetaObject::invokeMethod(page, [&]() {
            ok = page->sendScriptPayload(payload, QStringLiteral("text"), lineEnding, &localError);
        }, Qt::BlockingQueuedConnection);
        if (error) {
            *error = localError;
        }
        return ok;
    });

    runner->setSendHexCallback([page](const QString &payload, const QString &lineEnding, QString *error) {
        if (!page) {
            if (error) {
                *error = AppI18n::text("脚本窗口已关闭");
            }
            return false;
        }

        bool ok = false;
        QString localError;
        QMetaObject::invokeMethod(page, [&]() {
            ok = page->sendScriptPayload(payload, QStringLiteral("hex"), lineEnding, &localError);
        }, Qt::BlockingQueuedConnection);
        if (error) {
            *error = localError;
        }
        return ok;
    });

    runner->setReceivedTextCallback([page]() {
        QString value;
        if (page) {
            QMetaObject::invokeMethod(page, [&]() { value = page->scriptReceivedTextSnapshot(); },
                                      Qt::BlockingQueuedConnection);
        }
        return value;
    });
    runner->setReceivedHexCallback([page]() {
        QString value;
        if (page) {
            QMetaObject::invokeMethod(page, [&]() { value = page->scriptReceivedHexSnapshot(); },
                                      Qt::BlockingQueuedConnection);
        }
        return value;
    });
    runner->setLastRxTextCallback([page]() {
        QString value;
        if (page) {
            QMetaObject::invokeMethod(page, [&]() { value = page->scriptLastRxTextSnapshot(); },
                                      Qt::BlockingQueuedConnection);
        }
        return value;
    });
    runner->setLastRxHexCallback([page]() {
        QString value;
        if (page) {
            QMetaObject::invokeMethod(page, [&]() { value = page->scriptLastRxHexSnapshot(); },
                                      Qt::BlockingQueuedConnection);
        }
        return value;
    });
    runner->setRecordsCallback([page]() {
        QVariantList records;
        if (page) {
            QMetaObject::invokeMethod(page, [&]() { records = page->scriptRecordSnapshot(); },
                                      Qt::BlockingQueuedConnection);
        }
        return records;
    });

    runner->moveToThread(thread);
    m_scriptThread = thread;
    m_scriptRunner = runner;
    m_scriptRunning = true;
    if (m_scriptStatusLabel) {
        m_scriptStatusLabel->setText(AppI18n::text("脚本运行中"));
    }
    appendScriptLog(AppI18n::text("开始运行脚本"));
    updateScriptActionState();

    connect(thread, &QThread::started, runner, [runner, script, fileName]() { runner->runScript(script, fileName); });
    connect(runner, &ScriptRunner::logMessage, this, &WorkbenchPage::appendScriptLog);
    connect(runner, &ScriptRunner::finished, this, &WorkbenchPage::finishScriptRun);
    connect(runner, &ScriptRunner::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, runner, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread, runner]() {
        if (m_scriptRunner == runner) {
            m_scriptRunner = nullptr;
        }
        if (m_scriptThread == thread) {
            m_scriptThread = nullptr;
        }
        updateScriptActionState();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void WorkbenchPage::stopScript()
{
    if (!m_scriptRunner) {
        return;
    }
    appendScriptLog(AppI18n::text("正在停止脚本..."));
    if (m_scriptStatusLabel) {
        m_scriptStatusLabel->setText(AppI18n::text("正在停止脚本..."));
    }
    m_scriptRunner->requestStop();
}

void WorkbenchPage::appendScriptLog(const QString &message)
{
    if (!m_scriptLogEdit) {
        return;
    }
    m_scriptLogEdit->appendPlainText(timestampedScriptLog(message));
    updateScriptActionState();
}

void WorkbenchPage::clearScriptLog()
{
    if (m_scriptLogEdit) {
        m_scriptLogEdit->clear();
    }
    updateScriptActionState();
}

void WorkbenchPage::finishScriptRun(bool ok, const QString &message)
{
    m_scriptRunning = false;
    appendScriptLog((ok ? AppI18n::text("脚本完成：%1") : AppI18n::text("脚本失败：%1")).arg(message));
    if (m_scriptStatusLabel) {
        m_scriptStatusLabel->setText(ok ? AppI18n::text("脚本执行完成") : AppI18n::text("脚本执行失败"));
    }
    if (ok) {
        showSuccess(AppI18n::text("脚本执行完成"), message);
    } else if (message == AppI18n::text("脚本已停止")) {
        showInfo(AppI18n::text("脚本已停止"), message);
    } else {
        showError(AppI18n::text("脚本执行失败"), message);
    }
    updateScriptActionState();
}

bool WorkbenchPage::sendScriptPayload(const QString &payloadText, const QString &mode, const QString &lineEndingKey,
                                      QString *error)
{
    if (error) {
        error->clear();
    }
    if (!m_serial.isOpen()) {
        if (error) {
            *error = AppI18n::text("请先连接串口");
        }
        return false;
    }

    QByteArray payload;
    if (mode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(payloadText);
        if (!result.ok) {
            if (error) {
                *error = result.errorOffset >= 0
                             ? AppI18n::text("%1，位置 %2").arg(result.errorMessage).arg(result.errorOffset + 1)
                             : result.errorMessage;
            }
            return false;
        }
        payload = result.bytes;
    } else {
        const AppTextEncoding::EncodeResult result = AppTextEncoding::encode(payloadText, sendEncodingKey());
        if (!result.ok) {
            if (error) {
                *error = result.errorMessage;
            }
            return false;
        }
        payload = result.bytes;
    }

    payload.append(lineEndingForKey(normalizedScriptLineEnding(lineEndingKey)));
    if (payload.isEmpty()) {
        if (error) {
            *error = AppI18n::text("发送内容为空");
        }
        return false;
    }

    QByteArray output = payload;
    if (m_checksumAppendCheck && m_checksumAppendCheck->isChecked()) {
        const AppChecksum::ChecksumResult checksum =
            AppChecksum::calculate(payload, checksumAlgorithmKey(), checksumByteOrder());
        if (!checksum.ok) {
            if (error) {
                *error = checksum.errorMessage;
            }
            return false;
        }
        output.append(checksum.bytes);
        setChecksumResultText(QStringLiteral("%1 0x%2 · %3")
                                  .arg(AppChecksum::labelForAlgorithm(checksumAlgorithmKey()),
                                       QStringLiteral("%1")
                                           .arg(checksum.value, AppChecksum::widthForAlgorithm(checksumAlgorithmKey()) * 2,
                                                16, QLatin1Char('0'))
                                           .toUpper(),
                                       bytesToHex(checksum.bytes)));
    }

    QString writeError;
    if (!m_serial.writeData(output, &writeError)) {
        if (error) {
            *error = writeError;
        }
        return false;
    }

    SendHistoryItem historyItem;
    historyItem.mode = mode == QStringLiteral("hex") ? QStringLiteral("hex") : QStringLiteral("text");
    historyItem.payload = payloadText;
    historyItem.lineEnding = normalizedScriptLineEnding(lineEndingKey);
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, output, true, AppI18n::text("脚本"));
    appendScriptLog(AppI18n::text("脚本发送 %1").arg(formatBytes(output.size())));
    return true;
}

QString WorkbenchPage::scriptReceivedTextSnapshot() const
{
    QStringList lines;
    for (const SessionRecord &record : m_records) {
        if (record.direction == RecordDirection::Rx) {
            lines.append(record.displayText);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString WorkbenchPage::scriptReceivedHexSnapshot() const
{
    QStringList lines;
    for (const SessionRecord &record : m_records) {
        if (record.direction == RecordDirection::Rx) {
            lines.append(bytesToHex(record.bytes));
        }
    }
    return lines.join(QLatin1Char('\n'));
}

QString WorkbenchPage::scriptLastRxTextSnapshot() const
{
    for (int i = m_records.size() - 1; i >= 0; --i) {
        const SessionRecord &record = m_records.at(i);
        if (record.direction == RecordDirection::Rx) {
            return record.displayText;
        }
    }
    return {};
}

QString WorkbenchPage::scriptLastRxHexSnapshot() const
{
    for (int i = m_records.size() - 1; i >= 0; --i) {
        const SessionRecord &record = m_records.at(i);
        if (record.direction == RecordDirection::Rx) {
            return bytesToHex(record.bytes);
        }
    }
    return {};
}

QVariantList WorkbenchPage::scriptRecordSnapshot() const
{
    QVariantList records;
    records.reserve(m_records.size());
    for (const SessionRecord &record : m_records) {
        if (record.direction == RecordDirection::FrameBreak) {
            continue;
        }

        QVariantMap item;
        item.insert(QStringLiteral("time"), record.timestamp.toString(Qt::ISODateWithMs));
        item.insert(QStringLiteral("direction"), record.direction == RecordDirection::Tx ? QStringLiteral("tx")
                                                                                         : QStringLiteral("rx"));
        item.insert(QStringLiteral("source"), record.sourceLabel);
        item.insert(QStringLiteral("length"), record.bytes.size());
        item.insert(QStringLiteral("hex"), bytesToHex(record.bytes));
        item.insert(QStringLiteral("text"), record.displayText);
        records.append(item);
    }
    return records;
}
