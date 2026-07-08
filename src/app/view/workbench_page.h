#pragma once

#include "app/serial/serial_controller.h"
#include "app/view/app_page.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QTimer>
#include <QtGui/QColor>

class QEvent;
class QTextCursor;

class WorkbenchPage : public AppPage
{
    Q_OBJECT

  public:
    explicit WorkbenchPage(QWidget *parent = nullptr);
    ~WorkbenchPage() override;

    void saveSettings() const;

  public slots:
    void setTerminalFontFamily(const QString &family);

  signals:
    void settingsRequested();

  private:
    bool eventFilter(QObject *watched, QEvent *event) override;

    QWidget *createWorkbench();
    QWidget *createConnectionSection();
    QWidget *createReceiveSettingsSection();
    QWidget *createSendSettingsSection();
    QWidget *createPacketSection();
    QWidget *createFileSendSection();
    QWidget *createTerminalSection();
    QWidget *createSendSection();
    QWidget *createCheckRow(const QStringList &labels, const QList<FluentQt::CheckBox **> &targets, QWidget *parent);

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
        QString displayText;
    };

    struct SendHistoryItem
    {
        QString mode;
        QString payload;
        QString lineEnding;
    };

    struct SendPacket
    {
        QString name;
        QString mode;
        QString payload;
        QString lineEnding;
        bool enabled = true;
    };

    void setupSerialSignals();
    void refreshPorts();
    void restoreSettings();
    void applyTerminalFont(const QString &family = QString());
    void updateConnectionUi(bool connected);
    void updateCounters();
    void updateRateStats();
    void updateHistoryCombo();
    void applyHistoryItem(int index);
    void updatePacketTable(int selectedRow = -1);
    void applyPacket(int row);
    void saveCurrentPacket();
    void removeSelectedPacket();
    void moveSelectedPacket(int direction);
    void sendSelectedPacket();
    void sendPacket(int row);
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
    QString terminalSearchText() const;
    QString terminalDirectionFilter() const;
    QString directionText(RecordDirection direction) const;
    QString historyLabel(const SendHistoryItem &item) const;
    QString exportSuffix(ExportFormat format) const;
    QString formatRecordLine(const SessionRecord &record) const;
    QColor selectedTxColor() const;
    bool recordMatchesTerminalFilter(const SessionRecord &record) const;
    void appendRecord(RecordDirection direction, const QByteArray &data);
    void trimRecords();
    void renderTerminal();
    bool appendRecordToTerminal(QTextCursor &cursor, const SessionRecord &record, bool hasPrevious);
    void flushPendingLines();
    void sendCurrentPayload();
    void addSendHistory(const SendHistoryItem &item);
    void loadSendHistory();
    void saveSendHistory() const;
    void loadSendPackets();
    void saveSendPackets() const;
    void exportRecords(ExportFormat format);
    void updateReceiveCapture(bool enabled);
    void closeReceiveCapture();
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
    SerialPortConfig m_lastConfig;
    qint64 m_rxCount = 0;
    qint64 m_txCount = 0;
    qint64 m_lastStatsRxCount = 0;
    qint64 m_lastStatsTxCount = 0;
    qint64 m_fileSendTotal = 0;
    qint64 m_fileSendSent = 0;
    int m_terminalStartRecord = 0;
    QDateTime m_lastRxTimestamp;
    QDateTime m_connectionStartedAt;
    QTimer m_flushTimer;
    QTimer m_loopTimer;
    QTimer m_reconnectTimer;
    QTimer m_statsTimer;
    QTimer m_fileSendTimer;
    QFile m_receiveCaptureFile;
    QFile m_fileSendFile;
    bool m_manualDisconnect = false;

    FluentQt::ComboBox *m_portCombo = nullptr;
    FluentQt::EditableComboBox *m_baudCombo = nullptr;
    FluentQt::ComboBox *m_dataBitsCombo = nullptr;
    FluentQt::ComboBox *m_parityCombo = nullptr;
    FluentQt::ComboBox *m_stopBitsCombo = nullptr;
    FluentQt::ComboBox *m_flowControlCombo = nullptr;
    FluentQt::SegmentedWidget *m_displayModeSegment = nullptr;
    FluentQt::SearchLineEdit *m_terminalSearchEdit = nullptr;
    FluentQt::ComboBox *m_terminalFilterCombo = nullptr;
    FluentQt::ComboBox *m_lineEndingCombo = nullptr;
    FluentQt::ComboBox *m_historyCombo = nullptr;
    FluentQt::LineEdit *m_packetNameEdit = nullptr;
    FluentQt::PlainTextEdit *m_packetPayloadEdit = nullptr;
    FluentQt::ComboBox *m_packetModeCombo = nullptr;
    FluentQt::ComboBox *m_packetLineEndingCombo = nullptr;
    FluentQt::ListWidget *m_packetList = nullptr;
    FluentQt::PlainTextEdit *m_sendEdit = nullptr;
    FluentQt::TextBrowser *m_terminalView = nullptr;
    FluentQt::PrimaryPushButton *m_connectButton = nullptr;
    FluentQt::PrimaryPushButton *m_sendButton = nullptr;
    FluentQt::PrimaryPushButton *m_packetSendButton = nullptr;
    FluentQt::PrimaryPushButton *m_fileSendButton = nullptr;
    FluentQt::ToolButton *m_refreshButton = nullptr;
    FluentQt::ToolButton *m_packetUpButton = nullptr;
    FluentQt::ToolButton *m_packetDownButton = nullptr;
    FluentQt::PushButton *m_clearButton = nullptr;
    FluentQt::PushButton *m_resetCountersButton = nullptr;
    FluentQt::PushButton *m_exportTxtButton = nullptr;
    FluentQt::PushButton *m_exportCsvButton = nullptr;
    FluentQt::PushButton *m_exportBinButton = nullptr;
    FluentQt::PushButton *m_packetSaveButton = nullptr;
    FluentQt::PushButton *m_packetLoadButton = nullptr;
    FluentQt::PushButton *m_packetDeleteButton = nullptr;
    FluentQt::PushButton *m_fileBrowseButton = nullptr;
    FluentQt::PushButton *m_fileCancelButton = nullptr;
    FluentQt::CheckBox *m_saveReceiveCheck = nullptr;
    FluentQt::CheckBox *m_autoScrollCheck = nullptr;
    FluentQt::CheckBox *m_timestampCheck = nullptr;
    FluentQt::CheckBox *m_pauseCheck = nullptr;
    FluentQt::CheckBox *m_autoFrameBreakCheck = nullptr;
    FluentQt::CheckBox *m_hexSendCheck = nullptr;
    FluentQt::CheckBox *m_showTxCheck = nullptr;
    FluentQt::CheckBox *m_loopCheck = nullptr;
    FluentQt::CheckBox *m_autoReconnectCheck = nullptr;
    FluentQt::CheckBox *m_autoOpenCheck = nullptr;
    FluentQt::CheckBox *m_rtsCheck = nullptr;
    FluentQt::CheckBox *m_dtrCheck = nullptr;
    FluentQt::SpinBox *m_frameBreakIntervalSpin = nullptr;
    FluentQt::SpinBox *m_loopIntervalSpin = nullptr;
    FluentQt::SpinBox *m_fileChunkSizeSpin = nullptr;
    FluentQt::SpinBox *m_fileIntervalSpin = nullptr;
    FluentQt::ColorPickerButton *m_txColorButton = nullptr;
    FluentQt::LineEdit *m_filePathEdit = nullptr;
    FluentQt::ProgressBar *m_fileProgressBar = nullptr;
    FluentQt::CaptionLabel *m_receiveCaptureLabel = nullptr;
    FluentQt::CaptionLabel *m_terminalSummaryLabel = nullptr;
    FluentQt::CaptionLabel *m_connectionTimeLabel = nullptr;
    FluentQt::CaptionLabel *m_fileStatusLabel = nullptr;
    FluentQt::StrongBodyLabel *m_rxCounterLabel = nullptr;
    FluentQt::StrongBodyLabel *m_txCounterLabel = nullptr;
    FluentQt::StrongBodyLabel *m_rxRateLabel = nullptr;
    FluentQt::StrongBodyLabel *m_txRateLabel = nullptr;
};
