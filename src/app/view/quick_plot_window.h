#pragma once

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

    void appendText(const QDateTime &timestamp, const QString &text, bool ignorePause = false);
    void clearData();

  signals:
    void protocolChanged();

  private:
    enum class PlotProtocol
    {
        Numbers,
        Delimited,
        KeyValue,
        Json
    };

    struct PlotValue
    {
        QString name;
        double value = 0.0;
    };

    struct PlotRow
    {
        QDateTime timestamp;
        int sample = 0;
        QVector<double> values;
    };

    PlotProtocol protocolFromKey(const QString &key) const;
    QString protocolKey(PlotProtocol protocol) const;
    QVector<PlotValue> extractValues(const QString &text) const;
    QVector<PlotValue> extractNumberValues(const QString &text) const;
    QVector<PlotValue> extractDelimitedValues(const QString &text) const;
    QVector<PlotValue> extractKeyValuePairs(const QString &text) const;
    QVector<PlotValue> extractJsonValues(const QString &text) const;
    int channelIndexFor(const QString &name, int position);
    void ensureSeriesCount(int count);
    void updateStatus();
    void setPaused(bool paused);
    void setProtocol(PlotProtocol protocol);
    void showProtocolHelp(QWidget *target);
    void exportCsv();

    FluentQt::RealtimePlotWidget *m_plot = nullptr;
    FluentQt::ComboBox *m_protocolCombo = nullptr;
    FluentQt::PushButton *m_pauseButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;
    QVector<QString> m_channelNames;
    QVector<PlotRow> m_rows;
    PlotProtocol m_protocol = PlotProtocol::Numbers;
    int m_channelCount = 0;
    int m_nextSample = 0;
    bool m_paused = false;
};
