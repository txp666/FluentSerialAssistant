#include "app/core/app_settings.h"
#include "app/view/data_table_window.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSortFilterProxyModel>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>

class WorkbenchDataTableTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentQt::FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
    }

    void init()
    {
        AppSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("terminal/maxRecords"), 1000);
    }

    void batchesLiveRecordsAndSkipsHiddenWindow()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        page.showDataTableWindow();
        auto *window = page.m_dataTableWindow;
        QSignalSpy resets(window->m_model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(window->m_model, &QAbstractItemModel::rowsInserted);
        for (int i = 0; i < 100; ++i) {
            page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("batch"));
        }
        QCOMPARE(window->m_model->rowCount(), 0);
        QTRY_COMPARE(window->m_model->rowCount(), 100);
        QCOMPARE(inserts.size(), 1);
        QCOMPARE(resets.size(), 0);

        window->close();
        for (int i = 0; i < 1200; ++i) {
            page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("hidden"));
        }
        page.flushDataTableWindow();
        QCOMPARE(window->m_model->rowCount(), 100);
        QCOMPARE(inserts.size(), 1);
        page.showDataTableWindow();
        QCOMPARE(window->m_model->rowCount(), 1000);
        QCOMPARE(window->m_model->index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 300);
        QCOMPARE(resets.size(), 1);
    }

    void partialFramesUpdateCountersWithoutWaitingForARecord()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_autoFrameBreakCheck->setChecked(true);
        page.m_frameModeCombo->setCurrentIndex(page.m_frameModeCombo->findData(QStringLiteral("length")));
        page.m_frameFixedLengthEdit->setText(QStringLiteral("4"));
        page.handleReceivedData(QByteArray("ab"));
        QCOMPARE(page.controlStatus().receivedBytes, 2);
        QCOMPARE(page.controlStatus().recordCount, 0);
        QVERIFY(page.m_countersDirty);
        QTRY_VERIFY(!page.m_countersDirty);
        page.handleReceivedData(QByteArray("cd"));
        QCOMPARE(page.controlStatus().receivedBytes, 4);
        QCOMPARE(page.controlStatus().recordCount, 1);
        QCOMPARE(page.controlRecords(1, QStringLiteral("rx")).first().bytes, QByteArray("abcd"));
        QTRY_VERIFY(page.m_pendingRecordIndexes.isEmpty());
        QVERIFY(page.m_terminalView->toPlainText().contains(QStringLiteral("abcd")));
    }

    void terminalBatchesAndResumesAfterTrimming()
    {
        WorkbenchPage page(nullptr, false, false);
        for (int i = 0; i < 100; ++i) {
            page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray::number(i));
        }
        QVERIFY(page.m_terminalView->document()->isEmpty());
        QCOMPARE(page.m_pendingRecordIndexes.size(), 100);
        QTRY_VERIFY(page.m_pendingRecordIndexes.isEmpty());
        QVERIFY(page.m_terminalView->toPlainText().contains(QStringLiteral("99")));
        QVERIFY(!page.m_countersDirty);

        page.m_pauseCheck->setChecked(true);
        const QString pausedText = page.m_terminalView->toPlainText();
        for (int i = 100; i < 1200; ++i) {
            page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("payload-") + QByteArray::number(i));
        }
        QCOMPARE(page.m_firstRecordIndex, 200);
        QCOMPARE(page.m_pendingRecordIndexes.size(), 1000);
        QCOMPARE(page.m_pendingRecordIndexes.first(), 200);
        QCOMPARE(page.m_pendingRecordIndexes.last(), 1199);
        QCOMPARE(page.m_terminalView->toPlainText(), pausedText);
        page.m_pauseCheck->setChecked(false);
        QVERIFY(page.m_pendingRecordIndexes.isEmpty());
        QCOMPARE(page.m_terminalView->document()->blockCount(), 1000);
        QVERIFY(page.m_terminalView->toPlainText().contains(QStringLiteral("payload-200")));
        QVERIFY(page.m_terminalView->toPlainText().contains(QStringLiteral("payload-1199")));
        QVERIFY(!page.m_terminalView->toPlainText().contains(QStringLiteral("payload-199")));
    }

    void trimsFrameBreaksAndKeepsStableSelection()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        for (int i = 0; i < 1000; ++i) {
            WorkbenchPage::SessionRecord record;
            record.timestamp = QDateTime::fromMSecsSinceEpoch(1000 + i);
            record.direction =
                (i % 2 == 0) ? WorkbenchPage::RecordDirection::FrameBreak : WorkbenchPage::RecordDirection::Rx;
            record.bytes = QByteArray::number(i);
            record.displayText = record.terminalText = QString::number(i);
            page.m_records.append(record);
        }
        page.showDataTableWindow();
        auto *window = page.m_dataTableWindow;
        QCOMPARE(window->m_model->rowCount(), 500);
        window->m_table->sortByColumn(DataTableModel::LengthColumn, Qt::DescendingOrder);
        const QModelIndex selected =
            window->m_proxy->mapFromSource(window->m_model->index(window->m_model->rowForRecordIndex(999), 0));
        window->m_table->selectRow(selected.row());
        QCOMPARE(window->selectedRecordIndex(), 999);
        const QModelIndex expired =
            window->m_proxy->mapFromSource(window->m_model->index(window->m_model->rowForRecordIndex(1), 0));
        window->m_table->selectRow(expired.row());
        QSignalSpy resets(window->m_model, &QAbstractItemModel::modelReset);
        for (int i = 0; i < 5; ++i) {
            page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("tail"));
        }
        // The view can be one timer tick behind: a stale ID must not locate another frame.
        window->locateSelectedFrame();
        QVERIFY(page.m_terminalView->document()->isEmpty());
        window->m_table->selectRow(selected.row());
        page.flushDataTableWindow();
        QCOMPARE(page.m_firstRecordIndex, 5);
        QCOMPARE(window->m_model->rowCount(), 503);
        QCOMPARE(window->m_model->index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 5);
        QCOMPARE(window->selectedRecordIndex(), 999);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(window->selectedHexText(), QStringLiteral("39 39 39"));
        QVERIFY(window->selectedFrameText().contains(QStringLiteral("999")));
        window->locateSelectedFrame();
        QCOMPARE(page.m_terminalView->textCursor().selectedText().contains(QStringLiteral("999")), true);
    }

    void refreshesChecksumAndClearsCopiedSession()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray::fromHex("01020304"));
        page.showDataTableWindow();
        auto *window = page.m_dataTableWindow;
        const int checksumColumn = DataTableModel::ChecksumColumn;
        const QString before = window->m_model->index(0, checksumColumn).data().toString();
        const int current = page.m_checksumAlgorithmCombo->currentIndex();
        page.m_checksumAlgorithmCombo->setCurrentIndex((current + 1) % page.m_checksumAlgorithmCombo->count());
        const QString after = window->m_model->index(0, checksumColumn).data().toString();
        QVERIFY(after != before);
        QCOMPARE(after, page.dataTableRecord(0, page.m_records.first()).checksum);

        const int byteOrder = page.m_checksumByteOrderCombo->currentIndex();
        page.m_checksumByteOrderCombo->setCurrentIndex((byteOrder + 1) % page.m_checksumByteOrderCombo->count());
        QCOMPARE(window->m_model->index(0, checksumColumn).data().toString(),
                 page.dataTableRecord(0, page.m_records.first()).checksum);
        page.m_checksumByteOrderCombo->setCurrentIndex(byteOrder);
        window->close();
        page.m_checksumAlgorithmCombo->setCurrentIndex(current);
        QCOMPARE(window->m_model->index(0, checksumColumn).data().toString(), after);
        page.showDataTableWindow();
        QCOMPARE(window->m_model->index(0, checksumColumn).data().toString(), before);

        WorkbenchPage source(nullptr, false, false);
        page.copySessionConfigFrom(source);
        QCOMPARE(window->m_model->rowCount(), 0);
        QCOMPARE(page.m_firstRecordIndex, 0);
        QCOMPARE(page.m_dataTableNextRecordIndex, 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArray("new"));
        page.flushDataTableWindow();
        QCOMPARE(window->m_model->rowCount(), 1);
        QCOMPARE(window->m_model->index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 0);
    }

    void filtersCopiesAndLocatesAfterSorting()
    {
        WorkbenchPage page(nullptr, false, false);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("short"));
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArray("longer payload"));
        page.showDataTableWindow();
        auto *window = page.m_dataTableWindow;
        window->m_table->sortByColumn(DataTableModel::LengthColumn, Qt::DescendingOrder);
        window->m_table->selectRow(0);
        QCOMPARE(window->selectedRecordIndex(), 1);
        QSignalSpy locate(window, &DataTableWindow::locateRequested);
        window->m_locateButton->click();
        QCOMPARE(locate.size(), 1);
        QCOMPARE(locate.first().first().toLongLong(), 1);
        window->m_filterEdit->setText(QStringLiteral("  SHORT  "));
        QTRY_COMPARE(window->m_proxy->rowCount(), 1);
        window->m_table->selectRow(0);
        QCOMPARE(window->selectedRecordIndex(), 0);
        QCOMPARE(window->selectedHexText(), QStringLiteral("73 68 6F 72 74"));
        window->m_directionCombo->setCurrentIndex(window->m_directionCombo->findData(QStringLiteral("tx")));
        QCOMPARE(window->m_proxy->rowCount(), 0);
        QVERIFY(!window->m_copyButton->isEnabled());
        QVERIFY(!window->m_copyHexButton->isEnabled());
        QVERIFY(!window->m_locateButton->isEnabled());
    }

    void largeHistoryUsesIncrementalUpdates()
    {
        AppSettings settings;
        settings.setValue(QStringLiteral("terminal/maxRecords"), 50000);
        settings.sync();
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        WorkbenchPage::SessionRecord record;
        record.direction = WorkbenchPage::RecordDirection::Rx;
        record.bytes = QByteArray::fromHex("0104060001000200030000");
        record.displayText = record.terminalText = QStringLiteral("large history");
        for (int i = 0; i < 50000; ++i) {
            record.timestamp = QDateTime::fromMSecsSinceEpoch(1000 + i);
            page.m_records.append(record);
        }
        QElapsedTimer timer;
        timer.start();
        page.showDataTableWindow();
        qInfo() << "50,000-record table initial load (ms):" << timer.elapsed();
        auto *window = page.m_dataTableWindow;
        QSignalSpy resets(window->m_model, &QAbstractItemModel::modelReset);
        QSignalSpy inserts(window->m_model, &QAbstractItemModel::rowsInserted);
        timer.restart();
        for (int batch = 0; batch < 100; ++batch) {
            for (int i = 0; i < 100; ++i) {
                page.appendRecord(WorkbenchPage::RecordDirection::Rx, record.bytes);
            }
            page.flushDataTableWindow();
            QCoreApplication::processEvents();
        }
        qInfo() << "10,000 live records in 100 batches at 50,000-row cap (ms):" << timer.elapsed();
        QCOMPARE(window->m_model->rowCount(), 50000);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(inserts.size(), 100);
        QCOMPARE(window->m_model->index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 10000);
        QCOMPARE(window->m_model->index(49999, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 59999);
    }
};

QTEST_MAIN(WorkbenchDataTableTest)
#include "tst_workbench_data_table.moc"
