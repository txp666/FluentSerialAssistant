#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

WorkbenchPage::WorkbenchPage(QWidget *parent)
    : AppPage(QStringLiteral("终端"), QStringLiteral("连接串口设备，查看收发记录，并执行文本或 HEX 数据发送。"), parent,
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

    refreshPorts();
    loadSendHistory();
    loadSendPackets();
    loadMacroSteps();
    restoreSettings();
    updateConnectionUi(false);
    updateCounters();
    updateRateStats();
    updateHistoryCombo();

    if (m_autoOpenCheck && m_autoOpenCheck->isChecked() && !currentSerialConfig().portName.isEmpty()) {
        QTimer::singleShot(250, this, &WorkbenchPage::onConnectClicked);
    }
}
WorkbenchPage::~WorkbenchPage()
{
    m_fileSendTimer.stop();
    if (m_fileSendFile.isOpen()) {
        m_fileSendFile.close();
    }
    m_reconnectTimer.stop();
    m_macroTimer.stop();
    closeReceiveCapture();
    m_serial.closePort();
}

bool WorkbenchPage::eventFilter(QObject *watched, QEvent *event)
{
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
