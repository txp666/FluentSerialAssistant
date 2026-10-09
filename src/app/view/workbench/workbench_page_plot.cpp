#include "app/view/workbench/workbench_page_internal.h"

#include "app/view/quick_plot_window.h"

void WorkbenchPage::showQuickPlotWindow()
{
    if (!m_quickPlotWindow) {
        m_quickPlotWindow = new QuickPlotWindow(this);
    }

    if (m_quickPlotWindow->isMinimized()) {
        m_quickPlotWindow->showNormal();
    } else {
        m_quickPlotWindow->show();
    }
    m_quickPlotWindow->raise();
    m_quickPlotWindow->activateWindow();
}

void WorkbenchPage::appendQuickPlotRecord(const SessionRecord &record)
{
    if (!m_quickPlotWindow || !m_quickPlotWindow->isPlottingActive() || record.direction != RecordDirection::Rx) {
        return;
    }

    QByteArray payload;
    if (m_quickPlotWindow->requiresProtocolPayload() && m_protocolEnabledCheck && m_protocolEnabledCheck->isChecked() &&
        m_protocolTemplateCombo && m_protocolTemplateCombo->currentIndex() >= 0) {
        const AppProtocol::ParseResult result = AppProtocol::parseFrame(record.bytes, currentProtocolTemplateFromUi());
        if (result.ok) {
            payload = result.payload;
        }
    }
    m_quickPlotWindow->appendRecord(record.timestamp, record.terminalText, record.bytes, payload);
}
