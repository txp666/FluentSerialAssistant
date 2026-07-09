#pragma once

#include "app/core/checksum_utils.h"
#include "app/core/modbus_utils.h"
#include "app/core/protocol_template.h"
#include "app/serial/serial_controller.h"
#include "app/view/app_page.h"

#include <QtCore/QDateTime>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFile>
#include <QtCore/QRegularExpression>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVector>
#include <QtGui/QColor>

class QEvent;
class QTextCharFormat;
class QTextCursor;
class QThread;
class QWheelEvent;
class DataTableWindow;
class ScriptRunner;
struct DataTableRecord;
class QuickPlotWindow;

class WorkbenchPage : public AppPage
{
    Q_OBJECT

  public:
    explicit WorkbenchPage(QWidget *parent = nullptr, bool restoreSavedSession = true, bool allowAutoOpen = true);
    ~WorkbenchPage() override;

    void saveSettings() const;
    void copySessionConfigFrom(const WorkbenchPage &source);

  public slots:
    void setTerminalFontFamily(const QString &family);

  signals:
    void settingsRequested();

  private:
    bool eventFilter(QObject *watched, QEvent *event) override;

    QWidget *createWorkbench();
    QWidget *createConnectionSection();
    QWidget *createReceiveSettingsSection();
    QWidget *createProtocolTemplateSection();
    QWidget *createSendSettingsSection();
    QWidget *createModbusSection();
    QWidget *createPacketSection();
    QWidget *createMacroSection();
    QWidget *createScriptSection();
    QWidget *createAutoReplySection();
    QWidget *createFileSendSection();
    QWidget *createTerminalSection();
    QWidget *createSendSection();
    QWidget *createCheckRow(const QStringList &labels, const QList<FluentQt::CheckBox **> &targets, QWidget *parent);
    void installSidePanelWheelFilters(QWidget *root);
    bool forwardSidePanelWheelEvent(QObject *watched, QWheelEvent *event);

    enum class RecordDirection
    {
        Rx,
        Tx,
        FrameBreak
    };

    enum class ExportFormat
    {
        Txt,
        Csv,
        Bin
    };

    struct SessionRecord
    {
        QDateTime timestamp;
        RecordDirection direction = RecordDirection::Rx;
        QByteArray bytes;
        QString terminalText;
        QString displayText;
        QString sourceLabel;
    };

    struct SendHistoryItem
    {
        QString mode;
        QString payload;
        QString lineEnding;
    };

    struct SendPacket
    {
        QString group;
        QString name;
        QString note;
        QString mode;
        QString payload;
        QString lineEnding;
        bool enabled = true;
    };

    struct MacroStep
    {
        QString name;
        QString mode;
        QString payload;
        QString lineEnding;
        QString responseMode;
        QString expectedResponse;
        int timeoutMs = 1000;
        int delayMs = 0;
    };

    struct MacroRunResult
    {
        QDateTime timestamp;
        int loop = 0;
        int step = 0;
        QString stepName;
        QString status;
        QString message;
        QByteArray txBytes;
        QByteArray rxBytes;
        int elapsedMs = 0;
    };

    struct AutoReplyRule
    {
        QString name;
        bool enabled = true;
        QString matchMode;
        QString pattern;
        QString responseMode;
        QString responsePayload;
        QString lineEnding;
        int delayMs = 0;
    };

    struct SearchMatchRange
    {
        int start = 0;
        int length = 0;
    };

    struct TerminalSearchQuery
    {
        QString text;
        Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
        bool regexEnabled = false;
        bool valid = true;
        QString errorMessage;
        QRegularExpression regex;
    };

    struct TerminalSearchMatch
    {
        int position = 0;
        int length = 0;
    };

    void setupSerialSignals();
    void refreshPorts();
    void restoreSettings();
    void applyTerminalFont(const QString &family = QString());
    void updateConnectionUi(bool connected);
    void updateCounters();
    void updateRateStats();
    void updateReceiveModeButton();
    void updateSendModeButton();
    void updateHistoryCombo();
    void applyHistoryItem(int index);
    void updatePacketTable(int selectedRow = -1);
    void updatePacketActionState();
    QList<int> selectedPacketRows() const;
    void applyPacket(int row);
    void saveCurrentPacket();
    void removeSelectedPacket();
    void moveSelectedPacket(int direction);
    void sendSelectedPacket();
    void sendSelectedPackets();
    void sendPacket(int row);
    bool sendPacketPayload(const SendPacket &packet);
    void importSendPackets();
    void exportSendPackets();
    void showInfo(const QString &title, const QString &message);
    void showSuccess(const QString &title, const QString &message);
    void showWarning(const QString &title, const QString &message);
    void showError(const QString &title, const QString &message);

    SerialPortConfig currentSerialConfig() const;
    QByteArray currentPayload(bool *ok = nullptr);
    QByteArray payloadFromText(const QString &payloadText, const QString &mode, const QString &lineEndingKey,
                               bool focusEditorOnError, bool *ok = nullptr);
    QByteArray selectedLineEnding() const;
    QByteArray lineEndingForKey(const QString &key) const;
    QString currentDisplayMode() const;
    QString selectedLineEndingKey() const;
    QString receiveEncodingKey() const;
    QString sendEncodingKey() const;
    QString frameBreakModeKey() const;
    QByteArray frameBoundaryPattern(bool *ok = nullptr) const;
    QString checksumAlgorithmKey() const;
    AppChecksum::ByteOrder checksumByteOrder() const;
    QString terminalSearchText() const;
    QString terminalDirectionFilter() const;
    bool terminalSearchCaseSensitive() const;
    bool terminalSearchRegexEnabled() const;
    TerminalSearchQuery terminalSearchQuery() const;
    QList<SearchMatchRange> terminalSearchRanges(const QString &text, const TerminalSearchQuery &query) const;
    QString directionText(RecordDirection direction) const;
    QString historyLabel(const SendHistoryItem &item) const;
    QString exportSuffix(ExportFormat format) const;
    QString autoLogFormatKey() const;
    ExportFormat autoLogFormat() const;
    qint64 autoLogMaxFileBytes() const;
    QString formatRecordLine(const SessionRecord &record) const;
    QColor selectedTxColor() const;
    QByteArray payloadWithOptionalChecksum(const QByteArray &payload, bool *ok = nullptr);
    void calculateChecksumForCurrentPayload();
    void setChecksumResultText(const QString &text);
    AppModbus::RequestConfig currentModbusConfig() const;
    QByteArray currentModbusFrame(bool *ok = nullptr);
    void fillModbusRequest();
    void sendModbusRequest();
    void updateModbusUi();
    void updateModbusResponseStatus(const QByteArray &data);
    void setModbusStatusText(const QString &text);
    bool recordMatchesTerminalFilter(const SessionRecord &record) const;
    void handleReceivedData(const QByteArray &data);
    void recordReceivedBytes(const QByteArray &data);
    void processBufferedFrameData(const QByteArray &data);
    void flushRxFrameBuffer();
    void updateFrameControlState();
    void loadProtocolTemplates();
    void saveProtocolTemplates() const;
    void updateProtocolTemplateCombo(int selectedIndex = -1);
    void updateProtocolTemplateUi();
    void updateProtocolTemplateActionState();
    AppProtocol::ProtocolTemplate currentProtocolTemplateFromUi() const;
    void applyProtocolTemplate(int index);
    void saveCurrentProtocolTemplate();
    void deleteCurrentProtocolTemplate();
    void insertProtocolTemplateExample();
    QString protocolParseSourceLabel(const QByteArray &data);
    void appendRecord(RecordDirection direction, const QByteArray &data, bool updateStats = true,
                      const QString &sourceLabel = QString());
    void trimRecords();
    void renderTerminal();
    void insertTextWithSearchHighlights(QTextCursor &cursor, const QString &line, int start, int length,
                                        const QTextCharFormat &format, const QList<SearchMatchRange> &ranges);
    bool appendRecordToTerminal(QTextCursor &cursor, const SessionRecord &record, bool hasPrevious,
                                const TerminalSearchQuery &query);
    void resetTerminalSearchNavigation();
    void moveTerminalSearchMatch(int direction);
    void selectTerminalSearchMatch();
    void flushPendingLines();
    void sendCurrentPayload();
    void addSendHistory(const SendHistoryItem &item);
    void loadSendHistory();
    void saveSendHistory() const;
    void loadSendPackets();
    void saveSendPackets() const;
    void updateMacroTable(int selectedRow = -1);
    void updateMacroActionState();
    void applyMacroStep(int row);
    void saveCurrentMacroStep();
    void removeSelectedMacroStep();
    void moveSelectedMacroStep(int direction);
    void startMacroSequence();
    void stopMacroSequence(const QString &message = QString(), bool userRequested = true);
    void runMacroCurrentStep();
    void handleMacroTimer();
    void handleMacroReceivedData(const QByteArray &data);
    void completeMacroStep(bool passed, const QString &message, const QByteArray &rxBytes = QByteArray());
    void finishMacroSequence(const QString &message, bool passed);
    QByteArray expectedMacroResponseBytes(const MacroStep &step, bool *ok = nullptr, QString *error = nullptr) const;
    bool macroResponseMatches(const MacroStep &step, const QByteArray &buffer) const;
    void loadMacroSteps();
    void saveMacroSteps() const;
    void exportMacroResults();
    void updateScriptActionState();
    void loadScriptFile();
    void saveScriptFile();
    void insertScriptExample();
    void startScript();
    void stopScript();
    void appendScriptLog(const QString &message);
    void clearScriptLog();
    void finishScriptRun(bool ok, const QString &message);
    bool sendScriptPayload(const QString &payloadText, const QString &mode, const QString &lineEndingKey,
                           QString *error);
    QString scriptReceivedTextSnapshot() const;
    QString scriptReceivedHexSnapshot() const;
    QString scriptLastRxTextSnapshot() const;
    QString scriptLastRxHexSnapshot() const;
    QVariantList scriptRecordSnapshot() const;
    void updateAutoReplyTable(int selectedRow = -1);
    void updateAutoReplyActionState();
    void applyAutoReplyRule(int row);
    void saveCurrentAutoReplyRule();
    void removeSelectedAutoReplyRule();
    void moveSelectedAutoReplyRule(int direction);
    void handleAutoReplyReceivedData(const QByteArray &data);
    bool autoReplyRuleMatches(const AutoReplyRule &rule, const QByteArray &buffer) const;
    void sendAutoReplyRule(const AutoReplyRule &rule);
    void loadAutoReplyRules();
    void saveAutoReplyRules() const;
    void exportRecords(ExportFormat format);
    void updateReceiveCapture(bool enabled);
    void closeReceiveCapture();
    void updateAutoLog(bool enabled);
    void resetAutoLogSession();
    void closeAutoLog();
    void writeAutoLogRecord(const SessionRecord &record);
    bool ensureAutoLogFile(ExportFormat format, qint64 nextBytes);
    QByteArray serializeLogRecord(const SessionRecord &record, ExportFormat format) const;
    void updateAutoLogStatus();
    void showQuickPlotWindow();
    void appendQuickPlotRecord(const SessionRecord &record, bool ignorePause = false);
    void showDataTableWindow();
    void refreshDataTableWindow();
    QVector<DataTableRecord> dataTableRecords() const;
    DataTableRecord dataTableRecord(int recordIndex, const SessionRecord &record) const;
    void locateRecordInTerminal(int recordIndex);
    void browseSendFile();
    void startFileSend();
    void cancelFileSend();
    void sendNextFileChunk();
    void updateFileSendUi(bool sending);
    void updateFileProgress();
    void scheduleReconnect();
    void attemptReconnect();
    void onConnectClicked();
    void onLoopChanged(bool checked);
    void setControlsEnabledForConnection(bool connected);
    int maxRecordCount() const;

    SerialController m_serial;
    QList<SerialPortDescriptor> m_ports;
    QList<SessionRecord> m_records;
    QList<int> m_pendingRecordIndexes;
    QList<SendHistoryItem> m_sendHistory;
    QList<SendPacket> m_sendPackets;
    QList<MacroStep> m_macroSteps;
    QList<MacroRunResult> m_macroResults;
    QList<AutoReplyRule> m_autoReplyRules;
    QList<AppProtocol::ProtocolTemplate> m_protocolTemplates;
    QList<TerminalSearchMatch> m_terminalSearchMatches;
    SerialPortConfig m_lastConfig;
    QByteArray m_rxFrameBuffer;
    QByteArray m_autoReplyBuffer;
    qint64 m_rxCount = 0;
    qint64 m_txCount = 0;
    qint64 m_lastStatsRxCount = 0;
    qint64 m_lastStatsTxCount = 0;
    qint64 m_fileSendTotal = 0;
    qint64 m_fileSendSent = 0;
    int m_terminalStartRecord = 0;
    int m_terminalCurrentSearchMatch = -1;
    QDateTime m_lastRxTimestamp;
    QDateTime m_connectionStartedAt;
    QTimer m_flushTimer;
    QTimer m_loopTimer;
    QTimer m_reconnectTimer;
    QTimer m_statsTimer;
    QTimer m_fileSendTimer;
    QTimer m_macroTimer;
    QThread *m_scriptThread = nullptr;
    ScriptRunner *m_scriptRunner = nullptr;
    QFile m_receiveCaptureFile;
    QFile m_autoLogFile;
    QFile m_fileSendFile;
    QByteArray m_macroWaitBuffer;
    QString m_autoLogSessionStamp;
    QElapsedTimer m_macroStepClock;
    int m_macroLoopTotal = 1;
    int m_macroCurrentLoop = 0;
    int m_macroCurrentStep = 0;
    int m_macroActiveResult = -1;
    int m_autoLogFileIndex = 1;
    qint64 m_autoLogCurrentSize = 0;
    bool m_manualDisconnect = false;
    bool m_macroRunning = false;
    bool m_macroWaitingForResponse = false;
    bool m_scriptRunning = false;
    bool m_updatingProtocolTemplateUi = false;

    FluentQt::ScrollArea *m_sideScroll = nullptr;
    QWidget *m_sidePanel = nullptr;
    FluentQt::ComboBox *m_portCombo = nullptr;
    FluentQt::EditableComboBox *m_baudCombo = nullptr;
    FluentQt::ComboBox *m_dataBitsCombo = nullptr;
    FluentQt::ComboBox *m_parityCombo = nullptr;
    FluentQt::ComboBox *m_stopBitsCombo = nullptr;
    FluentQt::ComboBox *m_flowControlCombo = nullptr;
    FluentQt::SegmentedWidget *m_displayModeSegment = nullptr;
    FluentQt::SearchLineEdit *m_terminalSearchEdit = nullptr;
    FluentQt::ComboBox *m_terminalFilterCombo = nullptr;
    FluentQt::ComboBox *m_receiveEncodingCombo = nullptr;
    FluentQt::ComboBox *m_frameModeCombo = nullptr;
    FluentQt::ComboBox *m_protocolTemplateCombo = nullptr;
    FluentQt::ComboBox *m_protocolLengthSizeCombo = nullptr;
    FluentQt::ComboBox *m_protocolLengthModeCombo = nullptr;
    FluentQt::ComboBox *m_protocolLengthByteOrderCombo = nullptr;
    FluentQt::ComboBox *m_protocolChecksumAlgorithmCombo = nullptr;
    FluentQt::ComboBox *m_protocolChecksumByteOrderCombo = nullptr;
    FluentQt::ComboBox *m_lineEndingCombo = nullptr;
    FluentQt::ComboBox *m_sendEncodingCombo = nullptr;
    FluentQt::ComboBox *m_checksumAlgorithmCombo = nullptr;
    FluentQt::ComboBox *m_checksumByteOrderCombo = nullptr;
    FluentQt::ComboBox *m_modbusFunctionCombo = nullptr;
    FluentQt::ComboBox *m_autoLogFormatCombo = nullptr;
    FluentQt::ComboBox *m_historyCombo = nullptr;
    FluentQt::LineEdit *m_packetNameEdit = nullptr;
    FluentQt::LineEdit *m_packetGroupEdit = nullptr;
    FluentQt::LineEdit *m_packetNoteEdit = nullptr;
    FluentQt::LineEdit *m_macroNameEdit = nullptr;
    FluentQt::LineEdit *m_macroExpectedEdit = nullptr;
    FluentQt::LineEdit *m_autoReplyNameEdit = nullptr;
    FluentQt::LineEdit *m_autoReplyPatternEdit = nullptr;
    FluentQt::LineEdit *m_framePatternEdit = nullptr;
    FluentQt::LineEdit *m_protocolNameEdit = nullptr;
    FluentQt::LineEdit *m_protocolHeaderEdit = nullptr;
    FluentQt::PlainTextEdit *m_modbusValuesEdit = nullptr;
    FluentQt::PlainTextEdit *m_packetPayloadEdit = nullptr;
    FluentQt::PlainTextEdit *m_macroPayloadEdit = nullptr;
    FluentQt::PlainTextEdit *m_scriptEdit = nullptr;
    FluentQt::PlainTextEdit *m_scriptLogEdit = nullptr;
    FluentQt::PlainTextEdit *m_autoReplyPayloadEdit = nullptr;
    FluentQt::ComboBox *m_packetModeCombo = nullptr;
    FluentQt::ComboBox *m_packetLineEndingCombo = nullptr;
    FluentQt::ComboBox *m_macroModeCombo = nullptr;
    FluentQt::ComboBox *m_macroLineEndingCombo = nullptr;
    FluentQt::ComboBox *m_macroResponseModeCombo = nullptr;
    FluentQt::ComboBox *m_autoReplyMatchModeCombo = nullptr;
    FluentQt::ComboBox *m_autoReplyResponseModeCombo = nullptr;
    FluentQt::ComboBox *m_autoReplyLineEndingCombo = nullptr;
    FluentQt::ListWidget *m_packetList = nullptr;
    FluentQt::ListWidget *m_macroList = nullptr;
    FluentQt::ListWidget *m_autoReplyList = nullptr;
    FluentQt::PlainTextEdit *m_sendEdit = nullptr;
    FluentQt::TextBrowser *m_terminalView = nullptr;
    DataTableWindow *m_dataTableWindow = nullptr;
    QuickPlotWindow *m_quickPlotWindow = nullptr;
    FluentQt::PrimaryPushButton *m_connectButton = nullptr;
    FluentQt::PrimaryPushButton *m_sendButton = nullptr;
    FluentQt::TransparentToolButton *m_receiveModeButton = nullptr;
    FluentQt::PushButton *m_sendModeButton = nullptr;
    FluentQt::PrimaryPushButton *m_packetSendButton = nullptr;
    FluentQt::PrimaryPushButton *m_macroRunButton = nullptr;
    FluentQt::PrimaryPushButton *m_scriptRunButton = nullptr;
    FluentQt::PrimaryPushButton *m_fileSendButton = nullptr;
    FluentQt::ToolButton *m_refreshButton = nullptr;
    FluentQt::ToolButton *m_packetUpButton = nullptr;
    FluentQt::ToolButton *m_packetDownButton = nullptr;
    FluentQt::ToolButton *m_macroUpButton = nullptr;
    FluentQt::ToolButton *m_macroDownButton = nullptr;
    FluentQt::ToolButton *m_autoReplyUpButton = nullptr;
    FluentQt::ToolButton *m_autoReplyDownButton = nullptr;
    FluentQt::PushButton *m_clearButton = nullptr;
    FluentQt::PushButton *m_resetCountersButton = nullptr;
    FluentQt::PushButton *m_exportTxtButton = nullptr;
    FluentQt::PushButton *m_exportCsvButton = nullptr;
    FluentQt::PushButton *m_exportBinButton = nullptr;
    FluentQt::PushButton *m_protocolSaveButton = nullptr;
    FluentQt::PushButton *m_protocolDeleteButton = nullptr;
    FluentQt::PushButton *m_protocolExampleButton = nullptr;
    FluentQt::PushButton *m_packetSaveButton = nullptr;
    FluentQt::PushButton *m_packetLoadButton = nullptr;
    FluentQt::PushButton *m_packetDeleteButton = nullptr;
    FluentQt::PushButton *m_packetBatchSendButton = nullptr;
    FluentQt::PushButton *m_packetImportButton = nullptr;
    FluentQt::PushButton *m_packetExportButton = nullptr;
    FluentQt::PushButton *m_macroSaveButton = nullptr;
    FluentQt::PushButton *m_macroLoadButton = nullptr;
    FluentQt::PushButton *m_macroDeleteButton = nullptr;
    FluentQt::PushButton *m_macroStopButton = nullptr;
    FluentQt::PushButton *m_macroExportButton = nullptr;
    FluentQt::PushButton *m_scriptLoadButton = nullptr;
    FluentQt::PushButton *m_scriptSaveButton = nullptr;
    FluentQt::PushButton *m_scriptExampleButton = nullptr;
    FluentQt::PushButton *m_scriptStopButton = nullptr;
    FluentQt::PushButton *m_scriptClearLogButton = nullptr;
    FluentQt::PushButton *m_autoReplySaveButton = nullptr;
    FluentQt::PushButton *m_autoReplyLoadButton = nullptr;
    FluentQt::PushButton *m_autoReplyDeleteButton = nullptr;
    FluentQt::PushButton *m_checksumCalcButton = nullptr;
    FluentQt::PushButton *m_modbusFillButton = nullptr;
    FluentQt::PushButton *m_modbusSendButton = nullptr;
    FluentQt::PushButton *m_fileBrowseButton = nullptr;
    FluentQt::PushButton *m_fileCancelButton = nullptr;
    FluentQt::ToolButton *m_terminalSearchPrevButton = nullptr;
    FluentQt::ToolButton *m_terminalSearchNextButton = nullptr;
    FluentQt::CheckBox *m_saveReceiveCheck = nullptr;
    FluentQt::CheckBox *m_autoLogCheck = nullptr;
    FluentQt::CheckBox *m_autoScrollCheck = nullptr;
    FluentQt::CheckBox *m_timestampCheck = nullptr;
    FluentQt::CheckBox *m_pauseCheck = nullptr;
    FluentQt::CheckBox *m_autoFrameBreakCheck = nullptr;
    FluentQt::CheckBox *m_protocolEnabledCheck = nullptr;
    FluentQt::CheckBox *m_hexSendCheck = nullptr;
    FluentQt::CheckBox *m_showTxCheck = nullptr;
    FluentQt::CheckBox *m_loopCheck = nullptr;
    FluentQt::CheckBox *m_packetEnabledCheck = nullptr;
    FluentQt::CheckBox *m_macroAbortOnFailureCheck = nullptr;
    FluentQt::CheckBox *m_autoReplyEnabledCheck = nullptr;
    FluentQt::CheckBox *m_checksumAppendCheck = nullptr;
    FluentQt::CheckBox *m_autoReconnectCheck = nullptr;
    FluentQt::CheckBox *m_terminalSearchCaseCheck = nullptr;
    FluentQt::CheckBox *m_terminalSearchRegexCheck = nullptr;
    FluentQt::CheckBox *m_autoOpenCheck = nullptr;
    FluentQt::CheckBox *m_rtsCheck = nullptr;
    FluentQt::CheckBox *m_dtrCheck = nullptr;
    FluentQt::LineEdit *m_frameBreakIntervalEdit = nullptr;
    FluentQt::LineEdit *m_frameFixedLengthEdit = nullptr;
    FluentQt::LineEdit *m_protocolLengthOffsetEdit = nullptr;
    FluentQt::LineEdit *m_protocolCommandOffsetEdit = nullptr;
    FluentQt::LineEdit *m_protocolCommandSizeEdit = nullptr;
    FluentQt::LineEdit *m_protocolPayloadOffsetEdit = nullptr;
    FluentQt::LineEdit *m_protocolPayloadLengthEdit = nullptr;
    FluentQt::LineEdit *m_modbusSlaveEdit = nullptr;
    FluentQt::LineEdit *m_modbusAddressEdit = nullptr;
    FluentQt::LineEdit *m_modbusQuantityEdit = nullptr;
    FluentQt::LineEdit *m_loopIntervalEdit = nullptr;
    FluentQt::LineEdit *m_macroTimeoutEdit = nullptr;
    FluentQt::LineEdit *m_macroDelayEdit = nullptr;
    FluentQt::LineEdit *m_macroLoopCountEdit = nullptr;
    FluentQt::LineEdit *m_autoReplyDelayEdit = nullptr;
    FluentQt::LineEdit *m_fileChunkSizeEdit = nullptr;
    FluentQt::LineEdit *m_fileIntervalEdit = nullptr;
    FluentQt::LineEdit *m_autoLogMaxSizeEdit = nullptr;
    FluentQt::ColorPickerButton *m_txColorButton = nullptr;
    FluentQt::LineEdit *m_filePathEdit = nullptr;
    FluentQt::ProgressBar *m_fileProgressBar = nullptr;
    FluentQt::CaptionLabel *m_receiveCaptureLabel = nullptr;
    FluentQt::CaptionLabel *m_autoLogStatusLabel = nullptr;
    FluentQt::CaptionLabel *m_protocolStatusLabel = nullptr;
    FluentQt::CaptionLabel *m_checksumResultLabel = nullptr;
    FluentQt::CaptionLabel *m_modbusStatusLabel = nullptr;
    FluentQt::CaptionLabel *m_macroStatusLabel = nullptr;
    FluentQt::CaptionLabel *m_scriptStatusLabel = nullptr;
    FluentQt::CaptionLabel *m_autoReplyStatusLabel = nullptr;
    FluentQt::CaptionLabel *m_terminalSummaryLabel = nullptr;
    FluentQt::CaptionLabel *m_connectionTimeLabel = nullptr;
    FluentQt::CaptionLabel *m_fileStatusLabel = nullptr;
    FluentQt::StrongBodyLabel *m_rxCounterLabel = nullptr;
    FluentQt::StrongBodyLabel *m_txCounterLabel = nullptr;
    FluentQt::StrongBodyLabel *m_rxRateLabel = nullptr;
    FluentQt::StrongBodyLabel *m_txRateLabel = nullptr;
    QString m_scriptFilePath;
};
