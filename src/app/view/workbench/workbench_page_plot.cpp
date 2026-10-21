#include "app/view/workbench/workbench_page_internal.h"

#include "app/view/quick_plot_window.h"
#include "app/view/plot_parser_dialog.h"

#include <algorithm>
#include <iterator>

void WorkbenchPage::showQuickPlotWindow()
{
    // Start from the last applied configuration so field filters and binary
    // fields carry over; the protocol itself still has to be chosen.
    PlotParserDialog dialog(QuickPlotWindow::savedParserConfig(), window(), true);
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
    // Windows often share a template, so parse the frame once per template.
    QList<QPair<const AppProtocol::ProtocolTemplate *, AppProtocol::ParseResult>> parsedFrames;
    for (QuickPlotWindow *plotWindow : m_quickPlotWindows) {
        if (!plotWindow->isPlottingActive() || plotWindow->isPaused()) {
            continue;
        }
        const AppProtocol::ProtocolTemplate *protocolTemplate = plotWindow->payloadTemplate();
        if (!protocolTemplate) {
            plotWindow->appendRecord(record.timestamp, record.terminalText, record.bytes);
            continue;
        }
        auto parsed = std::find_if(parsedFrames.cbegin(), parsedFrames.cend(), [protocolTemplate](const auto &entry) {
            return *entry.first == *protocolTemplate;
        });
        if (parsed == parsedFrames.cend()) {
            parsedFrames.append({protocolTemplate, AppProtocol::parseFrame(record.bytes, *protocolTemplate)});
            parsed = std::prev(parsedFrames.cend());
        }
        plotWindow->appendRecord(record.timestamp, record.terminalText, record.bytes, &parsed->second);
    }
}
