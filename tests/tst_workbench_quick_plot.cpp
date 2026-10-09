#include "app/core/app_settings.h"
#include "app/view/quick_plot_window.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

class WorkbenchQuickPlotTest : public QObject
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

    void firstOpenAndToolbarReopenSkipReceivedHistory()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        for (int index = 0; index < 1200; ++index) {
            page.handleReceivedData(QByteArray::number(index));
        }
        QCOMPARE(page.m_records.size(), 1000);
        QVERIFY(page.m_rxCount > 0);
        QVERIFY(!page.m_quickPlotWindow);

        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        QVERIFY(window->isPlottingActive());
        QCOMPARE(window->m_rows.size(), 0);
        QCOMPARE(window->m_plot->sampleCount(), 0);
        page.handleReceivedData(QByteArray("1"));
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArray("99"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 1)}));

        QSignalSpy samples(window->m_plot, &FluentQt::RealtimePlotWidget::samplesChanged);
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        window->close();
        QVERIFY(!window->isPlottingActive());
        page.handleReceivedData(QByteArray("2"));
        window->appendRecord(QDateTime::currentDateTime(), QStringLiteral("100"), QByteArray("100"), {});
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        page.handleReceivedData(QByteArray("3"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 1), QPointF(1, 3)}));

        window->hide();
        samples.clear();
        for (int index = 0; index < 1200; ++index) {
            page.handleReceivedData(QByteArray::number(100 + index));
        }
        QCOMPARE(page.m_records.size(), 1000);
        QCOMPARE(window->m_rows.size(), 2);
        QCOMPARE(samples.size(), 0);
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 2);
        QCOMPARE(samples.size(), 0);
        page.handleReceivedData(QByteArray("4"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 1), QPointF(1, 3), QPointF(2, 4)}));
    }

    void keepsParserAndPauseSettingsWhenReopened()
    {
        WorkbenchPage page(nullptr, false, false);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=19 pressure=6"));
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        AppPlot::ParserConfig parser;
        parser.protocol = AppPlot::Protocol::KeyValue;
        parser.fields = {QStringLiteral("temperature")};
        QVERIFY(window->configureParser(parser));
        QCOMPARE(window->m_rows.size(), 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=20 pressure=7"));
        QCOMPARE(window->m_channelNames, QVector<QString>({QStringLiteral("temperature")}));
        QCOMPARE(window->m_rows.size(), 1);
        window->setPaused(true);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=21 pressure=8"));
        QCOMPARE(window->m_rows.size(), 1);
        window->close();
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=22 pressure=9"));
        QCOMPARE(window->m_rows.size(), 1);

        page.showQuickPlotWindow();
        QVERIFY(window->m_paused);
        QCOMPARE(window->m_parserConfig.protocol, AppPlot::Protocol::KeyValue);
        QCOMPARE(window->m_parserConfig.fields, parser.fields);
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 20)}));
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=23 pressure=10"));
        QCOMPARE(window->m_rows.size(), 1);
        window->setPaused(false);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=24 pressure=11"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 20), QPointF(1, 24)}));
    }

    void reopeningPreservesClearedDataAndView()
    {
        WorkbenchPage page(nullptr, false, false);
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("1"));
        window->clearData();
        window->m_plot->setAutoScroll(false);
        window->m_plot->setAutoYRange(false);
        window->close();
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("2"));
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 0);
        QCOMPARE(window->m_plot->sampleCount(), 0);
        QVERIFY(!window->m_plot->autoScroll());
        QVERIFY(!window->m_plot->autoYRange());
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("3"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 3)}));

        window->close();
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 1);
    }

    void parserChangesStartFreshWithFutureSamples()
    {
        WorkbenchPage page(nullptr, false, false);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=19 humidity=49"));
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=20 humidity=50"));
        QCOMPARE(window->m_rows.size(), 1);
        AppPlot::ParserConfig parser;
        parser.protocol = AppPlot::Protocol::KeyValue;
        parser.fields = {QStringLiteral("temperature")};
        QVERIFY(window->configureParser(parser));
        QCOMPARE(window->m_rows.size(), 0);
        QCOMPARE(window->m_plot->sampleCount(), 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=21 humidity=51"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 21)}));
        QVERIFY(window->configureParser(parser));
        QCOMPARE(window->m_rows.size(), 1);

        window->close();
        parser.fields = {QStringLiteral("humidity")};
        QVERIFY(window->configureParser(parser));
        QCOMPARE(window->m_rows.size(), 0);
        QSignalSpy samples(window->m_plot, &FluentQt::RealtimePlotWidget::samplesChanged);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=22 humidity=52"));
        QCOMPARE(window->m_rows.size(), 0);
        QCOMPARE(samples.size(), 0);
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 0);
        QCOMPARE(samples.size(), 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("temperature=23 humidity=53"));
        QCOMPARE(window->m_channelNames, QVector<QString>({QStringLiteral("humidity")}));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 53)}));
    }

    void copiedSessionClearsPlotAndSkipsHiddenRecords()
    {
        WorkbenchPage page(nullptr, false, false);
        WorkbenchPage source(nullptr, false, false);
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("1"));
        QCOMPARE(window->m_rows.size(), 1);
        window->close();
        page.copySessionConfigFrom(source);
        QCOMPARE(window->m_rows.size(), 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("2"));
        page.showQuickPlotWindow();
        QCOMPARE(window->m_rows.size(), 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("3"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 3)}));
    }

    void actualReceiveSkipsClosedAndHiddenDataOnNativeShow()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        QVERIFY(window->isPlottingActive());
        page.handleReceivedData(QByteArray("1"));
        QSignalSpy samples(window->m_plot, &FluentQt::RealtimePlotWidget::samplesChanged);

        window->close();
        QVERIFY(!window->isPlottingActive());
        QVERIFY(!window->m_plot->updatesEnabled());
        page.handleReceivedData(QByteArray("2"));
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        QCOMPARE(page.m_rxCount, qint64(2));
        window->show();
        QVERIFY(window->isPlottingActive());
        QVERIFY(window->m_plot->updatesEnabled());
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        page.handleReceivedData(QByteArray("3"));
        QCOMPARE(window->m_rows.size(), 2);

        window->hide();
        samples.clear();
        page.handleReceivedData(QByteArray("4"));
        QCOMPARE(window->m_rows.size(), 2);
        QCOMPARE(samples.size(), 0);
        window->show();
        QCOMPARE(window->m_rows.size(), 2);
        QCOMPARE(samples.size(), 0);
        page.handleReceivedData(QByteArray("5"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 1), QPointF(1, 3), QPointF(2, 5)}));
        QCOMPARE(page.m_rxCount, qint64(5));
    }

    void actualReceiveSkipsMinimizedDataOnNativeAndToolbarRestore()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        page.handleReceivedData(QByteArray("1"));
        QSignalSpy samples(window->m_plot, &FluentQt::RealtimePlotWidget::samplesChanged);

        window->showMinimized();
        QVERIFY(window->isMinimized());
        // Qt keeps isVisible() true for minimized windows.
        QVERIFY(window->isVisible());
        QVERIFY(!window->isPlottingActive());
        QVERIFY(!window->m_plot->updatesEnabled());
        page.handleReceivedData(QByteArray("2"));
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        window->showNormal();
        QVERIFY(window->isPlottingActive());
        QVERIFY(window->m_plot->updatesEnabled());
        QCOMPARE(window->m_rows.size(), 1);
        QCOMPARE(samples.size(), 0);
        page.handleReceivedData(QByteArray("3"));
        QCOMPARE(window->m_rows.size(), 2);
        samples.clear();
        page.showQuickPlotWindow();
        QCOMPARE(samples.size(), 0);

        window->showMinimized();
        page.handleReceivedData(QByteArray("4"));
        page.showQuickPlotWindow();
        QVERIFY(!window->isMinimized());
        QVERIFY(window->isPlottingActive());
        QCOMPARE(window->m_rows.size(), 2);
        QCOMPARE(samples.size(), 0);
        page.handleReceivedData(QByteArray("5"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 1), QPointF(1, 3), QPointF(2, 5)}));
    }

    void cancelsPendingRefreshWhenHiddenOrMinimized()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        page.showQuickPlotWindow();
        auto *window = page.m_quickPlotWindow;
        auto *refreshTimer = window->m_plot->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(refreshTimer);
        QVERIFY(refreshTimer->isSingleShot());
        window->m_plot->setRefreshRate(47);
        window->m_plot->setVisibleSpan(10000);
        for (int index = 0; index < 5000; ++index) {
            page.handleReceivedData(QByteArray::number(index));
        }
        QVERIFY(refreshTimer->isActive());

        window->close();
        QVERIFY(!refreshTimer->isActive());
        QVERIFY(!window->m_plot->updatesEnabled());
        page.handleReceivedData(QByteArray("5000"));
        QVERIFY(!refreshTimer->isActive());
        QCOMPARE(window->m_rows.size(), 5000);
        window->show();
        QCOMPARE(window->m_rows.size(), 5000);
        QCOMPARE(window->m_plot->refreshRate(), 47);
        QVERIFY(!refreshTimer->isActive());

        window->showMinimized();
        QVERIFY(!refreshTimer->isActive());
        page.handleReceivedData(QByteArray("5001"));
        QVERIFY(!refreshTimer->isActive());
        QCOMPARE(window->m_rows.size(), 5000);
        window->showNormal();
        QCOMPARE(window->m_rows.size(), 5000);
        QCOMPARE(window->m_plot->refreshRate(), 47);
        page.handleReceivedData(QByteArray("5002"));
        QCOMPARE(window->m_rows.size(), 5001);
        QCOMPARE(window->m_rows.last().values.first(), 5002.0);
    }
};

QTEST_MAIN(WorkbenchQuickPlotTest)
#include "tst_workbench_quick_plot.moc"
