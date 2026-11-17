#pragma once

#include "app/core/plot_value_parser.h"
#include "app/core/protocol_template.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QPointF>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

class QTextStream;
class QTimer;

namespace FluentQt {
class CaptionLabel;
class DoubleSpinBox;
class PushButton;
class RealtimePlotWidget;
class TableWidget;
} // namespace FluentQt

class QuickPlotWindow : public QWidget
{
    Q_OBJECT

    friend class WorkbenchQuickPlotTest;

  public:
    explicit QuickPlotWindow(QWidget *parent = nullptr);

    // The parser configuration a window last applied, used to prefill new windows.
    static AppPlot::ParserConfig savedParserConfig();

    void appendRecord(const QDateTime &timestamp, const QString &text, const QByteArray &frame, bool stream = false);
    void clearData();
    void resetStream();
    bool configureParser(const AppPlot::ParserConfig &config);
    void setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &protocolTemplates);
    void setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate);
    bool requiresProtocolPayload() const;
    bool isPlottingActive() const;
    bool isPaused() const;

  signals:
    void editTemplateRequested(const QString &name);

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

  private:
    struct PlotRow
    {
        QDateTime timestamp;
        int sample = 0;
        QVector<double> values;
    };

    struct ChannelTransform
    {
        double scale = 1.0;
        double offset = 0.0;
        QVector<QPointF> samples;
    };

    void appendValues(const QDateTime &timestamp, const AppPlot::PlotSample &values);
    void appendParsedValues(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                            const QByteArray &payload = {});
    // The channel for a value, or -1 when it would exceed AppPlot::MaximumChannels.
    int channelIndexFor(const QString &name, int position);
    void ensureSeriesCount(int count);
    QWidget *createChannelPanel();
    void setChannelPanelVisible(bool visible);
    void setChannelSettingsVisible(bool visible);
    void updateChannelValues();
    void updateChannelSelection();
    void applyChannelTransform(double scale, double offset);
    double transformedValue(int channel, double value) const;
    void rebuildPlot(int channel);
    void updateStatus();
    void updateProtocolName();
    void setPlottingActive(bool active);
    void setPaused(bool paused);
    void showParserSettings();
    void exportCsv();
    void writeCsv(QTextStream &out) const;

    FluentQt::RealtimePlotWidget *m_plot = nullptr;
    FluentQt::PushButton *m_protocolButton = nullptr;
    FluentQt::PushButton *m_pauseButton = nullptr;
    FluentQt::PushButton *m_channelToggleButton = nullptr;
    FluentQt::PushButton *m_channelSettingsToggleButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;
    FluentQt::TableWidget *m_channelTable = nullptr;
    FluentQt::DoubleSpinBox *m_channelScaleSpin = nullptr;
    FluentQt::DoubleSpinBox *m_channelOffsetSpin = nullptr;
    FluentQt::CaptionLabel *m_parsedValueLabel = nullptr;
    QWidget *m_channelSettings = nullptr;
    QWidget *m_channelPanel = nullptr;
    QTimer *m_channelValueTimer = nullptr;
    QVector<QString> m_channelNames;
    QVector<double> m_latestValues;
    QHash<QString, ChannelTransform> m_channelTransforms;
    QVector<PlotRow> m_rows;
    AppPlot::ParserConfig m_parserConfig;
    QList<AppProtocol::ProtocolTemplate> m_protocolTemplates;
    AppProtocol::ProtocolTemplate m_protocolTemplate;
    QByteArray m_frameBuffer;
    int m_channelCount = 0;
    int m_nextSample = 0;
    int m_plotRefreshRate = 0;
    bool m_paused = false;
    bool m_plottingActive = false;
    bool m_channelsLimited = false;
    bool m_hasProtocolTemplate = false;
    bool m_followTemplateFields = false;
};
