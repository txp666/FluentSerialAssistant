#include "app/view/workbench/workbench_page_internal.h"

#include "app/view/quick_plot_window.h"

void WorkbenchPage::showQuickPlotWindow()
{
    if (!m_quickPlotWindow) {
        m_quickPlotWindow = new QuickPlotWindow(this);
        connect(m_quickPlotWindow, &QuickPlotWindow::protocolChanged, this, [this]() {
            for (const SessionRecord &record : m_records) {
                appendQuickPlotRecord(record, true);
            }
        });
        for (const SessionRecord &record : m_records) {
            appendQuickPlotRecord(record, true);
        }
    }

    m_quickPlotWindow->show();
    m_quickPlotWindow->raise();
    m_quickPlotWindow->activateWindow();
}

void WorkbenchPage::appendQuickPlotRecord(const SessionRecord &record, bool ignorePause)
{
    if (!m_quickPlotWindow || record.direction != RecordDirection::Rx) {
        return;
    }

    m_quickPlotWindow->appendText(record.timestamp, record.displayText, ignorePause);
}
