#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createWorkbench()
{
    auto *workbench = new QWidget(this);
    auto *root = new QHBoxLayout(workbench);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *sideScroll = new ScrollArea(workbench);
    sideScroll->setFixedWidth(SidePanelWidth);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (sideScroll->horizontalFluentScrollBar()) {
        sideScroll->horizontalFluentScrollBar()->setForceHidden(true);
    }

    auto *sidePanel = new QWidget(sideScroll);
    auto *sideLayout = new QVBoxLayout(sidePanel);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(10);
    sideLayout->addWidget(createConnectionSection());
    sideLayout->addWidget(createReceiveSettingsSection());
    sideLayout->addWidget(createSendSettingsSection());
    sideLayout->addWidget(createModbusSection());
    sideLayout->addWidget(createPacketSection());
    sideLayout->addWidget(createMacroSection());
    sideLayout->addWidget(createFileSendSection());
    sideLayout->addStretch(1);
    sideScroll->setWidget(sidePanel);
    sideScroll->setWidgetResizable(true);

    auto *terminalPanel = new QWidget(workbench);
    auto *terminalLayout = new QVBoxLayout(terminalPanel);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(10);
    terminalLayout->addWidget(createTerminalSection(), 1);
    terminalLayout->addWidget(createSendSection());

    root->addWidget(sideScroll);
    root->addWidget(terminalPanel, 1);
    return workbench;
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
