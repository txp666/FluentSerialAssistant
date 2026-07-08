#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

SerialPortConfig WorkbenchPage::currentSerialConfig() const
{
    bool baudOk = false;
    const qint32 baud = m_baudCombo->currentText().trimmed().toInt(&baudOk);

    SerialPortConfig config;
    config.portName = m_portCombo->currentData().toString();
    config.baudRate = baudOk && baud > 0 ? baud : 115200;
    config.dataBits = dataBitsFromText(m_dataBitsCombo->currentText());
    config.parity = parityFromData(m_parityCombo->currentData());
    config.stopBits = stopBitsFromData(m_stopBitsCombo->currentData());
    config.flowControl = flowControlFromData(m_flowControlCombo->currentData());
    config.requestToSend = m_rtsCheck->isChecked();
    config.dataTerminalReady = m_dtrCheck->isChecked();
    return config;
}

QByteArray WorkbenchPage::currentPayload(bool *ok)
{
    return payloadFromText(m_sendEdit->toPlainText(),
                           m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text"),
                           selectedLineEndingKey(), true, ok);
}

QByteArray WorkbenchPage::payloadFromText(const QString &payloadText, const QString &mode, const QString &lineEndingKey,
                                          bool focusEditorOnError, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    QByteArray data;
    if (mode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(payloadText);
        if (!result.ok) {
            if (focusEditorOnError) {
                m_sendEdit->setFocus();
            }
            showWarning(QStringLiteral("HEX 输入无效"),
                        result.errorOffset >= 0
                            ? QStringLiteral("%1，位置 %2").arg(result.errorMessage).arg(result.errorOffset + 1)
                            : result.errorMessage);
            return data;
        }
        data = result.bytes;
    } else {
        const AppTextEncoding::EncodeResult result = AppTextEncoding::encode(payloadText, sendEncodingKey());
        if (!result.ok) {
            if (focusEditorOnError) {
                m_sendEdit->setFocus();
            }
            showWarning(QStringLiteral("文本编码失败"), result.errorMessage);
            return data;
        }
        data = result.bytes;
    }

    data.append(lineEndingForKey(lineEndingKey));
    if (ok) {
        *ok = true;
    }
    return data;
}

QByteArray WorkbenchPage::selectedLineEnding() const { return lineEndingForKey(selectedLineEndingKey()); }

QByteArray WorkbenchPage::lineEndingForKey(const QString &key) const { return lineEndingBytes(key); }

QString WorkbenchPage::selectedLineEndingKey() const { return m_lineEndingCombo->currentData().toString(); }

QString WorkbenchPage::receiveEncodingKey() const
{
    return AppTextEncoding::normalizedKey(m_receiveEncodingCombo ? m_receiveEncodingCombo->currentData().toString()
                                                                 : AppTextEncoding::defaultKey());
}

QString WorkbenchPage::sendEncodingKey() const
{
    return AppTextEncoding::normalizedKey(m_sendEncodingCombo ? m_sendEncodingCombo->currentData().toString()
                                                              : AppTextEncoding::defaultKey());
}

QString WorkbenchPage::currentDisplayMode() const
{
    return m_displayModeSegment ? m_displayModeSegment->currentItem() : QStringLiteral("text");
}

QString WorkbenchPage::terminalSearchText() const
{
    return m_terminalSearchEdit ? m_terminalSearchEdit->text().trimmed() : QString();
}

QString WorkbenchPage::terminalDirectionFilter() const
{
    return m_terminalFilterCombo ? m_terminalFilterCombo->currentData().toString() : QStringLiteral("all");
}

QString WorkbenchPage::directionText(RecordDirection direction) const
{
    if (direction == RecordDirection::Rx) {
        return QStringLiteral("收到");
    }
    if (direction == RecordDirection::Tx) {
        return QStringLiteral("发送");
    }
    return {};
}

QString WorkbenchPage::historyLabel(const SendHistoryItem &item) const
{
    QString payload = item.payload.simplified();
    if (payload.size() > 42) {
        payload = payload.left(39) + QStringLiteral("...");
    }
    return QStringLiteral("%1 · %2 · %3").arg(modeLabel(item.mode), lineEndingLabel(item.lineEnding), payload);
}

QString WorkbenchPage::exportSuffix(ExportFormat format) const
{
    switch (format) {
    case ExportFormat::Txt:
        return QStringLiteral("txt");
    case ExportFormat::Csv:
        return QStringLiteral("csv");
    case ExportFormat::Bin:
        return QStringLiteral("bin");
    }
    return QStringLiteral("txt");
}

QString WorkbenchPage::formatRecordLine(const SessionRecord &record) const
{
    if (record.direction == RecordDirection::FrameBreak) {
        return {};
    }

    const QString mode = currentDisplayMode();
    QString payload;
    if (mode == QStringLiteral("hex")) {
        payload = bytesToHex(record.bytes);
    } else if (mode == QStringLiteral("mixed")) {
        payload = QStringLiteral("%1    | %2").arg(bytesToHex(record.bytes), record.displayText);
    } else {
        payload = (!m_timestampCheck || !m_timestampCheck->isChecked()) ? record.terminalText : record.displayText;
    }

    const QString marker = record.direction == RecordDirection::Tx ? QStringLiteral("»") : QStringLiteral("«");
    if (m_timestampCheck && m_timestampCheck->isChecked()) {
        return payload.isEmpty()
                   ? QStringLiteral("%1 %2").arg(record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")), marker)
                   : QStringLiteral("%1 %2 %3")
                         .arg(record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")), marker, payload);
    }

    return payload.isEmpty() ? marker : QStringLiteral("%1 %2").arg(marker, payload);
}

QColor WorkbenchPage::selectedTxColor() const
{
    QColor color = m_txColorButton ? m_txColorButton->color() : defaultTxColor();
    if (!color.isValid()) {
        color = defaultTxColor();
    }
    color.setAlpha(255);
    return color;
}

bool WorkbenchPage::recordMatchesTerminalFilter(const SessionRecord &record) const
{
    if (record.direction == RecordDirection::FrameBreak) {
        return terminalSearchText().isEmpty() && terminalDirectionFilter() == QStringLiteral("all");
    }

    const QString directionFilter = terminalDirectionFilter();
    if (directionFilter == QStringLiteral("rx") && record.direction != RecordDirection::Rx) {
        return false;
    }
    if (directionFilter == QStringLiteral("tx") && record.direction != RecordDirection::Tx) {
        return false;
    }

    const QString searchText = terminalSearchText();
    if (searchText.isEmpty()) {
        return true;
    }

    const QString line = formatRecordLine(record);
    return line.contains(searchText, Qt::CaseInsensitive) ||
           bytesToHex(record.bytes).contains(searchText, Qt::CaseInsensitive) ||
           record.displayText.contains(searchText, Qt::CaseInsensitive);
}

void WorkbenchPage::appendRecord(RecordDirection direction, const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (direction == RecordDirection::Rx && m_autoFrameBreakCheck && m_autoFrameBreakCheck->isChecked() &&
        m_lastRxTimestamp.isValid()) {
        const int thresholdMs = m_frameBreakIntervalSpin ? m_frameBreakIntervalSpin->value() : 20;
        if (m_lastRxTimestamp.msecsTo(now) >= thresholdMs) {
            SessionRecord separator;
            separator.timestamp = now;
            separator.direction = RecordDirection::FrameBreak;
            m_records.append(separator);
            m_pendingRecordIndexes.append(m_records.size() - 1);
        }
    }

    SessionRecord record;
    record.timestamp = now;
    record.direction = direction;
    record.bytes = data;
    const QString decoded =
        AppTextEncoding::decode(data, direction == RecordDirection::Rx ? receiveEncodingKey() : sendEncodingKey());
    record.terminalText = AppTextEncoding::toTerminalText(decoded);
    record.displayText = AppTextEncoding::toSingleLineText(decoded);
    m_records.append(record);
    m_pendingRecordIndexes.append(m_records.size() - 1);

    if (direction == RecordDirection::Rx) {
        m_lastRxTimestamp = now;
        m_rxCount += data.size();
        if (m_saveReceiveCheck && m_saveReceiveCheck->isChecked()) {
            if (!m_receiveCaptureFile.isOpen()) {
                updateReceiveCapture(true);
            }
            if (m_receiveCaptureFile.isOpen()) {
                m_receiveCaptureFile.write(data);
                m_receiveCaptureFile.flush();
            }
        }
    } else {
        m_txCount += data.size();
    }

    trimRecords();
    updateCounters();
    flushPendingLines();
}

void WorkbenchPage::trimRecords()
{
    const int maxRecords = maxRecordCount();
    while (m_records.size() > maxRecords) {
        m_records.removeFirst();
        m_terminalStartRecord = qMax(0, m_terminalStartRecord - 1);

        QList<int> adjusted;
        adjusted.reserve(m_pendingRecordIndexes.size());
        for (int index : m_pendingRecordIndexes) {
            if (index > 0) {
                adjusted.append(index - 1);
            }
        }
        m_pendingRecordIndexes = adjusted;
    }
}

void WorkbenchPage::renderTerminal()
{
    if (!m_terminalView) {
        return;
    }

    m_terminalView->document()->setMaximumBlockCount(maxRecordCount());
    m_terminalView->clear();

    QTextCursor cursor = m_terminalView->textCursor();
    cursor.beginEditBlock();
    bool hasOutput = false;
    for (int i = m_terminalStartRecord; i < m_records.size(); ++i) {
        if (!recordMatchesTerminalFilter(m_records.at(i))) {
            continue;
        }
        if (m_records.at(i).direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
            continue;
        }
        if (appendRecordToTerminal(cursor, m_records.at(i), hasOutput)) {
            hasOutput = true;
        }
    }
    cursor.endEditBlock();

    m_pendingRecordIndexes.clear();
    if (!m_autoScrollCheck || m_autoScrollCheck->isChecked()) {
        cursor.movePosition(QTextCursor::End);
        m_terminalView->setTextCursor(cursor);
    }
    updateCounters();
}

bool WorkbenchPage::appendRecordToTerminal(QTextCursor &cursor, const SessionRecord &record, bool hasPrevious)
{
    if (record.direction == RecordDirection::FrameBreak) {
        if (hasPrevious) {
            cursor.insertBlock();
            return true;
        }
        return false;
    }

    if (hasPrevious) {
        cursor.insertBlock();
    }

    QTextCharFormat format;
    if (record.direction == RecordDirection::Tx) {
        format.setForeground(selectedTxColor());
    }
    const QString line = formatRecordLine(record);
    const QString searchText = terminalSearchText();
    if (!searchText.isEmpty() && line.contains(searchText, Qt::CaseInsensitive)) {
        format.setBackground(QColor(255, 214, 10, 72));
    }

    const bool showTimestamp = m_timestampCheck && m_timestampCheck->isChecked();
    const QString timestamp = record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString marker = record.direction == RecordDirection::Tx ? QStringLiteral("»") : QStringLiteral("«");
    int position = 0;
    if (showTimestamp && line.startsWith(timestamp)) {
        QTextCharFormat timestampFormat = format;
        timestampFormat.setForeground(terminalTimestampColor());
        cursor.insertText(timestamp, timestampFormat);
        position = timestamp.size();
    }

    const int markerIndex = line.indexOf(marker, position);
    if (markerIndex >= position) {
        QTextCharFormat markerFormat = format;
        markerFormat.setForeground(terminalDirectionMarkerColor(record.direction == RecordDirection::Tx));
        cursor.insertText(line.mid(position, markerIndex - position), format);
        cursor.insertText(marker, markerFormat);

        const int afterMarker = markerIndex + marker.size();
        int prefixStart = afterMarker;
        while (prefixStart < line.size() && line.at(prefixStart).isSpace()) {
            ++prefixStart;
        }

        const int prefixEnd = espIdfLogPrefixEnd(line, prefixStart);
        if (prefixEnd > prefixStart) {
            QTextCharFormat levelFormat = format;
            levelFormat.setForeground(terminalEspIdfLogLevelColor(line.at(prefixStart)));
            cursor.insertText(line.mid(afterMarker, prefixStart - afterMarker), format);
            cursor.insertText(line.mid(prefixStart, prefixEnd - prefixStart), levelFormat);
            cursor.insertText(line.mid(prefixEnd), format);
        } else {
            cursor.insertText(line.mid(afterMarker), format);
        }
        return true;
    }

    if (position > 0) {
        cursor.insertText(line.mid(position), format);
        return true;
    }

    cursor.insertText(line, format);
    return true;
}

void WorkbenchPage::flushPendingLines()
{
    if (m_pauseCheck->isChecked() || m_pendingRecordIndexes.isEmpty()) {
        return;
    }

    QTextCursor cursor = m_terminalView->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.beginEditBlock();
    bool hasOutput = !m_terminalView->document()->isEmpty();
    bool wrote = false;
    for (int index : m_pendingRecordIndexes) {
        if (index >= m_terminalStartRecord && index >= 0 && index < m_records.size()) {
            if (!recordMatchesTerminalFilter(m_records.at(index))) {
                continue;
            }
            if (m_records.at(index).direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
                continue;
            }
            if (appendRecordToTerminal(cursor, m_records.at(index), hasOutput)) {
                hasOutput = true;
                wrote = true;
            }
        }
    }
    cursor.endEditBlock();
    m_pendingRecordIndexes.clear();
    if (!wrote) {
        return;
    }

    if (!m_autoScrollCheck || m_autoScrollCheck->isChecked()) {
        cursor.movePosition(QTextCursor::End);
        m_terminalView->setTextCursor(cursor);
    }
    updateCounters();
}
