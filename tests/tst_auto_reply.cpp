#include "app/view/workbench/workbench_page_internal.h"

#include <QtCore/QTranslator>
#include <QtTest/QTest>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

using namespace WorkbenchPagePrivate;

class AutoReplyTest : public QObject
{
    Q_OBJECT

  private:
    static void fillRule(WorkbenchPage &page, const QString &name, const QString &pattern, const QString &payload)
    {
        page.m_autoReplyNameEdit->setText(name);
        page.m_autoReplyPatternEdit->setText(pattern);
        page.m_autoReplyPayloadEdit->setPlainText(payload);
    }

    static void addRule(WorkbenchPage &page, int number)
    {
        page.m_autoReplyNewButton->click();
        fillRule(page, QStringLiteral("Rule %1").arg(number), QStringLiteral("request%1").arg(number),
                 QStringLiteral("response%1").arg(number));
        page.m_autoReplySaveButton->click();
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
        AppSettings settings;
        settings.clear();
        settings.sync();
    }

    void createFourRulesAndEditSelectedRule()
    {
        WorkbenchPage page(nullptr, false, false);
        QCOMPARE(page.m_autoReplyList->currentRow(), -1);
        QCOMPARE(page.m_autoReplySaveButton->text(), AppI18n::text("添加"));
        for (int number = 1; number <= 4; ++number) {
            addRule(page, number);
            QCOMPARE(page.m_autoReplyRules.size(), number);
            QCOMPARE(page.m_autoReplyList->count(), number);
            QCOMPARE(page.m_autoReplyList->currentRow(), number - 1);
        }

        page.m_autoReplyList->setCurrentRow(1);
        QCOMPARE(page.m_autoReplyPatternEdit->text(), QStringLiteral("request2"));
        QCOMPARE(page.m_autoReplySaveButton->text(), AppI18n::text("保存"));
        fillRule(page, QStringLiteral("Edited"), QStringLiteral("changed"), QStringLiteral("new reply"));
        page.m_autoReplySaveButton->click();
        QCOMPARE(page.m_autoReplyRules.size(), 4);
        QCOMPARE(page.m_autoReplyRules.at(1).pattern, QStringLiteral("changed"));
        QCOMPARE(page.m_autoReplyRules.at(0).pattern, QStringLiteral("request1"));
        QCOMPARE(page.m_autoReplyRules.at(2).pattern, QStringLiteral("request3"));
        QCOMPARE(page.m_autoReplyRules.at(3).pattern, QStringLiteral("request4"));
    }

    void newRuleResetsEditorAndSurvivesRefresh()
    {
        WorkbenchPage page(nullptr, false, false);
        addRule(page, 1);
        page.m_autoReplyEnabledCheck->setChecked(false);
        page.m_autoReplyMatchModeCombo->setCurrentIndex(1);
        page.m_autoReplyResponseModeCombo->setCurrentIndex(1);
        page.m_autoReplyLineEndingCombo->setCurrentIndex(3);
        setNumberEditValue(page.m_autoReplyDelayEdit, 123, 0, 600000);
        page.m_autoReplyNewButton->click();

        QCOMPARE(page.m_autoReplyList->currentRow(), -1);
        QVERIFY(page.m_autoReplyNameEdit->text().isEmpty());
        QVERIFY(page.m_autoReplyPatternEdit->text().isEmpty());
        QVERIFY(page.m_autoReplyPayloadEdit->toPlainText().isEmpty());
        QVERIFY(page.m_autoReplyEnabledCheck->isChecked());
        QCOMPARE(page.m_autoReplyMatchModeCombo->currentIndex(), 0);
        QCOMPARE(page.m_autoReplyResponseModeCombo->currentIndex(), 0);
        QCOMPARE(page.m_autoReplyLineEndingCombo->currentIndex(), 0);
        QCOMPARE(numberEditValue(page.m_autoReplyDelayEdit, -1, 0, 600000), 0);
        QVERIFY(!page.m_autoReplyDeleteButton->isEnabled());
        QVERIFY(!page.m_autoReplyUpButton->isEnabled());
        QVERIFY(!page.m_autoReplyDownButton->isEnabled());

        fillRule(page, QStringLiteral("Rule 1"), QStringLiteral("second"), QStringLiteral("second reply"));
        page.updateAutoReplyTable(page.m_autoReplyList->currentRow());
        QCOMPARE(page.m_autoReplyList->currentRow(), -1);
        QCOMPARE(page.m_autoReplyPatternEdit->text(), QStringLiteral("second"));
        page.m_autoReplySaveButton->click();
        QCOMPARE(page.m_autoReplyRules.size(), 2);
        QCOMPARE(page.m_autoReplyRules.at(0).pattern, QStringLiteral("request1"));
        QCOMPARE(page.m_autoReplyRules.at(1).pattern, QStringLiteral("second"));
    }

    void savedRulesReloadAndReorder()
    {
        WorkbenchPage page(nullptr, false, false);
        for (int number = 1; number <= 4; ++number) {
            addRule(page, number);
        }
        page.m_autoReplyList->setCurrentRow(3);
        page.m_autoReplyUpButton->click();
        QCOMPARE(page.m_autoReplyList->currentRow(), 2);
        QCOMPARE(page.m_autoReplyRules.at(2).pattern, QStringLiteral("request4"));

        WorkbenchPage restored(nullptr, false, false);
        restored.loadAutoReplyRules();
        QCOMPARE(restored.m_autoReplyRules.size(), 4);
        QCOMPARE(restored.m_autoReplyList->count(), 4);
        QCOMPARE(restored.m_autoReplyList->currentRow(), 0);
        QCOMPARE(restored.m_autoReplyPatternEdit->text(), QStringLiteral("request1"));
        for (int row = 0; row < 4; ++row) {
            QCOMPARE(restored.m_autoReplyRules.at(row).pattern, page.m_autoReplyRules.at(row).pattern);
            QCOMPARE(restored.m_autoReplyRules.at(row).responsePayload, page.m_autoReplyRules.at(row).responsePayload);
            QVERIFY(restored.autoReplyRuleMatches(restored.m_autoReplyRules.at(row),
                                                  page.m_autoReplyRules.at(row).pattern.toUtf8()));
        }
    }

    void deleteKeepsSelectionAndEditorInSync()
    {
        WorkbenchPage page(nullptr, false, false);
        for (int number = 1; number <= 3; ++number) {
            addRule(page, number);
        }
        page.m_autoReplyList->setCurrentRow(1);
        page.m_autoReplyDeleteButton->click();
        QCOMPARE(page.m_autoReplyRules.size(), 2);
        QCOMPARE(page.m_autoReplyList->currentRow(), 1);
        QCOMPARE(page.m_autoReplyPatternEdit->text(), QStringLiteral("request3"));
        page.m_autoReplySaveButton->click();
        QCOMPARE(page.m_autoReplyRules.at(1).pattern, QStringLiteral("request3"));
        page.m_autoReplyDeleteButton->click();
        QCOMPARE(page.m_autoReplyPatternEdit->text(), QStringLiteral("request1"));
        page.m_autoReplyDeleteButton->click();
        QCOMPARE(page.m_autoReplyList->currentRow(), -1);
        QVERIFY(page.m_autoReplyPatternEdit->text().isEmpty());
        QCOMPARE(page.m_autoReplySaveButton->text(), AppI18n::text("添加"));
    }

    void copyingSessionPreservesDraftOrSelectedRule()
    {
        WorkbenchPage source(nullptr, false, false);
        addRule(source, 1);
        addRule(source, 2);
        WorkbenchPage copied(nullptr, false, false);
        copied.copySessionConfigFrom(source);
        QCOMPARE(copied.m_autoReplyList->currentRow(), 1);
        fillRule(copied, QStringLiteral("Edited"), QStringLiteral("edited"), QStringLiteral("reply"));
        copied.m_autoReplySaveButton->click();
        QCOMPARE(copied.m_autoReplyRules.size(), 2);
        QCOMPARE(copied.m_autoReplyRules.at(1).pattern, QStringLiteral("edited"));
        QCOMPARE(source.m_autoReplyRules.at(1).pattern, QStringLiteral("request2"));

        source.m_autoReplyNewButton->click();
        fillRule(source, QStringLiteral("Draft"), QStringLiteral("draft"), QStringLiteral("reply"));
        copied.copySessionConfigFrom(source);
        QCOMPARE(copied.m_autoReplyList->currentRow(), -1);
        QCOMPARE(copied.m_autoReplyPatternEdit->text(), QStringLiteral("draft"));
        copied.m_autoReplySaveButton->click();
        QCOMPARE(copied.m_autoReplyRules.size(), 3);
        QCOMPARE(copied.m_autoReplyRules.at(2).pattern, QStringLiteral("draft"));
    }

    void allMatchingEnabledRulesReply()
    {
#ifdef Q_OS_UNIX
        struct PseudoTerminal
        {
            int descriptor = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
            ~PseudoTerminal()
            {
                if (descriptor >= 0) {
                    ::close(descriptor);
                }
            }
        } terminal;
        QVERIFY(terminal.descriptor >= 0);
        QCOMPARE(grantpt(terminal.descriptor), 0);
        QCOMPARE(unlockpt(terminal.descriptor), 0);
        const char *portName = ptsname(terminal.descriptor);
        QVERIFY(portName);

        WorkbenchPage page(nullptr, false, false);
        const QStringList modes = {QStringLiteral("text"), QStringLiteral("regex"), QStringLiteral("hex"),
                                   QStringLiteral("text")};
        const QStringList patterns = {QStringLiteral("PING"), QStringLiteral("^P.NG$"), QStringLiteral("50 49 4E 47"),
                                      QStringLiteral("PING")};
        for (int row = 0; row < modes.size(); ++row) {
            page.m_autoReplyNewButton->click();
            fillRule(page, modes.at(row), patterns.at(row), QStringLiteral("reply%1").arg(row));
            page.m_autoReplyMatchModeCombo->setCurrentIndex(page.m_autoReplyMatchModeCombo->findData(modes.at(row)));
            page.m_autoReplyEnabledCheck->setChecked(row != 3);
            page.m_autoReplySaveButton->click();
        }
        QCOMPARE(page.m_autoReplyRules.size(), 4);

        SerialPortConfig config;
        config.portName = QString::fromLocal8Bit(portName);
        QVERIFY2(page.m_serial.openPort(config), qPrintable(page.m_serial.errorString()));
        QCOMPARE(::write(terminal.descriptor, "PI", 2), 2);
        QTRY_COMPARE(page.m_autoReplyBuffer, QByteArrayLiteral("PI"));
        QCOMPARE(::write(terminal.descriptor, "NG", 2), 2);

        QByteArray received;
        const auto replies = [&]() {
            char buffer[256];
            const auto count = ::read(terminal.descriptor, buffer, sizeof(buffer));
            if (count > 0) {
                received.append(buffer, count);
            }
            return received;
        };
        QTRY_COMPARE(replies(), QByteArrayLiteral("reply0reply1reply2"));
        QVERIFY(page.m_autoReplyBuffer.isEmpty());
        page.m_serial.closePort();
#else
        QSKIP("Pseudo-terminal serial integration is available on Unix platforms");
#endif
    }

    void englishButtonLabels()
    {
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/app/i18n/fluentserialassistant.en_US.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        WorkbenchPage page(nullptr, false, false);
        QCOMPARE(page.m_autoReplyNewButton->text(), QStringLiteral("New"));
        QCOMPARE(page.m_autoReplySaveButton->text(), QStringLiteral("Add"));
        addRule(page, 1);
        QCOMPARE(page.m_autoReplySaveButton->text(), QStringLiteral("Save"));
        page.m_autoReplyNewButton->click();
        QCOMPARE(page.m_autoReplySaveButton->text(), QStringLiteral("Add"));
        QCoreApplication::removeTranslator(&translator);
    }
};

QTEST_MAIN(AutoReplyTest)
#include "tst_auto_reply.moc"
