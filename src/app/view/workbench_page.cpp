#include "app/core/app_i18n.h"
#include "app/core/script_runner.h"
#include "app/view/quick_plot_window.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <QtCore/QPointer>
#include <QtCore/QThread>
#include <QtGui/QHideEvent>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

QList<WorkbenchPage *> &openWorkbenchPages()
{
    static QList<WorkbenchPage *> pages;
    return pages;
}

} // namespace

WorkbenchPage::WorkbenchPage(QWidget *parent, bool restoreSavedSession, bool allowAutoOpen)
    : AppPage(AppI18n::text("终端"), AppI18n::text("连接串口设备，查看收发记录，并执行文本或 HEX 数据发送。"), parent,
              false)
{
    contentLayout()->setAlignment(Qt::Alignment());
    contentLayout()->addWidget(createWorkbench(), 1);
    reloadTerminalColors();

    setupSerialSignals();

    connect(&m_flushTimer, &QTimer::timeout, this, [this]() {
        flushPendingLines();
        if (m_countersDirty) {
            updateCounters();
        }
    });
    m_flushTimer.start(FlushIntervalMs);
    connect(&m_loopTimer, &QTimer::timeout, this, &WorkbenchPage::sendCurrentPayload);
    m_reconnectTimer.setInterval(ReconnectIntervalMs);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &WorkbenchPage::attemptReconnect);
    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &WorkbenchPage::updateRateStats);
    m_fileSendTimer.setSingleShot(true);
    connect(&m_fileSendTimer, &QTimer::timeout, this, &WorkbenchPage::sendNextFileChunk);
    m_macroTimer.setSingleShot(true);
    connect(&m_macroTimer, &QTimer::timeout, this, &WorkbenchPage::handleMacroTimer);
    m_rxFrameIdleTimer.setSingleShot(true);
    connect(&m_rxFrameIdleTimer, &QTimer::timeout, this, &WorkbenchPage::flushRxFrameBuffer);
    connect(ThemeManager::instance(), &ThemeManager::effectiveThemeChanged, this, [this]() { renderTerminal(); });
    connect(FluentConfig::instance(), &FluentConfig::localeNameChanged, this, [this]() {
        updateConnectionUi(m_serial.isOpen());
        updateCounters();
        updateHistoryCombo();
        updateReceiveModeButton();
        updateSendModeButton();
        updatePacketTable(m_packetList ? m_packetList->currentRow() : -1);
        updateMacroTable(m_macroList ? m_macroList->currentRow() : -1);
        updateProtocolTemplateCombo(m_protocolTemplateCombo ? m_protocolTemplateCombo->currentIndex() : -1);
        updateProtocolTemplateActionState();
        updateScriptActionState();
        updateAutoReplyTable(m_autoReplyList ? m_autoReplyList->currentRow() : -1);
        updateModbusUi();
        updateFrameControlState();
        updateAutoLogStatus();
        updateFileSendUi(m_fileSendFile.isOpen());
        updateFileProgress();
        renderTerminal();
    });

    refreshPorts();
    // The lists are shared by all sessions. A blank session that skipped them
    // would erase them the next time it saves its settings.
    loadSendHistory();
    loadSendPackets();
    loadMacroSteps();
    loadProtocolTemplates();
    loadAutoReplyRules();
    applyAutoReplyRule(m_autoReplyList->currentRow());
    if (restoreSavedSession) {
        restoreSettings();
    } else {
        updateFrameControlState();
        updateModbusUi();
        updateProtocolTemplateActionState();
        updateScriptActionState();
        updatePacketActionState();
        updateMacroActionState();
        updateAutoReplyActionState();
        updateFileSendUi(false);
        updateFileProgress();
        updateAutoLogStatus();
    }
    updateConnectionUi(false);
    updateCounters();
    updateRateStats();
    updateHistoryCombo();

    if (allowAutoOpen && m_autoOpenCheck && m_autoOpenCheck->isChecked() && !currentSerialConfig().portName.isEmpty()) {
        const QString autoOpenPort = currentSerialConfig().portName;
        QTimer::singleShot(250, this, [this, autoOpenPort]() {
            if (!m_serial.isOpen() && currentSerialConfig().portName == autoOpenPort) {
                onConnectClicked();
            }
        });
    }
    openWorkbenchPages().append(this);
}
WorkbenchPage::~WorkbenchPage()
{
    openWorkbenchPages().removeAll(this);
    const auto plotWindows = m_quickPlotWindows;
    m_quickPlotWindows.clear();
    m_quickPlotWindow = nullptr;
    for (QuickPlotWindow *plotWindow : plotWindows) {
        disconnect(plotWindow, nullptr, this, nullptr);
        delete plotWindow;
    }
    if (m_scriptRunner) {
        m_scriptRunner->requestStop();
    }
    if (m_scriptThread) {
        // Stopping interrupts the engine and releases any request waiting on
        // this page, so the script thread finishes on its own.
        m_scriptThread->quit();
        if (!m_scriptThread->wait(5000)) {
            // Never terminate a thread inside the JS engine. Let it finish and
            // delete itself; its callbacks no longer reach this page.
            qWarning("Script thread did not stop before the session closed");
            m_scriptThread->setParent(nullptr);
        }
    }
    m_fileSendTimer.stop();
    if (m_fileSendFile.isOpen()) {
        m_fileSendFile.close();
    }
    m_reconnectTimer.stop();
    m_macroTimer.stop();
    closeAutoLog();
    closeReceiveCapture();
    m_serial.closePort();
}

void WorkbenchPage::reloadTerminalColors()
{
    applyTerminalColorConfig(AppTerminal::loadColorConfig());
}

void WorkbenchPage::publishSharedList(SharedList list) const
{
    for (WorkbenchPage *page : openWorkbenchPages()) {
        if (page != this) {
            page->reloadSharedList(list);
        }
    }
}

void WorkbenchPage::reloadSharedList(SharedList list)
{
    // The editors keep their contents. A selection survives only if the same
    // item is still listed, so a later save cannot overwrite a different item.
    switch (list) {
    case SharedList::SendHistory:
        loadSendHistory();
        updateHistoryCombo();
        break;
    case SharedList::SendPackets: {
        const int row = m_packetList ? m_packetList->currentRow() : -1;
        const SendPacket selected = row >= 0 && row < m_sendPackets.size() ? m_sendPackets.at(row) : SendPacket();
        loadSendPackets();
        for (int index = 0; row >= 0 && index < m_sendPackets.size(); ++index) {
            if (m_sendPackets.at(index).group == selected.group && m_sendPackets.at(index).name == selected.name) {
                updatePacketTable(index);
                break;
            }
        }
        break;
    }
    case SharedList::MacroSteps: {
        if (m_macroRunning) {
            // The running sequence indexes these steps; reload once it ends.
            m_macroStepsReloadPending = true;
            break;
        }
        const int row = m_macroList ? m_macroList->currentRow() : -1;
        const MacroStep selected = row >= 0 && row < m_macroSteps.size() ? m_macroSteps.at(row) : MacroStep();
        loadMacroSteps();
        int selectedRow = -1;
        for (int index = 0; row >= 0 && index < m_macroSteps.size(); ++index) {
            const MacroStep &step = m_macroSteps.at(index);
            if (step.name == selected.name && step.mode == selected.mode && step.payload == selected.payload &&
                step.lineEnding == selected.lineEnding && step.responseMode == selected.responseMode &&
                step.expectedResponse == selected.expectedResponse && step.timeoutMs == selected.timeoutMs &&
                step.delayMs == selected.delayMs) {
                selectedRow = index;
                break;
            }
        }
        updateMacroTable(selectedRow);
        if (selectedRow < 0 && m_macroList) {
            const QSignalBlocker blocker(m_macroList);
            m_macroList->setCurrentRow(-1);
            updateMacroActionState();
        }
        break;
    }
    case SharedList::AutoReplyRules: {
        const int row = m_autoReplyList ? m_autoReplyList->currentRow() : -1;
        const AutoReplyRule selected =
            row >= 0 && row < m_autoReplyRules.size() ? m_autoReplyRules.at(row) : AutoReplyRule();
        loadAutoReplyRules();
        int selectedRow = -1;
        for (int index = 0; row >= 0 && index < m_autoReplyRules.size(); ++index) {
            const AutoReplyRule &rule = m_autoReplyRules.at(index);
            if (rule.name == selected.name && rule.enabled == selected.enabled &&
                rule.matchMode == selected.matchMode && rule.pattern == selected.pattern &&
                rule.responseMode == selected.responseMode && rule.responsePayload == selected.responsePayload &&
                rule.lineEnding == selected.lineEnding && rule.delayMs == selected.delayMs) {
                selectedRow = index;
                break;
            }
        }
        updateAutoReplyTable(selectedRow);
        break;
    }
    case SharedList::ProtocolTemplates:
        loadProtocolTemplates(false);
        break;
    }
}

void WorkbenchPage::hideEvent(QHideEvent *event)
{
    if (m_terminalSearchBar) {
        m_terminalSearchBar->hide();
    }
    AppPage::hideEvent(event);
}

bool WorkbenchPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_terminalStatsWidget && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange)) {
        updateTerminalHeaderLayout();
    }
    if ((watched == m_terminalView || watched == m_terminalSearchEdit) && event->type() == QEvent::KeyPress &&
        m_terminalSearchBar && m_terminalSearchBar->isVisible() &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        hideTerminalSearchBar();
        return true;
    }
    if (m_terminalView && watched == m_terminalView->viewport() && event->type() == QEvent::Resize) {
        positionTerminalSearchBar();
        if (m_autoScrollCheck && m_autoScrollCheck->isChecked() &&
            !m_terminalView->property("terminalResizeScrollQueued").toBool()) {
            m_terminalView->setProperty("terminalResizeScrollQueued", true);
            const QPointer<TextBrowser> terminalView(m_terminalView);
            QTimer::singleShot(0, this, [this, terminalView]() {
                if (!terminalView) {
                    return;
                }
                terminalView->setProperty("terminalResizeScrollQueued", false);
                if (m_autoScrollCheck && m_autoScrollCheck->isChecked()) {
                    // Let the editor reflow first, then keep the latest text visible.
                    auto *scrollBar = terminalView->verticalScrollBar();
                    scrollBar->setValue(scrollBar->maximum());
                }
            });
        }
    }
    if (watched == m_terminalSearchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            moveTerminalSearchMatch(keyEvent->modifiers().testFlag(Qt::ShiftModifier) ? -1 : 1);
            return true;
        }
    }
    if (event->type() == QEvent::Wheel) {
        if (forwardSidePanelWheelEvent(watched, static_cast<QWheelEvent *>(event))) {
            return true;
        }
    }

    if (watched == m_sendEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const bool enterKey = keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter;
        if (enterKey && keyEvent->modifiers().testFlag(Qt::ControlModifier)) {
            sendCurrentPayload();
            return true;
        }
    }
    return AppPage::eventFilter(watched, event);
}

bool WorkbenchPage::forwardSidePanelWheelEvent(QObject *watched, QWheelEvent *event)
{
    if (!m_sideScroll || !m_sidePanel || !event) {
        return false;
    }

    auto *widget = qobject_cast<QWidget *>(watched);
    if (!widget || (widget != m_sidePanel && !m_sidePanel->isAncestorOf(widget))) {
        return false;
    }
    if (widget == m_sideScroll->viewport()) {
        return false;
    }

    auto *viewport = m_sideScroll->viewport();
    if (!viewport) {
        return false;
    }

    const QPointF viewportPosition = viewport->mapFromGlobal(event->globalPosition().toPoint());
    QWheelEvent forwarded(viewportPosition, event->globalPosition(), event->pixelDelta(), event->angleDelta(),
                          event->buttons(), event->modifiers(), event->phase(), event->inverted(), event->source(),
                          event->pointingDevice());
    QCoreApplication::sendEvent(viewport, &forwarded);
    event->setAccepted(forwarded.isAccepted());
    return forwarded.isAccepted();
}
