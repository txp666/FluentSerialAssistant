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

QString WorkbenchPage::frameBreakModeKey() const
{
    const QString mode = m_frameModeCombo ? m_frameModeCombo->currentData().toString() : QStringLiteral("timeout");
    if (mode == QStringLiteral("header") || mode == QStringLiteral("tail") || mode == QStringLiteral("length")) {
        return mode;
    }
    return QStringLiteral("timeout");
}

QByteArray WorkbenchPage::frameBoundaryPattern(bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    if (!m_framePatternEdit) {
        return {};
    }
    const QString patternText = m_framePatternEdit->text().trimmed();
    if (patternText.isEmpty()) {
        return {};
    }

    const HexParseResult result = parseHexPayload(patternText);
    if (!result.ok || result.bytes.isEmpty()) {
        return {};
    }
    if (ok) {
        *ok = true;
    }
    return result.bytes;
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

bool WorkbenchPage::terminalSearchCaseSensitive() const
{
    return m_terminalSearchCaseCheck && m_terminalSearchCaseCheck->isChecked();
}

bool WorkbenchPage::terminalSearchRegexEnabled() const
{
    return m_terminalSearchRegexCheck && m_terminalSearchRegexCheck->isChecked();
}

WorkbenchPage::TerminalSearchQuery WorkbenchPage::terminalSearchQuery() const
{
    TerminalSearchQuery query;
    query.text = terminalSearchText();
    query.caseSensitivity = terminalSearchCaseSensitive() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    query.regexEnabled = terminalSearchRegexEnabled();
    if (query.text.isEmpty() || !query.regexEnabled) {
        return query;
    }

    QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
    if (query.caseSensitivity == Qt::CaseInsensitive) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    query.regex = QRegularExpression(query.text, options);
    query.valid = query.regex.isValid();
    if (!query.valid) {
        query.errorMessage = query.regex.errorString();
    }
    return query;
}

QList<WorkbenchPage::SearchMatchRange> WorkbenchPage::terminalSearchRanges(const QString &text,
                                                                           const TerminalSearchQuery &query) const
{
    QList<SearchMatchRange> ranges;
    if (query.text.isEmpty() || !query.valid || text.isEmpty()) {
        return ranges;
    }

    if (query.regexEnabled) {
        QRegularExpressionMatchIterator iterator = query.regex.globalMatch(text);
        while (iterator.hasNext()) {
            const QRegularExpressionMatch match = iterator.next();
            const int start = match.capturedStart();
            const int length = match.capturedLength();
            if (start >= 0 && length > 0) {
                ranges.append({start, length});
            }
        }
        return ranges;
    }

    int index = 0;
    const int queryLength = static_cast<int>(query.text.size());
    while ((index = text.indexOf(query.text, index, query.caseSensitivity)) >= 0) {
        ranges.append({index, queryLength});
        index += qMax(1, queryLength);
    }
    return ranges;
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

    QString marker = record.direction == RecordDirection::Tx ? QStringLiteral("»") : QStringLiteral("«");
    if (!record.sourceLabel.trimmed().isEmpty()) {
        marker = QStringLiteral("%1[%2]").arg(marker, record.sourceLabel.trimmed());
    }
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
        return terminalDirectionFilter() == QStringLiteral("all");
    }

    const QString directionFilter = terminalDirectionFilter();
    if (directionFilter == QStringLiteral("rx") && record.direction != RecordDirection::Rx) {
        return false;
    }
    if (directionFilter == QStringLiteral("tx") && record.direction != RecordDirection::Tx) {
        return false;
    }

    return true;
}

void WorkbenchPage::handleReceivedData(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    handleMacroReceivedData(data);
    handleAutoReplyReceivedData(data);

    if (!m_autoFrameBreakCheck || !m_autoFrameBreakCheck->isChecked() ||
        frameBreakModeKey() == QStringLiteral("timeout")) {
        appendRecord(RecordDirection::Rx, data);
        return;
    }

    recordReceivedBytes(data);
    processBufferedFrameData(data);
}

void WorkbenchPage::recordReceivedBytes(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

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
    updateCounters();
}

void WorkbenchPage::processBufferedFrameData(const QByteArray &data)
{
    const QString mode = frameBreakModeKey();
    if (mode == QStringLiteral("length")) {
        const int frameLength = m_frameFixedLengthSpin ? qBound(1, m_frameFixedLengthSpin->value(), 65536) : 1;
        m_rxFrameBuffer.append(data);
        while (m_rxFrameBuffer.size() >= frameLength) {
            appendRecord(RecordDirection::Rx, m_rxFrameBuffer.left(frameLength), false);
            m_rxFrameBuffer.remove(0, frameLength);
        }
        return;
    }

    bool patternOk = false;
    const QByteArray pattern = frameBoundaryPattern(&patternOk);
    if (!patternOk || pattern.isEmpty()) {
        appendRecord(RecordDirection::Rx, data, false);
        return;
    }

    m_rxFrameBuffer.append(data);
    if (mode == QStringLiteral("tail")) {
        while (true) {
            const int index = m_rxFrameBuffer.indexOf(pattern);
            if (index < 0) {
                break;
            }
            const int frameEnd = index + pattern.size();
            appendRecord(RecordDirection::Rx, m_rxFrameBuffer.left(frameEnd), false);
            m_rxFrameBuffer.remove(0, frameEnd);
        }
    } else if (mode == QStringLiteral("header")) {
        while (true) {
            const int firstHeader = m_rxFrameBuffer.indexOf(pattern);
            if (firstHeader < 0) {
                const int keepBytes = qMin(pattern.size() - 1, m_rxFrameBuffer.size());
                const int emitBytes = m_rxFrameBuffer.size() - keepBytes;
                if (emitBytes > 0) {
                    appendRecord(RecordDirection::Rx, m_rxFrameBuffer.left(emitBytes), false);
                    m_rxFrameBuffer.remove(0, emitBytes);
                }
                break;
            }
            if (firstHeader > 0) {
                appendRecord(RecordDirection::Rx, m_rxFrameBuffer.left(firstHeader), false);
                m_rxFrameBuffer.remove(0, firstHeader);
            }

            const int nextHeader = m_rxFrameBuffer.indexOf(pattern, pattern.size());
            if (nextHeader < 0) {
                break;
            }
            appendRecord(RecordDirection::Rx, m_rxFrameBuffer.left(nextHeader), false);
            m_rxFrameBuffer.remove(0, nextHeader);
        }
    }

    if (m_rxFrameBuffer.size() > MaxFrameBufferBytes) {
        flushRxFrameBuffer();
    }
}

void WorkbenchPage::flushRxFrameBuffer()
{
    if (m_rxFrameBuffer.isEmpty()) {
        return;
    }
    const QByteArray data = m_rxFrameBuffer;
    m_rxFrameBuffer.clear();
    appendRecord(RecordDirection::Rx, data, false);
}

void WorkbenchPage::updateFrameControlState()
{
    const bool enabled = m_autoFrameBreakCheck && m_autoFrameBreakCheck->isChecked();
    const QString mode = frameBreakModeKey();
    if (m_frameModeCombo) {
        m_frameModeCombo->setEnabled(true);
    }
    if (m_framePatternEdit) {
        m_framePatternEdit->setEnabled(enabled && (mode == QStringLiteral("header") || mode == QStringLiteral("tail")));
    }
    if (m_frameFixedLengthSpin) {
        m_frameFixedLengthSpin->setEnabled(enabled && mode == QStringLiteral("length"));
    }
    if (m_frameBreakIntervalSpin) {
        m_frameBreakIntervalSpin->setEnabled(enabled && mode == QStringLiteral("timeout"));
    }
}

void WorkbenchPage::appendRecord(RecordDirection direction, const QByteArray &data, bool updateStats,
                                 const QString &sourceLabel)
{
    if (data.isEmpty()) {
        return;
    }

    const QDateTime now = QDateTime::currentDateTime();
    if (direction == RecordDirection::Rx && m_autoFrameBreakCheck && m_autoFrameBreakCheck->isChecked() &&
        frameBreakModeKey() == QStringLiteral("timeout") && m_lastRxTimestamp.isValid()) {
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
    record.sourceLabel = sourceLabel;
    m_records.append(record);
    m_pendingRecordIndexes.append(m_records.size() - 1);
    writeAutoLogRecord(record);
    appendQuickPlotRecord(record);
    if (direction == RecordDirection::Rx) {
        updateModbusResponseStatus(data);
    }

    if (direction == RecordDirection::Rx && updateStats) {
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
    } else if (direction == RecordDirection::Tx && updateStats) {
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

    const TerminalSearchQuery query = terminalSearchQuery();
    const int previousSearchMatch = m_terminalCurrentSearchMatch;
    m_terminalSearchMatches.clear();
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
        if (appendRecordToTerminal(cursor, m_records.at(i), hasOutput, query)) {
            hasOutput = true;
        }
    }
    cursor.endEditBlock();

    m_pendingRecordIndexes.clear();
    if (!query.text.isEmpty() && query.valid && !m_terminalSearchMatches.isEmpty()) {
        m_terminalCurrentSearchMatch =
            previousSearchMatch >= 0 ? qBound(0, previousSearchMatch, m_terminalSearchMatches.size() - 1) : 0;
        selectTerminalSearchMatch();
        return;
    }

    m_terminalCurrentSearchMatch = -1;
    if (!m_autoScrollCheck || m_autoScrollCheck->isChecked()) {
        cursor.movePosition(QTextCursor::End);
        m_terminalView->setTextCursor(cursor);
    }
    updateCounters();
}

void WorkbenchPage::insertTextWithSearchHighlights(QTextCursor &cursor, const QString &line, int start, int length,
                                                   const QTextCharFormat &format, const QList<SearchMatchRange> &ranges)
{
    if (length <= 0) {
        return;
    }

    const int end = start + length;
    int position = start;
    for (const SearchMatchRange &range : ranges) {
        const int rangeStart = qMax(start, range.start);
        const int rangeEnd = qMin(end, range.start + range.length);
        if (rangeEnd <= position || rangeEnd <= rangeStart) {
            continue;
        }
        if (position < rangeStart) {
            cursor.insertText(line.mid(position, rangeStart - position), format);
        }

        QTextCharFormat highlightFormat = format;
        highlightFormat.setBackground(QColor(255, 214, 10, 96));
        cursor.insertText(line.mid(rangeStart, rangeEnd - rangeStart), highlightFormat);
        position = rangeEnd;
    }

    if (position < end) {
        cursor.insertText(line.mid(position, end - position), format);
    }
}

bool WorkbenchPage::appendRecordToTerminal(QTextCursor &cursor, const SessionRecord &record, bool hasPrevious,
                                           const TerminalSearchQuery &query)
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
    const int lineDocumentStart = cursor.position();
    const QList<SearchMatchRange> searchRanges = terminalSearchRanges(line, query);
    for (const SearchMatchRange &range : searchRanges) {
        m_terminalSearchMatches.append({lineDocumentStart + range.start, range.length});
    }

    const bool showTimestamp = m_timestampCheck && m_timestampCheck->isChecked();
    const QString timestamp = record.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString marker = record.direction == RecordDirection::Tx ? QStringLiteral("»") : QStringLiteral("«");
    int position = 0;
    if (showTimestamp && line.startsWith(timestamp)) {
        QTextCharFormat timestampFormat = format;
        timestampFormat.setForeground(terminalTimestampColor());
        insertTextWithSearchHighlights(cursor, line, 0, timestamp.size(), timestampFormat, searchRanges);
        position = timestamp.size();
    }

    const int markerIndex = line.indexOf(marker, position);
    if (markerIndex >= position) {
        QTextCharFormat markerFormat = format;
        markerFormat.setForeground(terminalDirectionMarkerColor(record.direction == RecordDirection::Tx));
        insertTextWithSearchHighlights(cursor, line, position, markerIndex - position, format, searchRanges);
        insertTextWithSearchHighlights(cursor, line, markerIndex, marker.size(), markerFormat, searchRanges);

        const int afterMarker = markerIndex + marker.size();
        int prefixStart = afterMarker;
        while (prefixStart < line.size() && line.at(prefixStart).isSpace()) {
            ++prefixStart;
        }

        const int prefixEnd = espIdfLogPrefixEnd(line, prefixStart);
        if (prefixEnd > prefixStart) {
            QTextCharFormat levelFormat = format;
            levelFormat.setForeground(terminalEspIdfLogLevelColor(line.at(prefixStart)));
            insertTextWithSearchHighlights(cursor, line, afterMarker, prefixStart - afterMarker, format, searchRanges);
            insertTextWithSearchHighlights(cursor, line, prefixStart, prefixEnd - prefixStart, levelFormat,
                                           searchRanges);
            insertTextWithSearchHighlights(cursor, line, prefixEnd, line.size() - prefixEnd, format, searchRanges);
        } else {
            insertTextWithSearchHighlights(cursor, line, afterMarker, line.size() - afterMarker, format, searchRanges);
        }
        return true;
    }

    if (position > 0) {
        insertTextWithSearchHighlights(cursor, line, position, line.size() - position, format, searchRanges);
        return true;
    }

    insertTextWithSearchHighlights(cursor, line, 0, line.size(), format, searchRanges);
    return true;
}

void WorkbenchPage::resetTerminalSearchNavigation() { m_terminalCurrentSearchMatch = -1; }

void WorkbenchPage::moveTerminalSearchMatch(int direction)
{
    if (terminalSearchText().isEmpty()) {
        return;
    }
    if (m_terminalSearchMatches.isEmpty()) {
        renderTerminal();
    }
    if (m_terminalSearchMatches.isEmpty()) {
        return;
    }

    if (m_terminalCurrentSearchMatch < 0) {
        m_terminalCurrentSearchMatch = direction < 0 ? m_terminalSearchMatches.size() - 1 : 0;
    } else {
        m_terminalCurrentSearchMatch = (m_terminalCurrentSearchMatch + direction + m_terminalSearchMatches.size()) %
                                       m_terminalSearchMatches.size();
    }
    selectTerminalSearchMatch();
}

void WorkbenchPage::selectTerminalSearchMatch()
{
    if (!m_terminalView || m_terminalSearchMatches.isEmpty()) {
        m_terminalCurrentSearchMatch = -1;
        updateCounters();
        return;
    }

    m_terminalCurrentSearchMatch = qBound(0, m_terminalCurrentSearchMatch, m_terminalSearchMatches.size() - 1);
    const TerminalSearchMatch match = m_terminalSearchMatches.at(m_terminalCurrentSearchMatch);
    QTextCursor cursor(m_terminalView->document());
    cursor.setPosition(match.position);
    cursor.setPosition(match.position + match.length, QTextCursor::KeepAnchor);
    m_terminalView->setTextCursor(cursor);
    m_terminalView->ensureCursorVisible();
    updateCounters();
}

void WorkbenchPage::flushPendingLines()
{
    if (m_pauseCheck->isChecked() || m_pendingRecordIndexes.isEmpty()) {
        return;
    }
    if (!terminalSearchText().isEmpty()) {
        renderTerminal();
        return;
    }

    const TerminalSearchQuery query = terminalSearchQuery();
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
            if (appendRecordToTerminal(cursor, m_records.at(index), hasOutput, query)) {
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
