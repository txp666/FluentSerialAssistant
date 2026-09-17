#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/update_manager.h"
#include "app/view/settings_page.h"
#include "app/view/update_dialog.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtGui/QPixmap>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <memory>

class UpdateUiTest : public QObject
{
    Q_OBJECT

    using State = AppUpdate::UpdateManager::State;

    static void setRelease(AppUpdate::UpdateManager &manager)
    {
        manager.m_release.version = QStringLiteral("99.0.0");
        manager.m_release.notes = QStringLiteral("## Release notes\n\n- Fix multiple reply rules.\n- Improve tables.");
        manager.m_release.size = 1000000;
        manager.m_state = State::Available;
        manager.m_message = QStringLiteral("An update is available");
    }

    static void setState(AppUpdate::UpdateManager &manager, State state, const QString &message = QString())
    {
        manager.m_state = state;
        manager.m_message = message;
        emit manager.stateChanged(state);
    }

    static void disconnectDownload(UpdateDialog &dialog, AppUpdate::UpdateManager &manager)
    {
        QObject::disconnect(&dialog, &UpdateDialog::downloadRequested, &manager,
                            &AppUpdate::UpdateManager::downloadUpdate);
    }

    static void prepareCapture(QWidget &host, AppUpdate::UpdateManager &manager)
    {
        if (qEnvironmentVariableIsEmpty("FLUENT_UPDATE_UI_CAPTURE_DIR")) {
            return;
        }
        const QString notes = QStringLiteral(
            "## 更新内容\n\n"
            "- 自动应答可以连续添加多条规则，选中已有规则后可单独修改；文本、HEX 和正则匹配均可使用。\n"
            "- 大量数据记录采用增量批量更新，持续接收数据时保持表格流畅，并保留排序、筛选和记录定位功能。\n"
            "- 在设置中开启内置虚拟串口对，两个会话分别连接 VIRTUAL-A 和 VIRTUAL-B，即可测试双向通信。\n\n"
            "### 安装说明\n\n"
            "确认后开始下载安装包。下载期间可以查看进度和取消更新，安装包校验通过后，应用将保存配置并退出安装。\n\n");
        manager.m_release.notes = notes + manager.m_release.notes + QStringLiteral("\n\n") + notes.repeated(3);
        host.resize(1040, 700);
        host.show();
        QCoreApplication::processEvents();
    }

    static bool captureDialog(UpdateDialog &dialog, const QString &fileName)
    {
        const QString directory = qEnvironmentVariable("FLUENT_UPDATE_UI_CAPTURE_DIR");
        if (directory.isEmpty()) {
            return true;
        }
        if (!QDir().mkpath(directory)) {
            return false;
        }
        QCoreApplication::processEvents();
        QTest::qWait(250);
        QCoreApplication::processEvents();
        return dialog.grab().save(QDir(directory).filePath(fileName));
    }

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentQt::FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
        QCoreApplication::setApplicationVersion(QStringLiteral("0.1.13"));
    }

    void constructingSettingsDoesNotCheckOrDownload()
    {
        SettingsPage settings;
        const auto managers = settings.findChildren<AppUpdate::UpdateManager *>();
        QCOMPARE(managers.size(), 1);
        QSignalSpy changes(managers.first(), &AppUpdate::UpdateManager::stateChanged);
        QCoreApplication::processEvents();
        QCOMPARE(managers.first()->state(), State::Idle);
        QVERIFY(!managers.first()->m_downloadAuthorized);
        QVERIFY(changes.isEmpty());
    }

    void settingsSharesManagerAndStartupResultsStayQuiet()
    {
        QWidget host;
        AppUpdate::UpdateManager manager;
        SettingsPage settings(&host, &manager);
        QVERIFY(settings.findChildren<AppUpdate::UpdateManager *>().isEmpty());
        auto *card = settings.findChild<FluentQt::PushSettingCard *>(QStringLiteral("applicationUpdateCard"));
        QVERIFY(card);

        manager.m_automatic = true;
        setState(manager, State::Checking);
        QVERIFY(!card->button()->isEnabled());
        setState(manager, State::UpToDate);
        QVERIFY(card->button()->isEnabled());
        QVERIFY(host.findChildren<FluentQt::InfoBar *>().isEmpty());
        setState(manager, State::Checking);
        setState(manager, State::Failed, QStringLiteral("Offline"));
        QVERIFY(host.findChildren<FluentQt::InfoBar *>().isEmpty());

        manager.m_automatic = false;
        setState(manager, State::Checking);
        setState(manager, State::Failed, QStringLiteral("Offline"));
        QCOMPARE(host.findChildren<FluentQt::InfoBar *>().size(), 1);
    }

    void manualNoUpdateShowsFeedback()
    {
        QWidget host;
        AppUpdate::UpdateManager manager;
        SettingsPage settings(&host, &manager);
        manager.m_automatic = false;
        setState(manager, State::Checking);
        setState(manager, State::UpToDate);
        QCOMPARE(host.findChildren<FluentQt::InfoBar *>().size(), 1);
    }

    void availableUpdateShowsNotesAndRequiresConfirmation()
    {
        QWidget host;
        host.resize(1120, 900);
        AppUpdate::UpdateManager manager;
        setRelease(manager);
        prepareCapture(host, manager);
        SettingsPage settings(&host, &manager);
        QSignalSpy prompts(&settings, &SettingsPage::updateDialogRequested);
        emit manager.updateFound();
        QCOMPARE(prompts.size(), 1);
        QVERIFY(!manager.m_downloadAuthorized);

        UpdateDialog dialog(&manager, &host);
        disconnectDownload(dialog, manager);
        QSignalSpy downloads(&dialog, &UpdateDialog::downloadRequested);
        dialog.open();
        QCoreApplication::processEvents();
        QVERIFY(dialog.view()->width() >= 560);
        auto *notes = dialog.findChild<FluentQt::TextBrowser *>(QStringLiteral("updateReleaseNotes"));
        QVERIFY(notes);
        QVERIFY(notes->toPlainText().contains(QStringLiteral("Fix multiple reply rules.")));
        QVERIFY(dialog.findChild<QLabel *>(QStringLiteral("updateInstallNotice")));
        QVERIFY(dialog.yesButton()->isEnabled());
        QCOMPARE(dialog.yesButton()->text(), AppI18n::text("下载并安装"));
        QVERIFY(downloads.isEmpty());
        QVERIFY(captureDialog(dialog, QStringLiteral("confirmation.png")));

        dialog.yesButton()->click();
        QCOMPARE(downloads.size(), 1);
        QVERIFY(dialog.isVisible());
        setState(manager, State::Downloading, QStringLiteral("Downloading"));
        QVERIFY(!dialog.yesButton()->isEnabled());
        dialog.yesButton()->click();
        QCOMPARE(downloads.size(), 1);
    }

    void progressAndFailureRemainInTheDialog()
    {
        QWidget host;
        host.resize(1120, 900);
        AppUpdate::UpdateManager manager;
        setRelease(manager);
        prepareCapture(host, manager);
        UpdateDialog dialog(&manager, &host);
        disconnectDownload(dialog, manager);
        QSignalSpy downloads(&dialog, &UpdateDialog::downloadRequested);
        dialog.open();
        setState(manager, State::Downloading, QStringLiteral("Downloading with four connections"));
        manager.m_received = 375000;
        emit manager.progressChanged(375000, 1000000);
        auto *progress = dialog.findChild<FluentQt::ProgressBar *>(QStringLiteral("updateDownloadProgress"));
        auto *bytes = dialog.findChild<QLabel *>(QStringLiteral("updateDownloadBytes"));
        auto *status = dialog.findChild<QLabel *>(QStringLiteral("updateStatusLabel"));
        QVERIFY(progress);
        QVERIFY(bytes);
        QVERIFY(status);
        QCOMPARE(progress->value(), 37);
        QVERIFY(bytes->text().startsWith(QStringLiteral("37%")));
        QCOMPARE(status->text(), QStringLiteral("Downloading with four connections"));
        QVERIFY(captureDialog(dialog, QStringLiteral("progress.png")));
        setState(manager, State::Failed, QStringLiteral("Connection interrupted"));
        QCOMPARE(status->text(), QStringLiteral("Connection interrupted"));
        QVERIFY(progress->isError());
        QVERIFY(dialog.yesButton()->isEnabled());
        QCOMPARE(dialog.yesButton()->text(), AppI18n::text("重试下载并安装"));
        QVERIFY(downloads.isEmpty());
        dialog.yesButton()->click();
        QCOMPARE(downloads.size(), 1);
        QVERIFY(dialog.isVisible());
    }

    void dismissingActiveUpdateCancels_data()
    {
        QTest::addColumn<int>("state");
        QTest::addColumn<int>("action");
        for (int action = 0; action < 4; ++action) {
            QTest::addRow("download-%d", action) << static_cast<int>(State::Downloading) << action;
            QTest::addRow("verify-%d", action) << static_cast<int>(State::Verifying) << action;
        }
    }

    void dismissingActiveUpdateCancels()
    {
        QFETCH(int, state);
        QFETCH(int, action);
        QWidget host;
        host.resize(1120, 900);
        AppUpdate::UpdateManager manager;
        setRelease(manager);
        manager.m_downloadAuthorized = true;
        manager.m_state = static_cast<State>(state);
        auto dialog = std::make_unique<UpdateDialog>(&manager, &host);
        dialog->open();
        switch (action) {
        case 0:
            dialog->cancelButton()->click();
            break;
        case 1:
            QTest::keyClick(dialog.get(), Qt::Key_Escape);
            break;
        case 2:
            dialog->close();
            break;
        default:
            dialog.reset();
            break;
        }
        QCOMPARE(manager.state(), State::Cancelled);
        QVERIFY(!manager.m_downloadAuthorized);
    }

    void installationHandoffHasNoSecondConfirmation()
    {
        QWidget host;
        host.resize(1120, 900);
        AppUpdate::UpdateManager manager;
        setRelease(manager);
        UpdateDialog dialog(&manager, &host);
        disconnectDownload(dialog, manager);
        QSignalSpy downloads(&dialog, &UpdateDialog::downloadRequested);
        dialog.open();
        setState(manager, State::Installing, QStringLiteral("Starting installer"));
        QVERIFY(!dialog.yesButton()->isEnabled());
        QVERIFY(!dialog.cancelButton()->isEnabled());
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QVERIFY(dialog.isVisible());
        QVERIFY(!dialog.close());
        QVERIFY(dialog.isVisible());
        QVERIFY(downloads.isEmpty());
    }
};

QTEST_MAIN(UpdateUiTest)
#include "tst_update_ui.moc"
