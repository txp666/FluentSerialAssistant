#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createWorkbench()
{
    auto *workbench = new QWidget(this);
    auto *root = new QHBoxLayout(workbench);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    m_sideScroll = new ScrollArea(workbench);
    m_sideScroll->setFixedWidth(SidePanelWidth);
    m_sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (m_sideScroll->horizontalFluentScrollBar()) {
        m_sideScroll->horizontalFluentScrollBar()->setForceHidden(true);
    }

    m_sidePanel = new QWidget(m_sideScroll);
    auto *sideLayout = new QVBoxLayout(m_sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);
    sideLayout->addWidget(createConnectionSection());
    sideLayout->addWidget(createReceiveSettingsSection());
    sideLayout->addWidget(createSendSettingsSection());
    sideLayout->addWidget(createModbusSection());
    sideLayout->addWidget(createPacketSection());
    sideLayout->addWidget(createMacroSection());
    sideLayout->addWidget(createScriptSection());
    sideLayout->addWidget(createAutoReplySection());
    sideLayout->addWidget(createFileSendSection());
    sideLayout->addStretch(1);
    m_sideScroll->setWidget(m_sidePanel);
    m_sideScroll->setWidgetResizable(true);
    installSidePanelWheelFilters(m_sidePanel);

    auto *terminalPanel = new QWidget(workbench);
    auto *terminalLayout = new QVBoxLayout(terminalPanel);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(10);
    terminalLayout->addWidget(createTerminalSection(), 1);
    terminalLayout->addWidget(createSendSection());

    root->addWidget(m_sideScroll);
    root->addWidget(terminalPanel, 1);
    return workbench;
}

void WorkbenchPage::installSidePanelWheelFilters(QWidget *root)
{
    if (!root) {
        return;
    }

    root->installEventFilter(this);
    const auto widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        widget->installEventFilter(this);
    }
}

QWidget *WorkbenchPage::createCheckRow(const QStringList &labels, const QList<CheckBox **> &targets, QWidget *parent)
{
    auto *rowWidget = new QWidget(parent);
    auto *row = new QHBoxLayout(rowWidget);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    const int count = qMin(labels.size(), targets.size());
    for (int i = 0; i < count; ++i) {
        auto *check = new CheckBox(labels.at(i), rowWidget);
        check->setMinimumWidth(0);
        check->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        *targets.at(i) = check;
        row->addWidget(check);
    }
    row->addStretch(1);
    return rowWidget;
}
