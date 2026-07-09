#include "app/view/workbench_sessions_page.h"

#include "app/view/workbench_page.h"

#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>

using namespace FluentQt;

WorkbenchSessionsPage::WorkbenchSessionsPage(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_tabs = new TabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setAddButtonVisible(true);
    m_tabs->setMovable(true);
    if (m_tabs->tabBar()) {
        m_tabs->tabBar()->setScrollable(true);
        m_tabs->tabBar()->setTabMaximumWidth(180);
        m_tabs->tabBar()->setTabShadowEnabled(false);
        if (m_tabs->tabBar()->addButton()) {
            m_tabs->tabBar()->addButton()->setToolTip(QStringLiteral("复制当前会话"));
        }
    }
    layout->addWidget(m_tabs, 1);

    addSession(nullptr, true);

    connect(m_tabs, &TabWidget::tabAddRequested, this, [this]() { addSession(currentSession()); });
    connect(m_tabs, &TabWidget::tabCloseRequested, this, &WorkbenchSessionsPage::closeSession);
}

void WorkbenchSessionsPage::saveSettings() const
{
    if (auto *session = currentSession()) {
        session->saveSettings();
    }
}

void WorkbenchSessionsPage::installTitleBarTabs(FluentTitleBar *titleBar)
{
    if (!titleBar || !m_tabs || !m_tabs->tabBar()) {
        return;
    }

    m_titleBar = titleBar;
    auto *tabBar = m_tabs->tabBar();
    if (tabBar->parentWidget() != titleBar) {
        if (QLayout *sourceLayout = m_tabs->layout()) {
            sourceLayout->removeWidget(tabBar);
        }
        tabBar->setParent(titleBar);
        tabBar->setFixedHeight(40);
        tabBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        if (auto *titleLayout = qobject_cast<QHBoxLayout *>(titleBar->layout())) {
            int insertIndex = 0;
            if (titleBar->titleLabel()) {
                for (int i = 0; i < titleLayout->count(); ++i) {
                    if (titleLayout->itemAt(i) && titleLayout->itemAt(i)->widget() == titleBar->titleLabel()) {
                        insertIndex = i + 1;
                        break;
                    }
                }
            }
            titleLayout->insertWidget(insertIndex, tabBar, 100, Qt::AlignVCenter);
        }
    }

    setTitleBarTabsVisible(true);
}

void WorkbenchSessionsPage::setTitleBarTabsVisible(bool visible)
{
    if (!m_tabs || !m_tabs->tabBar()) {
        return;
    }

    m_tabs->tabBar()->setVisible(visible);
    if (!m_titleBar || m_tabs->tabBar()->parentWidget() != m_titleBar) {
        return;
    }

    if (m_titleBar->iconLabel()) {
        m_titleBar->iconLabel()->setVisible(true);
    }
    if (m_titleBar->titleLabel()) {
        m_titleBar->titleLabel()->setVisible(!visible);
    }
}

void WorkbenchSessionsPage::setTerminalFontFamily(const QString &family)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *session = qobject_cast<WorkbenchPage *>(m_tabs->widget(i))) {
            session->setTerminalFontFamily(family);
        }
    }
}

WorkbenchPage *WorkbenchSessionsPage::addSession(WorkbenchPage *source, bool restoreSavedSession)
{
    auto *session = new WorkbenchPage(m_tabs->stackedWidget(), restoreSavedSession, restoreSavedSession);
    if (source) {
        session->copySessionConfigFrom(*source);
    }
    connect(session, &WorkbenchPage::settingsRequested, this, &WorkbenchSessionsPage::settingsRequested);

    const int index = m_tabs->addTab(session, nextTitle(), icon(FluentIcon::CommandPrompt), nextRouteKey());
    if (index >= 0) {
        m_tabs->setCurrentIndex(index);
        ++m_nextSession;
    } else {
        session->deleteLater();
        return nullptr;
    }
    return session;
}

WorkbenchPage *WorkbenchSessionsPage::currentSession() const
{
    return m_tabs ? qobject_cast<WorkbenchPage *>(m_tabs->currentWidget()) : nullptr;
}

void WorkbenchSessionsPage::closeSession(int index)
{
    if (!m_tabs || index < 0 || index >= m_tabs->count()) {
        return;
    }

    QWidget *page = m_tabs->widget(index);
    if (auto *session = qobject_cast<WorkbenchPage *>(page)) {
        session->saveSettings();
    }
    m_tabs->removeTab(index);
    if (page) {
        page->deleteLater();
    }

    if (m_tabs->count() == 0) {
        addSession(nullptr, false);
    }
}

QString WorkbenchSessionsPage::nextRouteKey() const { return QStringLiteral("session-%1").arg(m_nextSession); }

QString WorkbenchSessionsPage::nextTitle() const { return QStringLiteral("会话 %1").arg(m_nextSession); }
