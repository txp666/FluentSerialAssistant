#include "app/view/main_window.h"

#include "app/view/settings_page.h"
#include "app/view/workbench_sessions_page.h"

#include <QtGui/QCloseEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtGui/QScreen>

using namespace FluentQt;

MainWindow::MainWindow(QWidget *parent) : MSFluentWindow(parent)
{
    setWindowTitle(QStringLiteral("Fluent 串口助手"));
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
    addSubInterface(m_workbenchPage, icon(FluentIcon::CommandPrompt), QStringLiteral("终端"));
    connect(m_workbenchPage, &WorkbenchSessionsPage::settingsRequested, this,
            [this]() { switchTo(QStringLiteral("settings")); });

    auto *settingsPage = new SettingsPage(this);
    settingsPage->setObjectName(QStringLiteral("settings"));
    addSubInterface(settingsPage, icon(FluentIcon::Setting), QStringLiteral("设置"), QIcon(),
                    NavigationItemPosition::Bottom);
    connect(settingsPage, &SettingsPage::terminalRequested, this, [this]() { switchTo(QStringLiteral("workbench")); });
    connect(settingsPage, &SettingsPage::terminalFontChanged, m_workbenchPage,
            &WorkbenchSessionsPage::setTerminalFontFamily);
}
