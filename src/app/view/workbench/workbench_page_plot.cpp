#include "app/view/workbench/workbench_page_internal.h"

#include "app/view/quick_plot_window.h"
#include "app/view/plot_parser_dialog.h"

void WorkbenchPage::showQuickPlotWindow()
{
    PlotParserDialog dialog(AppPlot::ParserConfig{}, window(), true);
    dialog.setProtocolTemplates(m_protocolTemplates);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (dialog.usesProtocolTemplate()) {
        const auto protocolTemplate = dialog.protocolTemplate();
        createQuickPlotWindow(dialog.parserConfig(), &protocolTemplate);
        return;
    }
    createQuickPlotWindow(dialog.parserConfig());
}

QuickPlotWindow *WorkbenchPage::createQuickPlotWindow(const AppPlot::ParserConfig &config,
                                                      const AppProtocol::ProtocolTemplate *protocolTemplate)
{
    if (config.protocol == AppPlot::Protocol::Binary && config.binarySource == AppPlot::BinarySource::Payload &&
        !protocolTemplate) {
        return nullptr;
    }
    auto *plotWindow = new QuickPlotWindow(this);
    plotWindow->setProtocolTemplates(m_protocolTemplates);
    if (protocolTemplate) {
        plotWindow->setProtocolTemplate(*protocolTemplate);
    }
    if (!plotWindow->configureParser(config)) {
        delete plotWindow;
        return nullptr;
    }
    plotWindow->setAttribute(Qt::WA_DeleteOnClose);
    plotWindow->setObjectName(QStringLiteral("quickPlotWindow-%1").arg(m_nextPlotWindowNumber));
    plotWindow->setWindowTitle(AppI18n::text("曲线 %1").arg(m_nextPlotWindowNumber++));
    m_quickPlotWindows.append(plotWindow);
    m_quickPlotWindow = plotWindow;
    connect(plotWindow, &QObject::destroyed, this, [this, plotWindow]() {
        m_quickPlotWindows.removeAll(plotWindow);
        if (m_quickPlotWindow == plotWindow) {
            m_quickPlotWindow = m_quickPlotWindows.isEmpty() ? nullptr : m_quickPlotWindows.last();
        }
    });
    plotWindow->show();
    plotWindow->raise();
    plotWindow->activateWindow();
    return plotWindow;
}

void WorkbenchPage::appendQuickPlotRecord(const SessionRecord &record)
{
    if (record.direction != RecordDirection::Rx) {
        return;
    }
    for (QuickPlotWindow *plotWindow : m_quickPlotWindows) {
        if (plotWindow->isPlottingActive()) {
            plotWindow->appendRecord(record.timestamp, record.terminalText, record.bytes);
        }
    }
}
