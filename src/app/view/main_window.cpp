#include "app/view/main_window.h"
#include "app/control/local_control_server.h"
#include "app/control/workbench_control_service.h"
#include "app/core/app_i18n.h"

#include "app/view/settings_page.h"
#include "app/view/workbench_sessions_page.h"

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

    populateInterfaces();
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
    if (m_workbenchPage) {
        m_workbenchPage->saveSettings();
    }
    MSFluentWindow::closeEvent(event);
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

    auto *settingsPage = new SettingsPage(this);
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
}
