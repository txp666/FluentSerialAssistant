#include "app/serial/virtual_serial_pair.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtTest/QTest>

using namespace WorkbenchPagePrivate;

class WorkbenchReliabilityTest : public QObject
{
    Q_OBJECT

  private:
    static void connectVirtualPort(WorkbenchPage &page, const QString &port = VirtualSerialPair::portAName())
    {
        VirtualSerialPair::instance()->setEnabled(true);
        page.refreshPorts();
        page.m_portCombo->setCurrentIndex(page.m_portCombo->findData(port));
        page.m_connectButton->click();
        QVERIFY(page.m_serial.isOpen());
    }

    static void savePacket(WorkbenchPage &page, const QString &name)
    {
        page.m_packetNameEdit->setText(name);
        page.m_packetPayloadEdit->setPlainText(name + QStringLiteral(" payload"));
        page.saveCurrentPacket();
    }

    static void saveMacroStep(WorkbenchPage &page, const QString &name)
    {
        page.m_macroList->setCurrentRow(-1);
        page.m_macroNameEdit->setText(name);
        page.m_macroPayloadEdit->setPlainText(name + QStringLiteral(" payload"));
        page.saveCurrentMacroStep();
    }

    static void saveAutoReplyRule(WorkbenchPage &page, const QString &name)
    {
        page.startNewAutoReplyRule();
        page.m_autoReplyNameEdit->setText(name);
        page.m_autoReplyPatternEdit->setText(name + QStringLiteral(" request"));
        page.m_autoReplyPayloadEdit->setPlainText(name + QStringLiteral(" response"));
        page.saveCurrentAutoReplyRule();
    }

    static QStringList packetNames(const WorkbenchPage &page)
    {
        QStringList names;
        for (const WorkbenchPage::SendPacket &packet : page.m_sendPackets) {
            names.append(packet.name);
        }
        return names;
    }

    static QStringList macroNames(const WorkbenchPage &page)
    {
        QStringList names;
        for (const WorkbenchPage::MacroStep &step : page.m_macroSteps) {
            names.append(step.name);
        }
        return names;
    }

    static QStringList autoReplyNames(const WorkbenchPage &page)
    {
        QStringList names;
        for (const WorkbenchPage::AutoReplyRule &rule : page.m_autoReplyRules) {
            names.append(rule.name);
        }
        return names;
    }

    static QString receivedText(const WorkbenchPage &page)
    {
        QString text;
        for (const WorkbenchPage::SessionRecord &record : page.m_records) {
            if (record.direction == WorkbenchPage::RecordDirection::Rx) {
                text += record.terminalText;
            }
        }
        return text;
    }

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
    }

    void init()
    {
        VirtualSerialPair::instance()->setEnabled(false);
        AppSettings settings;
        settings.clear();
        settings.sync();
    }

    void cleanup()
    {
        VirtualSerialPair::instance()->setEnabled(false);
        QCoreApplication::processEvents();
    }

    void longMacroSequenceRunsStepsFromTheEventLoop()
    {
        WorkbenchPage page(nullptr, false, false);
        connectVirtualPort(page);
        WorkbenchPage::MacroStep step;
        step.name = QStringLiteral("ping");
        step.mode = QStringLiteral("text");
        step.payload = QStringLiteral("ping");
        step.lineEnding = QStringLiteral("none");
        step.responseMode = QStringLiteral("text");
        page.m_macroSteps = {step, step};
        constexpr int Loops = 1000;
        setNumberEditValue(page.m_macroLoopCountEdit, Loops, 1, 100000);

        page.startMacroSequence();
        // Steps without a response or delay used to run recursively inside this
        // call, one stack frame chain per step.
        QVERIFY(page.m_macroRunning);
        QVERIFY(page.m_macroResults.size() < 2 * Loops);
        QTRY_VERIFY_WITH_TIMEOUT(!page.m_macroRunning, 60000);
        QCOMPARE(page.m_macroResults.size(), 2 * Loops);
        for (const WorkbenchPage::MacroRunResult &result : page.m_macroResults) {
            QCOMPARE(result.status, AppI18n::text("通过"));
        }
    }

    void closingSessionDoesNotWaitForBlockedScriptRequest()
    {
        auto *page = new WorkbenchPage(nullptr, false, false);
        page->m_scriptEdit->setPlainText(QStringLiteral("while (true) { serial.lastRxText(); serial.records(); }"));
        page->startScript();
        QVERIFY(page->m_scriptRunning);
        // Let the script make requests that wait for the page's thread.
        QTest::qWait(200);
        QVERIFY(page->m_scriptRunner);

        QElapsedTimer timer;
        timer.start();
        delete page;
        QVERIFY2(timer.elapsed() < 1500, qPrintable(QStringLiteral("Closing took %1 ms").arg(timer.elapsed())));
    }

    void stoppedScriptDoesNotSendQueuedRequest()
    {
        WorkbenchPage page(nullptr, false, false);
        connectVirtualPort(page);
        page.m_scriptEdit->setPlainText(QStringLiteral("while (true) { serial.sendText('x'); }"));
        page.startScript();
        QTRY_VERIFY(page.controlStatus().transmittedBytes > 0);
        // Without processing events, let the script queue its next send request.
        QThread::msleep(50);
        const qint64 sent = page.controlStatus().transmittedBytes;
        page.stopScript();
        QTRY_VERIFY(!page.m_scriptRunning && page.m_scriptRunner == nullptr);
        QCOMPARE(page.controlStatus().transmittedBytes, sent);
    }

    void sessionsKeepEachOthersSavedLists()
    {
        WorkbenchPage first(nullptr, true, false);
        WorkbenchPage second(nullptr, false, false);

        savePacket(first, QStringLiteral("first packet"));
        savePacket(second, QStringLiteral("second packet"));
        saveMacroStep(first, QStringLiteral("first step"));
        saveMacroStep(second, QStringLiteral("second step"));
        saveAutoReplyRule(first, QStringLiteral("first rule"));
        saveAutoReplyRule(second, QStringLiteral("second rule"));

        const QStringList packets = {QStringLiteral("first packet"), QStringLiteral("second packet")};
        const QStringList steps = {QStringLiteral("first step"), QStringLiteral("second step")};
        const QStringList rules = {QStringLiteral("first rule"), QStringLiteral("second rule")};
        QCOMPARE(packetNames(first), packets);
        QCOMPARE(macroNames(first), steps);
        QCOMPARE(autoReplyNames(first), rules);

        // Closing a session saves its settings. Neither session may drop the
        // other's items, and a blank session starts from the saved lists.
        first.saveSettings();
        second.saveSettings();
        WorkbenchPage blank(nullptr, false, false);
        blank.saveSettings();
        WorkbenchPage restored(nullptr, true, false);
        QCOMPARE(packetNames(restored), packets);
        QCOMPARE(macroNames(restored), steps);
        QCOMPARE(autoReplyNames(restored), rules);
    }

    void reloadedSelectionFollowsTheSameItem()
    {
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        saveMacroStep(first, QStringLiteral("removed"));
        saveMacroStep(first, QStringLiteral("kept"));
        second.m_macroList->setCurrentRow(1);
        QCOMPARE(second.m_macroNameEdit->text(), QStringLiteral("kept"));

        first.m_macroList->setCurrentRow(0);
        first.removeSelectedMacroStep();
        QCOMPARE(macroNames(second), QStringList{QStringLiteral("kept")});
        QCOMPARE(second.m_macroList->currentRow(), 0);

        // Saving the selected step updates it instead of overwriting another one.
        second.m_macroPayloadEdit->setPlainText(QStringLiteral("edited"));
        second.saveCurrentMacroStep();
        QCOMPARE(first.m_macroSteps.size(), 1);
        QCOMPARE(first.m_macroSteps.first().payload, QStringLiteral("edited"));

        // A selection whose item another session replaced is cleared, so the
        // next save adds a step.
        first.m_macroList->setCurrentRow(0);
        first.m_macroPayloadEdit->setPlainText(QStringLiteral("changed elsewhere"));
        first.saveCurrentMacroStep();
        QCOMPARE(second.m_macroList->currentRow(), -1);
    }

    void protocolTemplatesSyncWithoutChangingSelection()
    {
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        const QString selected = second.m_protocolNameEdit->text();

        first.m_protocolNameEdit->setText(QStringLiteral("Shared protocol"));
        first.m_protocolHeaderEdit->setText(QStringLiteral("AA BB"));
        first.saveCurrentProtocolTemplate();

        QCOMPARE(second.m_protocolTemplates.size(), first.m_protocolTemplates.size());
        QCOMPARE(second.m_protocolTemplates.last().name, QStringLiteral("Shared protocol"));
        QCOMPARE(second.m_protocolNameEdit->text(), selected);
        QCOMPARE(second.m_protocolTemplateCombo->currentIndex(), 0);

        second.saveSettings();
        WorkbenchPage restored(nullptr, true, false);
        QCOMPARE(restored.m_protocolTemplates.last().name, QStringLiteral("Shared protocol"));
    }

    void multibyteCharactersSplitAcrossReadsDecode_data()
    {
        QTest::addColumn<QString>("encoding");
        QTest::newRow("utf-8") << QStringLiteral("utf-8");
        QTest::newRow("gbk") << QStringLiteral("gbk");
    }

    void multibyteCharactersSplitAcrossReadsDecode()
    {
        QFETCH(QString, encoding);
        WorkbenchPage page(nullptr, false, false);
        selectEncodingOption(page.m_receiveEncodingCombo, encoding);
        const QString text = QStringLiteral("温度=25 湿度=60");
        const QByteArray bytes = AppTextEncoding::encode(text, encoding).bytes;
        for (int offset = 0; offset < bytes.size(); offset += 3) {
            // Odd-sized reads split two- and three-byte characters.
            page.handleReceivedData(bytes.mid(offset, 3));
        }
        QCOMPARE(receivedText(page), text);
    }

    void headerFramesEndWhenTheLineGoesQuiet()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_autoFrameBreakCheck->setChecked(true);
        page.m_frameModeCombo->setCurrentIndex(page.m_frameModeCombo->findData(QStringLiteral("header")));
        page.m_framePatternEdit->setText(QStringLiteral("AA 55"));
        setNumberEditValue(page.m_frameBreakIntervalEdit, 30, 1, 60000);
        QVERIFY(page.m_frameBreakIntervalEdit->isEnabled());

        // The next header ends the first frame; only a quiet line ends the last one.
        page.handleReceivedData(QByteArray::fromHex("AA5501AA5502"));
        QCOMPARE(page.m_records.size(), 1);
        QCOMPARE(page.m_records.first().bytes, QByteArray::fromHex("AA5501"));
        QTRY_COMPARE(page.m_records.size(), 2);
        QCOMPARE(page.m_records.last().bytes, QByteArray::fromHex("AA5502"));

        // A frame continued within the timeout stays whole.
        page.handleReceivedData(QByteArray::fromHex("AA55"));
        page.handleReceivedData(QByteArray::fromHex("0304"));
        QTRY_COMPARE(page.m_records.size(), 3);
        QCOMPARE(page.m_records.last().bytes, QByteArray::fromHex("AA550304"));
    }

    void autoReplyIsRecordedAfterItsRequest()
    {
        WorkbenchPage device(nullptr, false, false);
        WorkbenchPage responder(nullptr, false, false);
        connectVirtualPort(device);
        connectVirtualPort(responder, VirtualSerialPair::portBName());
        WorkbenchPage::AutoReplyRule rule;
        rule.matchMode = QStringLiteral("text");
        rule.pattern = QStringLiteral("PING");
        rule.responseMode = QStringLiteral("text");
        rule.responsePayload = QStringLiteral("PONG");
        rule.lineEnding = QStringLiteral("none");
        responder.m_autoReplyRules = {rule};

        QString error;
        QVERIFY2(device.controlSendBytes(QByteArrayLiteral("PING"), QString(), &error), qPrintable(error));
        QTRY_COMPARE(device.controlStatus().receivedBytes, 4);
        const auto records = responder.controlRecords(10, QStringLiteral("all"));
        QCOMPARE(records.size(), 2);
        QCOMPARE(records.at(0).direction, QStringLiteral("rx"));
        QCOMPARE(records.at(0).bytes, QByteArrayLiteral("PING"));
        QCOMPARE(records.at(1).direction, QStringLiteral("tx"));
        QCOMPARE(records.at(1).bytes, QByteArrayLiteral("PONG"));
        QVERIFY(records.at(0).timestamp <= records.at(1).timestamp);
    }

    void hardwareFlowControlLeavesRtsToTheDriver()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_flowControlCombo->setCurrentIndex(
            page.m_flowControlCombo->findData(static_cast<int>(QSerialPort::HardwareControl)));
        connectVirtualPort(page);
        QVERIFY(!page.m_rtsCheck->isEnabled());
        QVERIFY(page.m_dtrCheck->isEnabled());
        page.m_connectButton->click();
        page.m_flowControlCombo->setCurrentIndex(
            page.m_flowControlCombo->findData(static_cast<int>(QSerialPort::NoFlowControl)));
        connectVirtualPort(page);
        QVERIFY(page.m_rtsCheck->isEnabled());
    }

    void savedReceiveCaptureWaitsForAConnection()
    {
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        AppSettings settings;
        settings.setValue(QStringLiteral("receive/saveToFile"), true);
        settings.setValue(QStringLiteral("export/folder"), folder.path());
        settings.sync();
        const auto files = [&folder]() { return QDir(folder.path()).entryList(QDir::Files); };

        WorkbenchPage page(nullptr, true, false);
        QVERIFY(page.m_saveReceiveCheck->isChecked());
        QVERIFY(files().isEmpty());

        SerialController device;
        connectVirtualPort(page, VirtualSerialPair::portBName());
        SerialPortConfig config;
        config.portName = VirtualSerialPair::portAName();
        QVERIFY(device.openPort(config));
        QVERIFY(device.writeData(QByteArrayLiteral("captured")));
        QTRY_COMPARE(page.controlStatus().receivedBytes, 8);
        page.m_connectButton->click();
        QVERIFY(!page.m_serial.isOpen());
        QCOMPARE(files().size(), 1);
        QFile capture(QDir(folder.path()).filePath(files().first()));
        QVERIFY(capture.open(QIODevice::ReadOnly));
        QCOMPARE(capture.readAll(), QByteArrayLiteral("captured"));
    }
};

QTEST_MAIN(WorkbenchReliabilityTest)
#include "tst_workbench_reliability.moc"
