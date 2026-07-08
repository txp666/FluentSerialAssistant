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
    QWidget *createTerminalSection();
    QWidget *createSendSection();
    QWidget *createCheckRow(const QStringList &labels,
                            const QList<FluentQt::CheckBox **> &targets,
                            QWidget *parent);

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

    void setupSerialSignals();
    void refreshPorts();
    void restoreSettings();
    void applyTerminalFont(const QString &family = QString());
    void updateConnectionUi(bool connected);
    void updateCounters();
    void updateHistoryCombo();
    void applyHistoryItem(int index);
    void showInfo(const QString &title, const QString &message);
    void showSuccess(const QString &title, const QString &message);
    void showWarning(const QString &title, const QString &message);
    void showError(const QString &title, const QString &message);

    SerialPortConfig currentSerialConfig() const;
    QByteArray currentPayload(bool *ok = nullptr);
    QByteArray selectedLineEnding() const;
    QString currentDisplayMode() const;
    QString selectedLineEndingKey() const;
    QString directionText(RecordDirection direction) const;
    QString historyLabel(const SendHistoryItem &item) const;
    QString exportSuffix(ExportFormat format) const;
    QString formatRecordLine(const SessionRecord &record) const;
    QColor selectedTxColor() const;
    void appendRecord(RecordDirection direction, const QByteArray &data);
    void trimRecords();
    void renderTerminal();
    bool appendRecordToTerminal(QTextCursor &cursor, const SessionRecord &record, bool hasPrevious);
    void flushPendingLines();
    void sendCurrentPayload();
    void addSendHistory(const SendHistoryItem &item);
    void loadSendHistory();
    void saveSendHistory() const;
    void exportRecords(ExportFormat format);
    void updateReceiveCapture(bool enabled);
    void closeReceiveCapture();
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
    SerialPortConfig m_lastConfig;
    qint64 m_rxCount = 0;
    qint64 m_txCount = 0;
    int m_terminalStartRecord = 0;
    QDateTime m_lastRxTimestamp;
    QTimer m_flushTimer;
    QTimer m_loopTimer;
    QTimer m_reconnectTimer;
    QFile m_receiveCaptureFile;
    bool m_manualDisconnect = false;

    FluentQt::ComboBox *m_portCombo = nullptr;
    FluentQt::EditableComboBox *m_baudCombo = nullptr;
    FluentQt::ComboBox *m_dataBitsCombo = nullptr;
    FluentQt::ComboBox *m_parityCombo = nullptr;
    FluentQt::ComboBox *m_stopBitsCombo = nullptr;
    FluentQt::ComboBox *m_flowControlCombo = nullptr;
    FluentQt::SegmentedWidget *m_displayModeSegment = nullptr;
    FluentQt::ComboBox *m_lineEndingCombo = nullptr;
    FluentQt::ComboBox *m_historyCombo = nullptr;
    FluentQt::PlainTextEdit *m_sendEdit = nullptr;
    FluentQt::TextBrowser *m_terminalView = nullptr;
    FluentQt::PrimaryPushButton *m_connectButton = nullptr;
    FluentQt::PrimaryPushButton *m_sendButton = nullptr;
    FluentQt::ToolButton *m_refreshButton = nullptr;
    FluentQt::PushButton *m_clearButton = nullptr;
    FluentQt::PushButton *m_resetCountersButton = nullptr;
    FluentQt::PushButton *m_exportTxtButton = nullptr;
    FluentQt::PushButton *m_exportCsvButton = nullptr;
    FluentQt::PushButton *m_exportBinButton = nullptr;
    FluentQt::CheckBox *m_saveReceiveCheck = nullptr;
    FluentQt::CheckBox *m_autoScrollCheck = nullptr;
    FluentQt::CheckBox *m_timestampCheck = nullptr;
    FluentQt::CheckBox *m_pauseCheck = nullptr;
    FluentQt::CheckBox *m_autoFrameBreakCheck = nullptr;
    FluentQt::CheckBox *m_hexSendCheck = nullptr;
    FluentQt::CheckBox *m_showTxCheck = nullptr;
    FluentQt::CheckBox *m_loopCheck = nullptr;
    FluentQt::CheckBox *m_autoReconnectCheck = nullptr;
    FluentQt::CheckBox *m_rtsCheck = nullptr;
    FluentQt::CheckBox *m_dtrCheck = nullptr;
    FluentQt::SpinBox *m_frameBreakIntervalSpin = nullptr;
    FluentQt::SpinBox *m_loopIntervalSpin = nullptr;
    FluentQt::ColorPickerButton *m_txColorButton = nullptr;
    FluentQt::CaptionLabel *m_receiveCaptureLabel = nullptr;
    FluentQt::StrongBodyLabel *m_rxCounterLabel = nullptr;
    FluentQt::StrongBodyLabel *m_txCounterLabel = nullptr;
};
