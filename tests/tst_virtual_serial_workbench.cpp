#include "app/core/app_settings.h"
#include "app/core/script_runner.h"
#include "app/serial/virtual_serial_pair.h"
#include "app/view/settings_page.h"
#include "app/view/workbench_page.h"
#include "app/view/workbench_sessions_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QTranslator>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

class VirtualSerialWorkbenchTest : public QObject
{
    Q_OBJECT

  private:
    static FluentQt::SwitchSettingCard *pairSwitch(SettingsPage &settings)
    {
        return settings.findChild<FluentQt::SwitchSettingCard *>(QStringLiteral("virtualSerialPairSwitch"));
    }

    static void connectUi(WorkbenchPage &page, const QString &port)
    {
        page.m_portCombo->setCurrentIndex(page.m_portCombo->findData(port));
        page.m_connectButton->click();
    }

    static void addReply(WorkbenchPage &page, const QString &pattern, const QString &response, int delayMs = 0)
    {
        WorkbenchPage::AutoReplyRule rule;
        rule.name = QStringLiteral("virtual test");
        rule.enabled = true;
        rule.matchMode = QStringLiteral("text");
        rule.pattern = pattern;
        rule.responseMode = QStringLiteral("text");
        rule.responsePayload = response;
        rule.lineEnding = QStringLiteral("none");
        rule.delayMs = delayMs;
        page.m_autoReplyRules.append(rule);
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

    void settingCreatesPortsInTwoRealSessions()
    {
        WorkbenchSessionsPage sessions;
        auto *tabs = sessions.findChild<FluentQt::TabWidget *>();
        QVERIFY(tabs);
        QCOMPARE(sessions.controlSessions().size(), 1);
        tabs->tabAddRequested();
        QCOMPARE(sessions.controlSessions().size(), 2);
        auto *first = dynamic_cast<WorkbenchPage *>(sessions.controlSessions().at(0).control);
        auto *second = dynamic_cast<WorkbenchPage *>(sessions.controlSessions().at(1).control);
        QVERIFY(first);
        QVERIFY(second);
        SettingsPage settings;
        auto *toggle = pairSwitch(settings);
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        QCOMPARE(first->m_portCombo->findData(VirtualSerialPair::portAName()), -1);
        toggle->switchButton()->toggle();
        QVERIFY(VirtualSerialPair::instance()->isEnabled());
        QVERIFY(AppSettings().value(QStringLiteral("serial/virtualPairEnabled")).toBool());
        QVERIFY(first->m_portCombo->findData(VirtualSerialPair::portAName()) >= 0);
        QVERIFY(second->m_portCombo->findData(VirtualSerialPair::portBName()) >= 0);

        connectUi(*first, VirtualSerialPair::portAName());
        connectUi(*second, VirtualSerialPair::portBName());
        QVERIFY(first->controlStatus().connected);
        QVERIFY(second->controlStatus().connected);
        first->m_hexSendCheck->setChecked(true);
        first->m_sendEdit->setPlainText(QStringLiteral("00 FF 01 80 0D 0A"));
        first->m_sendButton->click();
        QTRY_COMPARE(second->controlStatus().receivedBytes, 6);
        QCOMPARE(second->controlRecords(1, QStringLiteral("rx")).first().bytes, QByteArray::fromHex("00ff01800d0a"));
        QString error;
        QVERIFY2(second->controlSendBytes(QByteArray("hello A"), QString(), &error), qPrintable(error));
        QTRY_COMPARE(first->controlStatus().receivedBytes, 7);
        QCOMPARE(first->controlRecords(1, QStringLiteral("rx")).first().bytes, QByteArray("hello A"));
        QTRY_VERIFY(second->m_terminalView->document()->blockCount() >= 1);

        first->m_autoReconnectCheck->setChecked(true);
        second->m_autoReconnectCheck->setChecked(true);
        first->m_loopCheck->setChecked(true);
        QVERIFY(first->m_loopTimer.isActive());
        toggle->switchButton()->toggle();
        QVERIFY(!first->controlStatus().connected);
        QVERIFY(!second->controlStatus().connected);
        QVERIFY(!first->m_loopTimer.isActive());
        QVERIFY(!first->m_reconnectTimer.isActive());
        QVERIFY(!second->m_reconnectTimer.isActive());
        QCOMPARE(first->m_portCombo->findData(VirtualSerialPair::portAName()), -1);
        QCOMPARE(second->m_portCombo->findData(VirtualSerialPair::portBName()), -1);
        QVERIFY(first->currentSerialConfig().portName.isEmpty());
        QVERIFY(second->currentSerialConfig().portName.isEmpty());
        QVERIFY(!AppSettings().value(QStringLiteral("serial/virtualPairEnabled")).toBool());
        toggle->switchButton()->toggle();
        QVERIFY(!first->controlStatus().connected);
        QVERIFY(!second->controlStatus().connected);
        connectUi(*first, VirtualSerialPair::portAName());
        connectUi(*second, VirtualSerialPair::portBName());
        QVERIFY(first->controlStatus().connected);
        QVERIFY(second->controlStatus().connected);
    }

    void autoReplyWorksAcrossThePair()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        connectUi(first, VirtualSerialPair::portAName());
        connectUi(second, VirtualSerialPair::portBName());
        addReply(second, QStringLiteral("PING"), QStringLiteral("PONG"));
        QString error;
        QVERIFY(first.controlSendBytes(QByteArray("PING"), QString(), &error));
        QTRY_COMPARE(first.controlStatus().receivedBytes, 4);
        QCOMPARE(first.controlRecords(1, QStringLiteral("rx")).first().bytes, QByteArray("PONG"));
        QCOMPARE(second.controlRecords(1, QStringLiteral("rx")).first().bytes, QByteArray("PING"));
    }

    void delayedReplyAndPartialMatchDoNotCrossConnections()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        connectUi(first, VirtualSerialPair::portAName());
        connectUi(second, VirtualSerialPair::portBName());
        addReply(second, QStringLiteral("PING"), QStringLiteral("OLD"), 180);
        QString error;
        QVERIFY(first.controlSendBytes(QByteArray("PING"), QString(), &error));
        QTRY_COMPARE(second.controlStatus().receivedBytes, 4);
        VirtualSerialPair::instance()->setEnabled(false);
        VirtualSerialPair::instance()->setEnabled(true);
        connectUi(first, VirtualSerialPair::portAName());
        connectUi(second, VirtualSerialPair::portBName());
        QTest::qWait(220);
        QCOMPARE(first.controlStatus().receivedBytes, 0);

        second.m_autoReplyRules.first().delayMs = 0;
        QVERIFY(first.controlSendBytes(QByteArray("PI"), QString(), &error));
        QTRY_COMPARE(second.m_autoReplyBuffer, QByteArray("PI"));
        VirtualSerialPair::instance()->setEnabled(false);
        VirtualSerialPair::instance()->setEnabled(true);
        connectUi(first, VirtualSerialPair::portAName());
        connectUi(second, VirtualSerialPair::portBName());
        QVERIFY(first.controlSendBytes(QByteArray("NG"), QString(), &error));
        QTRY_COMPARE(second.m_autoReplyBuffer, QByteArray("NG"));
        QCOMPARE(first.controlStatus().receivedBytes, 0);
    }

    void stoppedScriptCannotSendToReopenedPair()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        WorkbenchPage first(nullptr, false, false);
        WorkbenchPage second(nullptr, false, false);
        connectUi(first, VirtualSerialPair::portAName());
        connectUi(second, VirtualSerialPair::portBName());
        first.m_scriptEdit->setPlainText(QStringLiteral("serial.sleep(200); serial.sendText('OLD');"));
        first.startScript();
        QVERIFY(first.m_scriptRunning);
        VirtualSerialPair::instance()->setEnabled(false);
        VirtualSerialPair::instance()->setEnabled(true);
        connectUi(first, VirtualSerialPair::portAName());
        connectUi(second, VirtualSerialPair::portBName());
        QTRY_VERIFY(!first.m_scriptRunning && first.m_scriptRunner == nullptr);
        QCOMPARE(second.controlStatus().receivedBytes, 0);
    }

    void preStartScriptCancellationIsPreserved()
    {
        ScriptRunner runner;
        QSignalSpy finished(&runner, &ScriptRunner::finished);
        runner.requestStop();
        runner.runScript(QStringLiteral("while (true) {}"), QStringLiteral("cancelled.js"));
        QCOMPARE(finished.size(), 1);
        QVERIFY(!finished.first().first().toBool());
    }

    void startupDoesNotOpenAReplacedSelection()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        AppSettings config;
        config.setValue(QStringLiteral("serial/portName"), VirtualSerialPair::portAName());
        config.setValue(QStringLiteral("serial/autoOpen"), true);
        config.sync();
        WorkbenchPage page(nullptr, true, true);
        page.m_portCombo->setCurrentIndex(page.m_portCombo->findData(VirtualSerialPair::portBName()));
        QTest::qWait(300);
        QVERIFY(!page.m_serial.isOpen());
        VirtualSerialPair::instance()->setEnabled(false);
        WorkbenchPage disabled(nullptr, true, true);
        QVERIFY(disabled.currentSerialConfig().portName.isEmpty());
        QTest::qWait(300);
        QVERIFY(!disabled.m_serial.isOpen());
    }

    void settingsSwitchStaysSynchronizedAndTranslates()
    {
        SettingsPage first;
        auto *firstSwitch = pairSwitch(first);
        QVERIFY(firstSwitch);
        firstSwitch->setChecked(true);
        SettingsPage second;
        auto *secondSwitch = pairSwitch(second);
        QVERIFY(secondSwitch);
        QVERIFY(secondSwitch->isChecked());
        secondSwitch->setChecked(false);
        QVERIFY(!firstSwitch->isChecked());
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/app/i18n/fluentserialassistant.en_US.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        SettingsPage english;
        auto *englishSwitch = pairSwitch(english);
        QVERIFY(englishSwitch);
        QCOMPARE(englishSwitch->title(), QStringLiteral("Built-in virtual serial pair"));
        QVERIFY(englishSwitch->content().contains(QStringLiteral("VIRTUAL-A")));
        QVERIFY(!englishSwitch->content().contains(QStringLiteral("两个会话")));
        english.resize(1040, 800);
        english.show();
        QCoreApplication::processEvents();
        const auto *label = englishSwitch->contentLabel();
        const QRect textBounds =
            label->fontMetrics().boundingRect(QRect(0, 0, label->width(), 1000), Qt::TextWordWrap, label->text());
        const QString screenshot = qEnvironmentVariable("FLUENT_TEST_SETTINGS_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QVERIFY(englishSwitch->grab().save(screenshot));
        }
        QVERIFY2(englishSwitch->rect().contains(englishSwitch->titleLabel()->geometry()),
                 qPrintable(QStringLiteral("Title bounds: %1,%2 %3x%4")
                                .arg(englishSwitch->titleLabel()->x())
                                .arg(englishSwitch->titleLabel()->y())
                                .arg(englishSwitch->titleLabel()->width())
                                .arg(englishSwitch->titleLabel()->height())));
        QVERIFY2(label->height() >= textBounds.height(),
                 qPrintable(QStringLiteral("Label %1x%2 requires height %3; card %4x%5")
                                .arg(label->width())
                                .arg(label->height())
                                .arg(textBounds.height())
                                .arg(englishSwitch->width())
                                .arg(englishSwitch->height())));
        QCoreApplication::removeTranslator(&translator);
    }
};

QTEST_MAIN(VirtualSerialWorkbenchTest)
#include "tst_virtual_serial_workbench.moc"
