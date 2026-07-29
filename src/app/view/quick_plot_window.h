#pragma once

#include "app/core/plot_value_parser.h"

#include <QtCore/QByteArray>
#include <QtCore/QDateTime>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

namespace FluentQt {
class CaptionLabel;
class ComboBox;
class PushButton;
class RealtimePlotWidget;
} // namespace FluentQt

class QuickPlotWindow : public QWidget
{
    Q_OBJECT

  public:
    explicit QuickPlotWindow(QWidget *parent = nullptr);

    void appendRecord(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                      const QByteArray &payload = {}, bool ignorePause = false);
    void clearData();
    bool configureParser(const AppPlot::ParserConfig &config);
    bool requiresProtocolPayload() const;

  signals:
    void protocolChanged();

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
    void setPaused(bool paused);
    void showParserSettings();
    void showProtocolHelp(QWidget *target);
    void exportCsv();

    FluentQt::RealtimePlotWidget *m_plot = nullptr;
    FluentQt::ComboBox *m_protocolCombo = nullptr;
    FluentQt::PushButton *m_pauseButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;
    QVector<QString> m_channelNames;
    QVector<PlotRow> m_rows;
    AppPlot::ParserConfig m_parserConfig;
    int m_channelCount = 0;
    int m_nextSample = 0;
    bool m_paused = false;
};
