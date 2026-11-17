#pragma once

#include "app/core/protocol_template.h"

#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QSet>
#include <QtWidgets/QWidget>

class QTimer;
namespace FluentQt {
class CaptionLabel;
class CheckBox;
class PushButton;
class RealtimePlotWidget;
class TableWidget;
} // namespace FluentQt

class ProtocolAnalysisWindow : public QWidget
{
    Q_OBJECT

  public:
    explicit ProtocolAnalysisWindow(QWidget *parent = nullptr);
    void setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &templates,
                              const QString &selectedTemplate = {});
    void setParsingEnabled(bool enabled);
    void appendRecord(const QDateTime &timestamp, const QByteArray &bytes, bool stream = false);
    void clearData();
    void resetStream();

  signals:
    void parsingEnabledChanged(bool enabled);
    void editTemplateRequested(const QString &name);

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    struct Channel
    {
        QString key;
        QString label;
        double value = 0.0;
        double x = 0.0;
        QDateTime timestamp;
        int series = -1;
    };

    bool isCapturing() const;
    void updateActivity();
    void resetChannels();
    void updateValues();
    void updateStatus();
    int channelFor(const QString &templateName, const QString &address, const QString &fieldName);
    void setCurveVisible(int row, bool visible);
    void appendFrame(const QDateTime &timestamp, const QByteArray &frame,
                     const AppProtocol::ProtocolTemplate &protocolTemplate, const AppProtocol::ParseResult &parsed);

    QList<AppProtocol::ProtocolTemplate> m_templates;
    QSet<QString> m_selectedTemplates;
    QSet<QString> m_curveKeys;
    QHash<QString, QByteArray> m_streamBuffers;
    QVector<Channel> m_channels;
    QHash<QString, int> m_channelIndexes;
    FluentQt::CheckBox *m_enabledCheck = nullptr;
    FluentQt::PushButton *m_pauseButton = nullptr;
    FluentQt::TableWidget *m_templateTable = nullptr;
    FluentQt::TableWidget *m_valueTable = nullptr;
    FluentQt::RealtimePlotWidget *m_plot = nullptr;
    FluentQt::CaptionLabel *m_status = nullptr;
    QTimer *m_valueTimer = nullptr;
    QDateTime m_startTime;
    double m_lastX = 0.0;
    qint64 m_frameCount = 0;
    int m_allocatedSeries = 0;
    int m_refreshRate = 30;
    bool m_paused = false;
    bool m_limited = false;
};
