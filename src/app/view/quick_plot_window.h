#pragma once

#include "app/core/plot_value_parser.h"
#include "app/core/protocol_template.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

namespace FluentQt {
class CaptionLabel;
class BodyLabel;
class PushButton;
class RealtimePlotWidget;
} // namespace FluentQt

class QuickPlotWindow : public QWidget
{
    Q_OBJECT

    friend class WorkbenchQuickPlotTest;

  public:
    explicit QuickPlotWindow(QWidget *parent = nullptr);

    void appendRecord(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                      const QByteArray &payload = {});
    void clearData();
    bool configureParser(const AppPlot::ParserConfig &config);
    void setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &protocolTemplates);
    void setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate);
    bool requiresProtocolPayload() const;
    bool isPlottingActive() const;

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

    void appendValues(const QDateTime &timestamp, const AppPlot::PlotSample &values);
    int channelIndexFor(const QString &name, int position);
    void ensureSeriesCount(int count);
    void updateStatus();
    void updateProtocolName();
    void setPlottingActive(bool active);
    void setPaused(bool paused);
    void showParserSettings();
    void showProtocolHelp(QWidget *target);
    void exportCsv();

    FluentQt::RealtimePlotWidget *m_plot = nullptr;
    FluentQt::BodyLabel *m_protocolNameLabel = nullptr;
    FluentQt::PushButton *m_pauseButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;
    QVector<QString> m_channelNames;
    QVector<PlotRow> m_rows;
    AppPlot::ParserConfig m_parserConfig;
    QList<AppProtocol::ProtocolTemplate> m_protocolTemplates;
    AppProtocol::ProtocolTemplate m_protocolTemplate;
    int m_channelCount = 0;
    int m_nextSample = 0;
    int m_plotRefreshRate = 0;
    bool m_paused = false;
    bool m_plottingActive = false;
    bool m_hasProtocolTemplate = false;
};
