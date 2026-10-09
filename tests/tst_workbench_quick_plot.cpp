#include "app/core/app_settings.h"
#include "app/view/plot_parser_dialog.h"
#include "app/view/quick_plot_window.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>

class WorkbenchQuickPlotTest : public QObject
{
    Q_OBJECT

    struct DialogResult
    {
        bool seen = false;
        bool requiresChoice = false;
        bool chosen = false;
        bool accepted = false;
    };

    QuickPlotWindow *openFromToolbar(WorkbenchPage &page, AppPlot::Protocol protocol, const QString &fields,
                                    bool cancel, DialogResult *result)
    {
        QTimer::singleShot(0, &page, [protocol, fields, cancel, result]() {
            auto *dialog = qobject_cast<PlotParserDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            result->seen = true;
            auto *combo = dialog->findChild<FluentQt::ComboBox *>(QStringLiteral("plotProtocolCombo"));
            auto *apply = dialog->findChild<FluentQt::PushButton *>(QStringLiteral("plotApplyButton"));
            auto *fieldEdit = dialog->findChild<FluentQt::LineEdit *>(QStringLiteral("plotFieldsEdit"));
            result->requiresChoice = combo && combo->currentData().toString().isEmpty() && apply &&
                                     !apply->isEnabled();
            if (cancel || !combo || !apply || !fieldEdit) {
                dialog->reject();
                return;
            }
            combo->setCurrentIndex(combo->findData(AppPlot::protocolKey(protocol)));
            fieldEdit->setText(fields);
            result->chosen = combo->currentData().toString() == AppPlot::protocolKey(protocol);
            apply->click();
            result->accepted = dialog->result() == QDialog::Accepted;
            // Fail an invalid configuration without leaving the modal event loop blocked.
            if (!result->accepted) {
                dialog->reject();
            }
        });
        page.showQuickPlotWindow();
        return page.m_quickPlotWindow;
    }

    static AppPlot::ParserConfig keyValueParser(const QString &field)
    {
        AppPlot::ParserConfig parser;
        parser.protocol = AppPlot::Protocol::KeyValue;
        parser.fields = {field};
        return parser;
    }

    static QByteArray checkedFrame()
    {
        QByteArray frame = QByteArray::fromHex("AA550310010203");
        frame += AppChecksum::calculate(frame, QStringLiteral("crc16-modbus"),
                                        AppChecksum::ByteOrder::LittleEndian).bytes;
        return frame;
    }

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

    void canceledCreationDoesNotPlotOrReplayReceivedHistory()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        for (int index = 0; index < 1200; ++index) {
            page.handleReceivedData(QByteArray::number(index));
        }
        QCOMPARE(page.m_records.size(), 1000);
        DialogResult canceled;
        QVERIFY(!openFromToolbar(page, AppPlot::Protocol::Numbers, {}, true, &canceled));
        QVERIFY(canceled.seen);
        QVERIFY(canceled.requiresChoice);
        QVERIFY(page.m_quickPlotWindows.isEmpty());
        page.handleReceivedData(QByteArray("9"));

        DialogResult selected;
        auto *window = openFromToolbar(page, AppPlot::Protocol::Numbers, {}, false, &selected);
        QVERIFY(selected.seen && selected.requiresChoice && selected.chosen && selected.accepted);
        QVERIFY(window);
        QVERIFY(window->isPlottingActive());
        QCOMPARE(window->m_rows.size(), 0);
        QCOMPARE(window->m_plot->sampleCount(), 0);
        page.handleReceivedData(QByteArray("1"));
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArray("99"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 1)}));
    }

    void everyToolbarClickRequiresChoiceAndCreatesANewWindow()
    {
        WorkbenchPage page(nullptr, false, false);
        DialogResult firstChoice;
        auto *first = openFromToolbar(page, AppPlot::Protocol::KeyValue, QStringLiteral("temperature"), false,
                                      &firstChoice);
        QVERIFY(firstChoice.seen && firstChoice.requiresChoice && firstChoice.accepted);
        QVERIFY(first);
        page.handleReceivedData(QByteArray("temperature=20 pressure=7"));
        QCOMPARE(first->m_rows.size(), 1);

        DialogResult secondChoice;
        auto *second = openFromToolbar(page, AppPlot::Protocol::KeyValue, QStringLiteral("pressure"), false,
                                       &secondChoice);
        QVERIFY(secondChoice.seen && secondChoice.requiresChoice && secondChoice.accepted);
        QVERIFY(second);
        QVERIFY(second != first);
        QVERIFY(second->windowTitle() != first->windowTitle());
        QCOMPARE(page.m_quickPlotWindows.size(), 2);
        QCOMPARE(second->m_rows.size(), 0);
        page.handleReceivedData(QByteArray("temperature=21 pressure=8"));
        QCOMPARE(first->m_plot->points(), QVector<QPointF>({QPointF(0, 20), QPointF(1, 21)}));
        QCOMPARE(second->m_plot->points(), QVector<QPointF>({QPointF(0, 8)}));
        QCOMPARE(first->m_channelNames, QVector<QString>({QStringLiteral("temperature")}));
        QCOMPARE(second->m_channelNames, QVector<QString>({QStringLiteral("pressure")}));

        first->showMinimized();
        DialogResult thirdChoice;
        auto *third = openFromToolbar(page, AppPlot::Protocol::Numbers, {}, false, &thirdChoice);
        QVERIFY(thirdChoice.requiresChoice && thirdChoice.accepted);
        QVERIFY(third && third != first && third != second);
        QVERIFY(first->isMinimized());
        QCOMPARE(page.m_quickPlotWindows.size(), 3);
        DialogResult canceled;
        QCOMPARE(openFromToolbar(page, AppPlot::Protocol::Json, {}, true, &canceled), third);
        QVERIFY(canceled.requiresChoice);
        QCOMPARE(page.m_quickPlotWindows.size(), 3);
    }

    void independentWindowsSupportAllParserTypes_data()
    {
        QTest::addColumn<int>("protocol");
        QTest::addColumn<QByteArray>("frame");
        QTest::addColumn<double>("firstValue");
        QTest::newRow("numbers") << int(AppPlot::Protocol::Numbers) << QByteArray("20 7") << 20.0;
        QTest::newRow("delimited") << int(AppPlot::Protocol::Delimited) << QByteArray("20,7") << 20.0;
        QTest::newRow("key-value") << int(AppPlot::Protocol::KeyValue) << QByteArray("temperature=20") << 20.0;
        QTest::newRow("json") << int(AppPlot::Protocol::Json) << QByteArray("{\"temperature\":20}") << 20.0;
        QTest::newRow("binary") << int(AppPlot::Protocol::Binary) << QByteArray::fromHex("1407") << 20.0;
    }

    void independentWindowsSupportAllParserTypes()
    {
        QFETCH(int, protocol);
        QFETCH(QByteArray, frame);
        QFETCH(double, firstValue);
        WorkbenchPage page(nullptr, false, false);
        AppPlot::ParserConfig parser;
        parser.protocol = static_cast<AppPlot::Protocol>(protocol);
        auto *first = page.createQuickPlotWindow(parser);
        auto *second = page.createQuickPlotWindow(parser);
        QVERIFY(first && second && first != second);
        page.handleReceivedData(frame);
        QCOMPARE(first->m_rows.size(), 1);
        QCOMPARE(second->m_rows.size(), 1);
        QCOMPARE(first->m_rows.first().values.first(), firstValue);
        QCOMPARE(second->m_rows.first().values.first(), firstValue);
        first->clearData();
        QCOMPARE(first->m_rows.size(), 0);
        QCOMPARE(second->m_rows.size(), 1);
    }

    void hidingMinimizingAndClosingAWindowDoesNotStopOtherWindows()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        QPointer<QuickPlotWindow> first = page.createQuickPlotWindow({});
        auto *second = page.createQuickPlotWindow({});
        QVERIFY(first && second);
        page.handleReceivedData(QByteArray("1"));
        first->hide();
        QVERIFY(!first->isPlottingActive());
        QVERIFY(!first->m_plot->updatesEnabled());
        page.handleReceivedData(QByteArray("2"));
        QCOMPARE(first->m_rows.size(), 1);
        QCOMPARE(second->m_rows.size(), 2);
        first->show();
        QCOMPARE(first->m_rows.size(), 1);
        page.handleReceivedData(QByteArray("3"));
        first->showMinimized();
        QVERIFY(first->isMinimized());
        QVERIFY(first->isVisible());
        QVERIFY(!first->isPlottingActive());
        page.handleReceivedData(QByteArray("4"));
        QCOMPARE(first->m_rows.size(), 2);
        first->showNormal();
        QCOMPARE(first->m_rows.size(), 2);
        page.handleReceivedData(QByteArray("5"));
        QCOMPARE(first->m_plot->points(), QVector<QPointF>({QPointF(0, 1), QPointF(1, 3), QPointF(2, 5)}));
        QCOMPARE(second->m_rows.size(), 5);

        QVERIFY(first->testAttribute(Qt::WA_DeleteOnClose));
        first->close();
        QVERIFY(!first->isPlottingActive());
        page.handleReceivedData(QByteArray("6"));
        QCOMPARE(first->m_rows.size(), 3);
        QCOMPARE(second->m_rows.size(), 6);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!first);
        QCOMPARE(page.m_quickPlotWindows, QList<QuickPlotWindow *>({second}));
        QCOMPARE(page.m_quickPlotWindow, second);
        QPointer<QuickPlotWindow> last = second;
        second->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!last);
        QVERIFY(page.m_quickPlotWindows.isEmpty());
        QVERIFY(!page.m_quickPlotWindow);
        page.handleReceivedData(QByteArray("7"));
        QCOMPARE(page.m_rxCount, qint64(7));
    }

    void parserChangesAndPauseOnlyAffectTheSelectedWindow()
    {
        WorkbenchPage page(nullptr, false, false);
        auto *first = page.createQuickPlotWindow(keyValueParser(QStringLiteral("temperature")));
        auto *second = page.createQuickPlotWindow(keyValueParser(QStringLiteral("pressure")));
        page.handleReceivedData(QByteArray("temperature=20 pressure=7"));
        first->setPaused(true);
        page.handleReceivedData(QByteArray("temperature=21 pressure=8"));
        QCOMPARE(first->m_rows.size(), 1);
        QCOMPARE(second->m_rows.size(), 2);
        first->hide();
        first->show();
        QVERIFY(first->m_paused);
        first->setPaused(false);
        QVERIFY(first->configureParser(keyValueParser(QStringLiteral("pressure"))));
        QCOMPARE(first->m_rows.size(), 0);
        QCOMPARE(second->m_rows.size(), 2);
        page.handleReceivedData(QByteArray("temperature=22 pressure=9"));
        QCOMPARE(first->m_plot->points(), QVector<QPointF>({QPointF(0, 9)}));
        QCOMPARE(second->m_plot->points(), QVector<QPointF>({QPointF(0, 7), QPointF(1, 8), QPointF(2, 9)}));
        QVERIFY(first->configureParser(keyValueParser(QStringLiteral("pressure"))));
        QCOMPARE(first->m_rows.size(), 1);
        first->clearData();
        first->m_plot->setAutoScroll(false);
        first->m_plot->setAutoYRange(false);
        first->hide();
        page.handleReceivedData(QByteArray("temperature=23 pressure=10"));
        first->show();
        QCOMPARE(first->m_rows.size(), 0);
        QVERIFY(!first->m_plot->autoScroll());
        QVERIFY(!first->m_plot->autoYRange());
        page.handleReceivedData(QByteArray("temperature=24 pressure=11"));
        QCOMPARE(first->m_plot->points(), QVector<QPointF>({QPointF(0, 11)}));
    }

    void binaryPayloadWindowsKeepTheirOwnTemplateSnapshots()
    {
        WorkbenchPage page(nullptr, false, false);
        AppPlot::ParserConfig parser;
        parser.protocol = AppPlot::Protocol::Binary;
        parser.binarySource = AppPlot::BinarySource::Payload;
        AppPlot::BinaryField field;
        field.name = QStringLiteral("value");
        parser.binaryFields = {field};
        auto firstTemplate = AppProtocol::defaultTemplate();
        auto secondTemplate = firstTemplate;
        secondTemplate.name = QStringLiteral("second payload");
        secondTemplate.lengthSize = 0;
        secondTemplate.payloadOffset = 5;
        secondTemplate.payloadLength = 2;
        auto *first = page.createQuickPlotWindow(parser, &firstTemplate);
        auto *second = page.createQuickPlotWindow(parser, &secondTemplate);
        QVERIFY(first && second);
        const QByteArray frame = checkedFrame();
        QVERIFY(AppProtocol::parseFrame(frame, firstTemplate).checksumValid);
        QVERIFY(AppProtocol::parseFrame(frame, secondTemplate).checksumValid);
        page.handleReceivedData(frame);
        QCOMPARE(first->m_plot->points(), QVector<QPointF>({QPointF(0, 1)}));
        QCOMPARE(second->m_plot->points(), QVector<QPointF>({QPointF(0, 2)}));

        // Editing or disabling the workbench template must not alter existing windows.
        auto editedTemplate = firstTemplate;
        editedTemplate.header = QByteArray::fromHex("FFFF");
        page.m_protocolTemplates = {editedTemplate};
        page.updateProtocolTemplateCombo(0);
        page.m_protocolEnabledCheck->setChecked(false);
        firstTemplate.payloadOffset = 0;
        secondTemplate.payloadOffset = 0;
        page.handleReceivedData(frame);
        QCOMPARE(first->m_rows.size(), 2);
        QCOMPARE(second->m_rows.size(), 2);
        QCOMPARE(first->m_rows.last().values.first(), 1.0);
        QCOMPARE(second->m_rows.last().values.first(), 2.0);

        QByteArray corrupt = frame;
        corrupt[corrupt.size() - 1] = char(corrupt.at(corrupt.size() - 1) ^ 0x01);
        page.handleReceivedData(corrupt);
        page.handleReceivedData(QByteArray::fromHex("FFFF0310010203"));
        QCOMPARE(first->m_rows.size(), 2);
        QCOMPARE(second->m_rows.size(), 2);
        QCOMPARE(page.m_records.size(), 4);
        first->hide();
        page.handleReceivedData(frame);
        QCOMPARE(first->m_rows.size(), 2);
        QCOMPARE(second->m_rows.size(), 3);
    }

    void copiedSessionClearsAllPlotsAndSkipsHiddenRecords()
    {
        WorkbenchPage page(nullptr, false, false);
        WorkbenchPage source(nullptr, false, false);
        auto *first = page.createQuickPlotWindow({});
        auto *second = page.createQuickPlotWindow({});
        page.handleReceivedData(QByteArray("1"));
        first->hide();
        page.copySessionConfigFrom(source);
        QCOMPARE(first->m_rows.size(), 0);
        QCOMPARE(second->m_rows.size(), 0);
        page.handleReceivedData(QByteArray("2"));
        first->show();
        QCOMPARE(first->m_rows.size(), 0);
        QCOMPARE(second->m_rows.size(), 1);
        page.handleReceivedData(QByteArray("3"));
        QCOMPARE(first->m_plot->points(), QVector<QPointF>({QPointF(0, 3)}));
        QCOMPARE(second->m_plot->points(), QVector<QPointF>({QPointF(0, 2), QPointF(1, 3)}));
    }

    void destroyingTheWorkbenchDestroysAllActivePlotWindows()
    {
        auto *page = new WorkbenchPage(nullptr, false, false);
        QPointer<QuickPlotWindow> first = page->createQuickPlotWindow({});
        QPointer<QuickPlotWindow> second = page->createQuickPlotWindow({});
        QVERIFY(first && second);
        QVERIFY(first->isPlottingActive() && second->isPlottingActive());
        page->handleReceivedData(QByteArray("1"));
        QCOMPARE(first->m_rows.size(), 1);
        QCOMPARE(second->m_rows.size(), 1);
        delete page;
        QVERIFY(!first);
        QVERIFY(!second);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void controlShowPlotUsesExplicitConfigWithoutPromptAndKeepsExistingSamples()
    {
        WorkbenchPage page(nullptr, false, false);
        bool unexpectedDialog = false;
        QTimer modalWatchdog;
        connect(&modalWatchdog, &QTimer::timeout, this, [&unexpectedDialog]() {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                unexpectedDialog = true;
                dialog->reject();
            }
        });
        modalWatchdog.start(1);
        const auto parser = keyValueParser(QStringLiteral("temperature"));
        QString error;
        QVERIFY2(page.controlShowPlot(parser, false, &error), qPrintable(error));
        modalWatchdog.stop();
        QVERIFY(!unexpectedDialog);
        auto *window = page.m_quickPlotWindow;
        QVERIFY(window);
        QCOMPARE(page.m_quickPlotWindows.size(), 1);
        QCOMPARE(window->m_parserConfig.protocol, parser.protocol);
        QCOMPARE(window->m_parserConfig.fields, parser.fields);
        page.handleReceivedData(QByteArray("temperature=20 pressure=7"));
        modalWatchdog.start(1);
        QVERIFY2(page.controlShowPlot(parser, false, &error), qPrintable(error));
        modalWatchdog.stop();
        QVERIFY(!unexpectedDialog);
        QCOMPARE(page.m_quickPlotWindow, window);
        QCOMPARE(page.m_quickPlotWindows.size(), 1);
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 20)}));
        QVERIFY2(page.controlShowPlot(parser, true, &error), qPrintable(error));
        QCOMPARE(page.m_quickPlotWindow, window);
        QCOMPARE(page.m_quickPlotWindows.size(), 1);
        QCOMPARE(window->m_rows.size(), 0);
        page.handleReceivedData(QByteArray("temperature=21 pressure=8"));
        QCOMPARE(window->m_plot->points(), QVector<QPointF>({QPointF(0, 21)}));
    }

    void cancelsPendingRefreshWhenHiddenMinimizedOrClosed()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_pauseCheck->setChecked(true);
        QPointer<QuickPlotWindow> window = page.createQuickPlotWindow({});
        auto *refreshTimer = window->m_plot->findChild<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(refreshTimer);
        QVERIFY(refreshTimer->isSingleShot());
        window->m_plot->setRefreshRate(47);
        // The widget batches repaint work only above its visible-sample threshold.
        // Keep enough points visible to exercise an actual pending refresh at any window width.
        const int initialSamples = qMax(5000, window->m_plot->width() * 4 + 10);
        window->m_plot->setVisibleSpan(initialSamples + 20);
        for (int index = 0; index < initialSamples; ++index) {
            page.handleReceivedData(QByteArray::number(index));
        }
        QVERIFY(refreshTimer->isActive());
        window->hide();
        QVERIFY(!refreshTimer->isActive());
        QVERIFY(!window->m_plot->updatesEnabled());
        page.handleReceivedData(QByteArray::number(initialSamples));
        QCOMPARE(window->m_rows.size(), initialSamples);
        window->show();
        QCOMPARE(window->m_rows.size(), initialSamples);
        QCOMPARE(window->m_plot->refreshRate(), 47);
        QVERIFY(!refreshTimer->isActive());
        page.handleReceivedData(QByteArray::number(initialSamples + 1));
        QVERIFY(refreshTimer->isActive());
        window->showMinimized();
        QVERIFY(!refreshTimer->isActive());
        page.handleReceivedData(QByteArray::number(initialSamples + 2));
        QCOMPARE(window->m_rows.size(), initialSamples + 1);
        window->showNormal();
        QCOMPARE(window->m_rows.size(), initialSamples + 1);
        page.handleReceivedData(QByteArray::number(initialSamples + 3));
        QVERIFY(refreshTimer->isActive());
        window->close();
        QVERIFY(!refreshTimer->isActive());
        QVERIFY(!window->isPlottingActive());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!window);
        QVERIFY(page.m_quickPlotWindows.isEmpty());
    }
};

QTEST_MAIN(WorkbenchQuickPlotTest)
#include "tst_workbench_quick_plot.moc"
