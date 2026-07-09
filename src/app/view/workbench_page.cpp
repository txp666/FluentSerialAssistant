#include "app/view/workbench/workbench_page_internal.h"
#include "app/core/app_i18n.h"
#include "app/core/script_runner.h"

#include <QtCore/QThread>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

WorkbenchPage::WorkbenchPage(QWidget *parent, bool restoreSavedSession, bool allowAutoOpen)
    : AppPage(AppI18n::text("终端"), AppI18n::text("连接串口设备，查看收发记录，并执行文本或 HEX 数据发送。"), parent,
              false)
{
    contentLayout()->setAlignment(Qt::Alignment());
    contentLayout()->addWidget(createWorkbench(), 1);

    setupSerialSignals();

    connect(&m_flushTimer, &QTimer::timeout, this, &WorkbenchPage::flushPendingLines);
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
    connect(ThemeManager::instance(), &ThemeManager::effectiveThemeChanged, this, [this]() { renderTerminal(); });
    connect(FluentConfig::instance(), &FluentConfig::localeNameChanged, this, [this]() {
        updateConnectionUi(m_serial.isOpen());
        updateCounters();
        updateHistoryCombo();
        updateReceiveModeButton();
        updateSendModeButton();
        updatePacketTable(m_packetList ? m_packetList->currentRow() : -1);
        updateMacroTable(m_macroList ? m_macroList->currentRow() : -1);
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
    if (restoreSavedSession) {
        loadSendHistory();
        loadSendPackets();
        loadMacroSteps();
        loadAutoReplyRules();
        restoreSettings();
    } else {
        updateFrameControlState();
        updateModbusUi();
        updatePacketTable();
        updateMacroTable();
        updateScriptActionState();
        updateAutoReplyTable();
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
        QTimer::singleShot(250, this, &WorkbenchPage::onConnectClicked);
    }
}
WorkbenchPage::~WorkbenchPage()
{
    if (m_scriptRunner) {
        m_scriptRunner->requestStop();
    }
    if (m_scriptThread) {
        m_scriptThread->quit();
        if (!m_scriptThread->wait(2000)) {
            m_scriptThread->terminate();
            m_scriptThread->wait();
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

bool WorkbenchPage::eventFilter(QObject *watched, QEvent *event)
{
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
