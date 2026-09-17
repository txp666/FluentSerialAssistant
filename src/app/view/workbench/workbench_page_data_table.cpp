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
        connect(&m_dataTableTimer, &QTimer::timeout, this, &WorkbenchPage::flushDataTableWindow);
        m_dataTableTimer.start(50);
        const auto refreshVisibleTable = [this]() {
            if (m_dataTableWindow->isVisible()) {
                refreshDataTableWindow();
            }
        };
        connect(m_checksumAlgorithmCombo, &FluentQt::ComboBox::currentIndexChanged, this, refreshVisibleTable);
        connect(m_checksumByteOrderCombo, &FluentQt::ComboBox::currentIndexChanged, this, refreshVisibleTable);
        connect(FluentQt::FluentConfig::instance(), &FluentQt::FluentConfig::localeNameChanged, this,
                refreshVisibleTable);
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
        m_dataTableNextRecordIndex = m_firstRecordIndex + m_records.size();
    }
}

void WorkbenchPage::flushDataTableWindow()
{
    if (!m_dataTableWindow || !m_dataTableWindow->isVisible()) {
        return;
    }

    const qint64 nextRecordIndex = m_firstRecordIndex + m_records.size();
    if (m_dataTableNextRecordIndex == nextRecordIndex) {
        return;
    }
    const int first = static_cast<int>(qMax(m_firstRecordIndex, m_dataTableNextRecordIndex) - m_firstRecordIndex);
    QVector<DataTableRecord> rows;
    rows.reserve(m_records.size() - first);
    for (int i = first; i < m_records.size(); ++i) {
        const SessionRecord &record = m_records.at(i);
        if (record.direction != RecordDirection::FrameBreak) {
            rows.append(dataTableRecord(i, record));
        }
    }
    m_dataTableWindow->appendRecords(rows, m_firstRecordIndex);
    m_dataTableNextRecordIndex = nextRecordIndex;
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
    row.recordIndex = m_firstRecordIndex + recordIndex;
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

void WorkbenchPage::locateRecordInTerminal(qint64 recordId)
{
    const qint64 offset = recordId - m_firstRecordIndex;
    if (!m_terminalView || offset < 0 || offset >= m_records.size() ||
        m_records.at(static_cast<int>(offset)).direction == RecordDirection::FrameBreak) {
        showWarning(AppI18n::text("无法定位"), AppI18n::text("记录已被清理或不存在"));
        return;
    }

    const int recordIndex = static_cast<int>(offset);
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
