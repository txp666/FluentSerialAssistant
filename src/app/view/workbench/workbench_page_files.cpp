#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

void WorkbenchPage::exportRecords(ExportFormat format)
{
    if (m_records.isEmpty()) {
        showWarning(QStringLiteral("没有可导出的记录"), QStringLiteral("当前会话还没有收发数据"));
        return;
    }

    QSettings settings;
    const QString initialFolder = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    FolderPickerDialog dialog(initialFolder, window());
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString folder = dialog.selectedFolder();
    if (folder.isEmpty()) {
        return;
    }
    settings.setValue(QStringLiteral("export/folder"), folder);

    const QString fileName =
        QStringLiteral("serial_session_%1.%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")), exportSuffix(format));
    const QString path = QDir(folder).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(QStringLiteral("导出失败"), file.errorString());
        return;
    }

    if (format == ExportFormat::Bin) {
        for (const SessionRecord &record : m_records) {
            if (record.direction == RecordDirection::FrameBreak) {
                continue;
            }
            file.write(record.bytes);
        }
    } else if (format == ExportFormat::Csv) {
        QByteArray output("\xEF\xBB\xBF", 3);
        output.append("timestamp,direction,source,length,hex,text\n");
        for (const SessionRecord &record : m_records) {
            if (record.direction == RecordDirection::FrameBreak) {
                continue;
            }
            const QString line = QStringLiteral("%1,%2,%3,%4,%5,%6\n")
                                     .arg(csvEscape(record.timestamp.toString(Qt::ISODateWithMs)),
                                          csvEscape(directionText(record.direction)), csvEscape(record.sourceLabel))
                                     .arg(record.bytes.size())
                                     .arg(csvEscape(bytesToHex(record.bytes)), csvEscape(record.displayText));
            output.append(line.toUtf8());
        }
        file.write(output);
    } else {
        QStringList lines;
        lines.reserve(m_records.size());
        for (const SessionRecord &record : m_records) {
            lines.append(formatRecordLine(record));
        }
        file.write(lines.join(QLatin1Char('\n')).toUtf8());
    }

    file.close();
    showSuccess(QStringLiteral("导出完成"), path);
}

QString WorkbenchPage::autoLogFormatKey() const
{
    return m_autoLogFormatCombo ? m_autoLogFormatCombo->currentData().toString() : QStringLiteral("txt");
}

WorkbenchPage::ExportFormat WorkbenchPage::autoLogFormat() const
{
    const QString format = autoLogFormatKey();
    if (format == QStringLiteral("csv")) {
        return ExportFormat::Csv;
    }
    if (format == QStringLiteral("bin")) {
        return ExportFormat::Bin;
    }
    return ExportFormat::Txt;
}

qint64 WorkbenchPage::autoLogMaxFileBytes() const
{
    const qint64 megabytes = m_autoLogMaxSizeSpin ? qBound(1, m_autoLogMaxSizeSpin->value(), 4096) : 16;
    return megabytes * 1024 * 1024;
}

void WorkbenchPage::updateAutoLog(bool enabled)
{
    QSettings settings;
    settings.setValue(QStringLiteral("log/enabled"), enabled);

    if (!enabled) {
        closeAutoLog();
        return;
    }

    resetAutoLogSession();
    updateAutoLogStatus();
}

void WorkbenchPage::resetAutoLogSession()
{
    if (m_autoLogFile.isOpen()) {
        m_autoLogFile.close();
    }
    m_autoLogFile.setFileName(QString());
    m_autoLogSessionStamp.clear();
    m_autoLogFileIndex = 1;
    m_autoLogCurrentSize = 0;
}

void WorkbenchPage::closeAutoLog()
{
    if (m_autoLogFile.isOpen()) {
        m_autoLogFile.close();
    }
    updateAutoLogStatus();
}

QByteArray WorkbenchPage::serializeLogRecord(const SessionRecord &record, ExportFormat format) const
{
    if (format == ExportFormat::Bin) {
        return record.bytes;
    }

    if (format == ExportFormat::Csv) {
        const QString line = QStringLiteral("%1,%2,%3,%4,%5,%6\n")
                                 .arg(csvEscape(record.timestamp.toString(Qt::ISODateWithMs)),
                                      csvEscape(directionText(record.direction)), csvEscape(record.sourceLabel))
                                 .arg(record.bytes.size())
                                 .arg(csvEscape(bytesToHex(record.bytes)), csvEscape(record.displayText));
        return line.toUtf8();
    }

    const QString source =
        record.sourceLabel.trimmed().isEmpty() ? QString() : QStringLiteral(" [%1]").arg(record.sourceLabel.trimmed());
    const QString line = QStringLiteral("%1 %2%3 len=%4 hex=%5 text=%6\n")
                             .arg(record.timestamp.toString(Qt::ISODateWithMs), directionText(record.direction), source)
                             .arg(record.bytes.size())
                             .arg(bytesToHex(record.bytes), record.displayText);
    return line.toUtf8();
}

bool WorkbenchPage::ensureAutoLogFile(ExportFormat format, qint64 nextBytes)
{
    const qint64 maxBytes = autoLogMaxFileBytes();
    if (m_autoLogFile.isOpen() && m_autoLogCurrentSize > 0 && m_autoLogCurrentSize + nextBytes > maxBytes) {
        m_autoLogFile.close();
        ++m_autoLogFileIndex;
        m_autoLogCurrentSize = 0;
    }

    if (m_autoLogFile.isOpen()) {
        return true;
    }

    QSettings settings;
    const QString folderPath = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    QDir folder(folderPath);
    if (!folder.exists() && !folder.mkpath(QStringLiteral("."))) {
        const QSignalBlocker blocker(m_autoLogCheck);
        if (m_autoLogCheck) {
            m_autoLogCheck->setChecked(false);
        }
        settings.setValue(QStringLiteral("log/enabled"), false);
        showError(QStringLiteral("自动日志失败"), QStringLiteral("无法创建目录：%1").arg(folderPath));
        updateAutoLogStatus();
        return false;
    }

    if (m_autoLogSessionStamp.isEmpty()) {
        m_autoLogSessionStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    }

    QString portName = m_lastConfig.portName;
    if (portName.isEmpty() && m_portCombo) {
        portName = m_portCombo->currentData().toString();
    }
    const QString baudRate = m_lastConfig.baudRate > 0
                                 ? QString::number(m_lastConfig.baudRate)
                                 : (m_baudCombo ? m_baudCombo->currentText().trimmed() : QStringLiteral("baud"));
    const QString fileName = QStringLiteral("serial_log_%1_%2_%3_%4.%5")
                                 .arg(m_autoLogSessionStamp, safeFileNamePart(portName), safeFileNamePart(baudRate))
                                 .arg(m_autoLogFileIndex, 3, 10, QLatin1Char('0'))
                                 .arg(exportSuffix(format));
    const QString path = folder.filePath(fileName);

    m_autoLogFile.setFileName(path);
    if (!m_autoLogFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString error = m_autoLogFile.errorString();
        const QSignalBlocker blocker(m_autoLogCheck);
        if (m_autoLogCheck) {
            m_autoLogCheck->setChecked(false);
        }
        settings.setValue(QStringLiteral("log/enabled"), false);
        m_autoLogFile.setFileName(QString());
        showError(QStringLiteral("自动日志失败"), error);
        updateAutoLogStatus();
        return false;
    }

    m_autoLogCurrentSize = 0;
    if (format == ExportFormat::Csv) {
        const QByteArray header("\xEF\xBB\xBFtimestamp,direction,source,length,hex,text\n");
        m_autoLogFile.write(header);
        m_autoLogCurrentSize += header.size();
    }
    updateAutoLogStatus();
    return true;
}

void WorkbenchPage::writeAutoLogRecord(const SessionRecord &record)
{
    if (record.direction == RecordDirection::FrameBreak || !m_autoLogCheck || !m_autoLogCheck->isChecked()) {
        return;
    }

    const ExportFormat format = autoLogFormat();
    const QByteArray output = serializeLogRecord(record, format);
    if (output.isEmpty() && format != ExportFormat::Bin) {
        return;
    }
    if (!ensureAutoLogFile(format, output.size())) {
        return;
    }

    const qint64 written = m_autoLogFile.write(output);
    if (written != output.size()) {
        const QString error = m_autoLogFile.errorString();
        closeAutoLog();
        const QSignalBlocker blocker(m_autoLogCheck);
        m_autoLogCheck->setChecked(false);
        QSettings().setValue(QStringLiteral("log/enabled"), false);
        showError(QStringLiteral("自动日志失败"), error.isEmpty() ? QStringLiteral("写入不完整") : error);
        return;
    }
    m_autoLogFile.flush();
    m_autoLogCurrentSize += written;
    updateAutoLogStatus();
}

void WorkbenchPage::updateAutoLogStatus()
{
    if (!m_autoLogStatusLabel) {
        return;
    }

    m_autoLogStatusLabel->setToolTip(QString());
    if (!m_autoLogCheck || !m_autoLogCheck->isChecked()) {
        m_autoLogStatusLabel->setText(QStringLiteral("自动日志未启用"));
        return;
    }

    if (m_autoLogFile.isOpen()) {
        const QString path = m_autoLogFile.fileName();
        m_autoLogStatusLabel->setText(QStringLiteral("自动日志：%1 · %2 / %3")
                                          .arg(QFileInfo(path).fileName(), formatBytes(m_autoLogCurrentSize),
                                               formatBytes(autoLogMaxFileBytes())));
        m_autoLogStatusLabel->setToolTip(path);
        return;
    }

    m_autoLogStatusLabel->setText(m_serial.isOpen() ? QStringLiteral("自动日志已启用，等待数据写入")
                                                    : QStringLiteral("自动日志已启用，连接后写入"));
}

void WorkbenchPage::updateReceiveCapture(bool enabled)
{
    QSettings settings;
    settings.setValue(QStringLiteral("receive/saveToFile"), enabled);

    if (!enabled) {
        closeReceiveCapture();
        return;
    }
    if (m_receiveCaptureFile.isOpen()) {
        return;
    }

    const QString folderPath = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    QDir folder(folderPath);
    if (!folder.exists() && !folder.mkpath(QStringLiteral("."))) {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        showError(QStringLiteral("接收保存失败"), QStringLiteral("无法创建目录：%1").arg(folderPath));
        return;
    }

    const QString fileName = QStringLiteral("serial_rx_%1.bin")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
    const QString path = folder.filePath(fileName);
    m_receiveCaptureFile.setFileName(path);
    if (!m_receiveCaptureFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QSignalBlocker blocker(m_saveReceiveCheck);
        m_saveReceiveCheck->setChecked(false);
        showError(QStringLiteral("接收保存失败"), m_receiveCaptureFile.errorString());
        return;
    }

    m_receiveCaptureLabel->setText(QStringLiteral("保存至：%1").arg(path));
}

void WorkbenchPage::closeReceiveCapture()
{
    if (m_receiveCaptureFile.isOpen()) {
        m_receiveCaptureFile.close();
    }
    if (m_receiveCaptureLabel) {
        m_receiveCaptureLabel->setText(m_saveReceiveCheck && m_saveReceiveCheck->isChecked()
                                           ? QStringLiteral("连接后继续保存接收")
                                           : QStringLiteral("接收保存未启用"));
    }
}

void WorkbenchPage::browseSendFile()
{
    QSettings settings;
    const QString initialPath = m_filePathEdit && !m_filePathEdit->text().isEmpty()
                                    ? QFileInfo(m_filePathEdit->text()).absolutePath()
                                    : settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    const QString path = QFileDialog::getOpenFileName(window(), QStringLiteral("选择发送文件"), initialPath);
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo info(path);
    settings.setValue(QStringLiteral("fileSend/path"), path);
    settings.setValue(QStringLiteral("export/folder"), info.absolutePath());
    m_filePathEdit->setText(path);
    m_fileStatusLabel->setText(QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size())));
    updateFileProgress();
}

void WorkbenchPage::startFileSend()
{
    if (!m_serial.isOpen()) {
        showWarning(QStringLiteral("无法发送文件"), QStringLiteral("请先连接串口"));
        return;
    }
    if (m_fileSendFile.isOpen()) {
        return;
    }

    const QString path = m_filePathEdit->text().trimmed();
    if (path.isEmpty()) {
        showWarning(QStringLiteral("未选择文件"), QStringLiteral("请先选择待发送文件"));
        return;
    }

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        showError(QStringLiteral("文件不可用"), QStringLiteral("无法读取：%1").arg(path));
        return;
    }
    if (info.size() <= 0) {
        showWarning(QStringLiteral("文件为空"), QStringLiteral("请选择包含数据的文件"));
        return;
    }

    m_fileSendFile.setFileName(path);
    if (!m_fileSendFile.open(QIODevice::ReadOnly)) {
        showError(QStringLiteral("打开文件失败"), m_fileSendFile.errorString());
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("fileSend/path"), path);
    settings.setValue(QStringLiteral("fileSend/chunkSize"), m_fileChunkSizeSpin->value());
    settings.setValue(QStringLiteral("fileSend/intervalMs"), m_fileIntervalSpin->value());

    m_fileSendTotal = m_fileSendFile.size();
    m_fileSendSent = 0;
    updateFileSendUi(true);
    updateFileProgress();
    sendNextFileChunk();
}

void WorkbenchPage::cancelFileSend()
{
    if (!m_fileSendFile.isOpen()) {
        return;
    }
    m_fileSendTimer.stop();
    m_fileSendFile.close();
    updateFileSendUi(false);
    updateFileProgress();
    showInfo(QStringLiteral("文件发送已取消"), QStringLiteral("已停止发送当前文件"));
}

void WorkbenchPage::sendNextFileChunk()
{
    if (!m_fileSendFile.isOpen()) {
        return;
    }
    if (!m_serial.isOpen()) {
        cancelFileSend();
        showWarning(QStringLiteral("文件发送中止"), QStringLiteral("串口已断开"));
        return;
    }

    if (m_fileSendFile.atEnd()) {
        const QString name = QFileInfo(m_fileSendFile.fileName()).fileName();
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showSuccess(QStringLiteral("文件发送完成"), name);
        return;
    }

    const int chunkSize = qBound(1, m_fileChunkSizeSpin->value(), 65536);
    const QByteArray data = m_fileSendFile.read(chunkSize);
    if (data.isEmpty()) {
        const QString error = m_fileSendFile.errorString();
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showError(QStringLiteral("读取文件失败"), error);
        return;
    }

    QString error;
    if (!m_serial.writeData(data, &error)) {
        m_fileSendFile.close();
        updateFileSendUi(false);
        updateFileProgress();
        showError(QStringLiteral("文件发送失败"), error);
        return;
    }

    m_fileSendSent += data.size();
    appendRecord(RecordDirection::Tx, data);
    updateFileProgress();

    if (m_fileSendFile.atEnd()) {
        sendNextFileChunk();
        return;
    }
    m_fileSendTimer.start(qBound(0, m_fileIntervalSpin->value(), 60000));
}

void WorkbenchPage::updateFileSendUi(bool sending)
{
    if (m_fileBrowseButton) {
        m_fileBrowseButton->setEnabled(!sending);
    }
    if (m_filePathEdit) {
        m_filePathEdit->setEnabled(!sending);
    }
    if (m_fileChunkSizeSpin) {
        m_fileChunkSizeSpin->setEnabled(!sending);
    }
    if (m_fileIntervalSpin) {
        m_fileIntervalSpin->setEnabled(!sending);
    }
    if (m_fileSendButton) {
        m_fileSendButton->setEnabled(!sending && m_serial.isOpen());
    }
    if (m_fileCancelButton) {
        m_fileCancelButton->setEnabled(sending);
    }
}

void WorkbenchPage::updateFileProgress()
{
    const int percent = m_fileSendTotal > 0 ? static_cast<int>((m_fileSendSent * 100) / m_fileSendTotal) : 0;
    if (m_fileProgressBar) {
        m_fileProgressBar->setValue(qBound(0, percent, 100));
    }
    if (!m_fileStatusLabel) {
        return;
    }

    if (m_fileSendFile.isOpen()) {
        m_fileStatusLabel->setText(
            QStringLiteral("发送中：%1 / %2").arg(formatBytes(m_fileSendSent), formatBytes(m_fileSendTotal)));
        return;
    }

    const QString path = m_filePathEdit ? m_filePathEdit->text().trimmed() : QString();
    if (path.isEmpty()) {
        m_fileStatusLabel->setText(QStringLiteral("未选择文件"));
        return;
    }
    const QFileInfo info(path);
    m_fileStatusLabel->setText(info.exists() ? QStringLiteral("%1 · %2").arg(info.fileName(), formatBytes(info.size()))
                                             : QStringLiteral("文件不存在：%1").arg(path));
}
