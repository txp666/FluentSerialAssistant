#include "app/view/main_window.h"
#include "app/control/local_control_server.h"
#include "app/control/workbench_control_service.h"
#include "app/core/app_i18n.h"
#include "app/core/update_manager.h"

#include "app/view/settings_page.h"
#include "app/view/update_dialog.h"
#include "app/view/workbench_sessions_page.h"

#include <QtCore/QCoreApplication>
#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QScreen>

using namespace FluentQt;

MainWindow::MainWindow(QWidget *parent) : MSFluentWindow(parent)
{
    setWindowTitle(AppI18n::text("Fluent 串口助手"));
    setWindowIcon(QIcon(QStringLiteral(":/app/logo.png")));
    setMinimumSize(1040, 700);
    resize(1120, 900);
    QScreen *targetScreen = screen();
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen) {
        const QRect available = targetScreen->availableGeometry();
        move(available.center() - rect().center());
    }

    m_updateManager = new AppUpdate::UpdateManager(this);
    populateInterfaces();
    connect(m_updateManager, &AppUpdate::UpdateManager::installationReady, this, [this]() {
        m_installationReady = true;
        if (m_updateDialog) {
            m_updateDialog->hide();
        }
        close();
        QCoreApplication::quit();
    });
    m_controlService = new AppControl::WorkbenchControlService(m_workbenchPage, this);
    m_controlServer = new AppControl::LocalControlServer(m_controlService, this);
    QString controlError;
    if (!m_controlServer->start(&controlError)) {
        qWarning().noquote() << "Local control service unavailable:" << controlError;
    }
    navigationInterface()->setVisible(false);
    navigationInterface()->setFixedWidth(0);
    switchTo(QStringLiteral("workbench"));
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_updateManager && !m_installationReady) {
        if (m_updateManager->state() == AppUpdate::UpdateManager::State::Installing) {
            event->ignore();
            return;
        }
        m_updateManager->cancelDownload();
    }
    if (m_workbenchPage) {
        m_workbenchPage->saveSettings();
    }
    MSFluentWindow::closeEvent(event);
}

void MainWindow::startUpdateCheck()
{
    if (m_startupUpdateCheckStarted || !m_updateManager) {
        return;
    }
    m_startupUpdateCheckStarted = true;
    m_updateManager->checkForUpdates(true);
}

void MainWindow::showUpdateDialog()
{
    if (!m_updateManager || m_updateManager->release().version.isEmpty()) {
        return;
    }
    if (!m_updateDialog) {
        m_updateDialog = new UpdateDialog(m_updateManager, this);
        m_updateDialog->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_updateDialog->open();
    m_updateDialog->raise();
}

void MainWindow::populateInterfaces()
{
    m_workbenchPage = new WorkbenchSessionsPage(this);
    m_workbenchPage->setObjectName(QStringLiteral("workbench"));
    addSubInterface(m_workbenchPage, icon(FluentIcon::CommandPrompt), AppI18n::text("终端"));
    m_workbenchPage->installTitleBarTabs(titleBar());
    connect(m_workbenchPage, &WorkbenchSessionsPage::settingsRequested, this, [this]() {
        if (m_workbenchPage) {
            m_workbenchPage->setTitleBarTabsVisible(false);
        }
        switchTo(QStringLiteral("settings"));
    });

    auto *settingsPage = new SettingsPage(this, m_updateManager);
    settingsPage->setObjectName(QStringLiteral("settings"));
    addSubInterface(settingsPage, icon(FluentIcon::Setting), AppI18n::text("设置"), QIcon(),
                    NavigationItemPosition::Bottom);
    connect(settingsPage, &SettingsPage::terminalRequested, this, [this]() {
        switchTo(QStringLiteral("workbench"));
        if (m_workbenchPage) {
            m_workbenchPage->setTitleBarTabsVisible(true);
        }
    });
    connect(settingsPage, &SettingsPage::terminalFontChanged, m_workbenchPage,
            &WorkbenchSessionsPage::setTerminalFontFamily);
    connect(settingsPage, &SettingsPage::updateDialogRequested, this, &MainWindow::showUpdateDialog);
}
