#include "app/view/workbench/workbench_page_internal.h"

#include "app/core/app_i18n.h"
#include "app/view/data_table_window.h"

#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>

using namespace WorkbenchPagePrivate;

void WorkbenchPage::showDataTableWindow()
{
    if (!m_dataTableWindow) {
        m_dataTableWindow = new DataTableWindow(this);
        connect(m_dataTableWindow, &DataTableWindow::refreshRequested, this, &WorkbenchPage::refreshDataTableWindow);
        connect(m_dataTableWindow, &DataTableWindow::locateRequested, this, &WorkbenchPage::locateRecordInTerminal);
    }

    refreshDataTableWindow();
    m_dataTableWindow->show();
    m_dataTableWindow->raise();
    m_dataTableWindow->activateWindow();
}

void WorkbenchPage::refreshDataTableWindow()
{
    if (m_dataTableWindow) {
        m_dataTableWindow->setRecords(dataTableRecords());
    }
}

QVector<DataTableRecord> WorkbenchPage::dataTableRecords() const
{
    QVector<DataTableRecord> rows;
    rows.reserve(m_records.size());
    for (int i = 0; i < m_records.size(); ++i) {
        const SessionRecord &record = m_records.at(i);
        if (record.direction == RecordDirection::FrameBreak) {
            continue;
        }
        rows.append(dataTableRecord(i, record));
    }
    return rows;
}

DataTableRecord WorkbenchPage::dataTableRecord(int recordIndex, const SessionRecord &record) const
{
    DataTableRecord row;
    row.recordIndex = recordIndex;
    row.timestamp = record.timestamp;
    row.direction = directionText(record.direction);
    row.source = record.sourceLabel;
    row.length = record.bytes.size();
    row.hex = bytesToHex(record.bytes);
    row.text = record.displayText;

    const AppChecksum::ChecksumResult checksum =
        AppChecksum::calculate(record.bytes, checksumAlgorithmKey(), checksumByteOrder());
    row.checksum = checksum.ok ? QStringLiteral("%1: %2").arg(AppChecksum::labelForAlgorithm(checksumAlgorithmKey()),
                                                               bytesToHex(checksum.bytes))
                               : checksum.errorMessage;
    return row;
}

void WorkbenchPage::locateRecordInTerminal(int recordIndex)
{
    if (!m_terminalView || recordIndex < 0 || recordIndex >= m_records.size() ||
        m_records.at(recordIndex).direction == RecordDirection::FrameBreak) {
        showWarning(AppI18n::text("无法定位"), AppI18n::text("记录已被清理或不存在"));
        return;
    }

    if (m_terminalFilterCombo && m_terminalFilterCombo->currentData().toString() != QStringLiteral("all")) {
        const int allIndex = m_terminalFilterCombo->findData(QStringLiteral("all"));
        if (allIndex >= 0) {
            m_terminalFilterCombo->setCurrentIndex(allIndex);
        }
    }
    if (m_records.at(recordIndex).direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
        m_showTxCheck->setChecked(true);
    }

    renderTerminal();

    int blockNumber = -1;
    bool hasOutput = false;
    bool found = false;
    for (int i = m_terminalStartRecord; i < m_records.size(); ++i) {
        const SessionRecord &record = m_records.at(i);
        if (!recordMatchesTerminalFilter(record)) {
            continue;
        }
        if (record.direction == RecordDirection::Tx && m_showTxCheck && !m_showTxCheck->isChecked()) {
            continue;
        }
        if (record.direction == RecordDirection::FrameBreak) {
            if (hasOutput) {
                ++blockNumber;
            }
            continue;
        }

        blockNumber = hasOutput ? blockNumber + 1 : 0;
        hasOutput = true;
        if (i == recordIndex) {
            found = true;
            break;
        }
    }

    if (!found || blockNumber < 0) {
        showWarning(AppI18n::text("无法定位"), AppI18n::text("记录已被过滤或不在当前终端范围内"));
        return;
    }

    const QTextBlock block = m_terminalView->document()->findBlockByNumber(blockNumber);
    if (!block.isValid()) {
        showWarning(AppI18n::text("无法定位"), AppI18n::text("记录已被过滤或不在当前终端范围内"));
        return;
    }

    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    m_terminalView->setTextCursor(cursor);
    m_terminalView->ensureCursorVisible();
    m_terminalView->setFocus(Qt::OtherFocusReason);
}
