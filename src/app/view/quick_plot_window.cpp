#include "app/view/quick_plot_window.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>
#include <QtCore/QTextStream>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <limits>

namespace {

const QRegularExpression &numberPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"([+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?)"));
    return pattern;
}

const QRegularExpression &wholeNumberPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?$)"));
    return pattern;
}

double blankValue() { return std::numeric_limits<double>::quiet_NaN(); }

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (escaped.contains(QLatin1Char(',')) || escaped.contains(QLatin1Char('"')) ||
        escaped.contains(QLatin1Char('\n')) || escaped.contains(QLatin1Char('\r'))) {
        return QStringLiteral("\"%1\"").arg(escaped);
    }
    return escaped;
}

} // namespace

QuickPlotWindow::QuickPlotWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    using namespace FluentQt;

    setWindowTitle(QStringLiteral("快速绘图"));
    setMinimumSize(760, 460);
    resize(980, 620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    auto *protocolLabel = new BodyLabel(QStringLiteral("协议"), this);
    protocolLabel->setFixedHeight(32);
    m_protocolCombo = new ComboBox(this);
    m_protocolCombo->addItem(QStringLiteral("全部数字"), QIcon(), QStringLiteral("numbers"));
    m_protocolCombo->addItem(QStringLiteral("分隔值"), QIcon(), QStringLiteral("delimited"));
    m_protocolCombo->addItem(QStringLiteral("键值对"), QIcon(), QStringLiteral("keyValue"));
    m_protocolCombo->addItem(QStringLiteral("JSON 对象"), QIcon(), QStringLiteral("json"));
    m_protocolCombo->setFixedSize(132, 32);
    m_protocolCombo->setToolTip(QStringLiteral("选择接收文本如何转换为曲线数据"));
    const QString savedProtocol =
        QSettings().value(QStringLiteral("plot/protocol"), QStringLiteral("numbers")).toString();
    const int protocolIndex = m_protocolCombo->findData(savedProtocol);
    m_protocolCombo->setCurrentIndex(protocolIndex >= 0 ? protocolIndex : 0);
    m_protocol = protocolFromKey(m_protocolCombo->currentData().toString());
    auto *protocolHelpButton = new TransparentToolButton(icon(FluentIcon::Question), this);
    protocolHelpButton->setToolTip(QStringLiteral("绘图协议示例"));
    protocolHelpButton->setFixedSize(32, 32);
    protocolHelpButton->setIconSize(QSize(16, 16));

    m_pauseButton = new PushButton(icon(FluentIcon::Pause), QStringLiteral("暂停"), this);
    m_pauseButton->setFixedHeight(32);
    auto *clearButton = new PushButton(icon(FluentIcon::Broom), QStringLiteral("清空"), this);
    clearButton->setFixedHeight(32);
    auto *resetButton = new PushButton(icon(FluentIcon::FitPage), QStringLiteral("复位视图"), this);
    resetButton->setFixedHeight(32);
    auto *exportButton = new PushButton(icon(FluentIcon::ImageExport), QStringLiteral("导出 CSV"), this);
    exportButton->setFixedHeight(32);

    m_statusLabel = new CaptionLabel(QString(), this);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    toolbar->addWidget(protocolLabel);
    toolbar->addWidget(m_protocolCombo);
    toolbar->addWidget(protocolHelpButton);
    toolbar->addSpacing(4);
    toolbar->addWidget(m_pauseButton);
    toolbar->addWidget(clearButton);
    toolbar->addWidget(resetButton);
    toolbar->addWidget(exportButton);
    toolbar->addStretch(1);
    toolbar->addWidget(m_statusLabel, 1);
    root->addLayout(toolbar);

    m_plot = new RealtimePlotWidget(this);
    m_plot->setMinimumHeight(360);
    m_plot->setCapacity(120000);
    m_plot->setVisibleSpan(600);
    m_plot->setMaximumVisiblePoints(2000);
    m_plot->setAutoScroll(true);
    m_plot->setAutoYRange(true);
    m_plot->setGridVisible(true);
    m_plot->setLegendVisible(true);
    m_plot->setCrosshairVisible(true);
    m_plot->setPointsVisible(false);
    m_plot->setSeriesName(0, QStringLiteral("CH1"));
    root->addWidget(m_plot, 1);

    connect(m_protocolCombo, &ComboBox::currentIndexChanged, this,
            [this](int) { setProtocol(protocolFromKey(m_protocolCombo->currentData().toString())); });
    connect(protocolHelpButton, &TransparentToolButton::clicked, this,
            [this, protocolHelpButton]() { showProtocolHelp(protocolHelpButton); });
    connect(m_pauseButton, &PushButton::clicked, this, [this]() { setPaused(!m_paused); });
    connect(clearButton, &PushButton::clicked, this, &QuickPlotWindow::clearData);
    connect(resetButton, &PushButton::clicked, m_plot, &RealtimePlotWidget::resetView);
    connect(exportButton, &PushButton::clicked, this, &QuickPlotWindow::exportCsv);

    updateStatus();
}

void QuickPlotWindow::appendText(const QDateTime &timestamp, const QString &text, bool ignorePause)
{
    if (m_paused && !ignorePause) {
        return;
    }

    const QVector<PlotValue> values = extractValues(text);
    if (values.isEmpty()) {
        return;
    }

    const int sample = m_nextSample++;
    PlotRow row;
    row.timestamp = timestamp;
    row.sample = sample;

    for (int index = 0; index < values.size(); ++index) {
        const PlotValue &plotValue = values.at(index);
        const int channelIndex = channelIndexFor(plotValue.name, index);
        if (row.values.size() < m_channelCount) {
            const int oldSize = row.values.size();
            row.values.resize(m_channelCount);
            for (int valueIndex = oldSize; valueIndex < row.values.size(); ++valueIndex) {
                row.values[valueIndex] = blankValue();
            }
        }
        row.values[channelIndex] = plotValue.value;
        m_plot->appendPoint(channelIndex, sample, plotValue.value);
    }

    m_rows.append(row);
    updateStatus();
}

void QuickPlotWindow::clearData()
{
    m_rows.clear();
    m_channelNames.clear();
    m_channelCount = 0;
    m_nextSample = 0;
    if (m_plot) {
        m_plot->clearSeries();
        m_plot->addSeries(QStringLiteral("CH1"));
        m_plot->resetView();
    }
    updateStatus();
}

QuickPlotWindow::PlotProtocol QuickPlotWindow::protocolFromKey(const QString &key) const
{
    if (key == QStringLiteral("delimited")) {
        return PlotProtocol::Delimited;
    }
    if (key == QStringLiteral("keyValue")) {
        return PlotProtocol::KeyValue;
    }
    if (key == QStringLiteral("json")) {
        return PlotProtocol::Json;
    }
    return PlotProtocol::Numbers;
}

QString QuickPlotWindow::protocolKey(PlotProtocol protocol) const
{
    switch (protocol) {
    case PlotProtocol::Delimited:
        return QStringLiteral("delimited");
    case PlotProtocol::KeyValue:
        return QStringLiteral("keyValue");
    case PlotProtocol::Json:
        return QStringLiteral("json");
    case PlotProtocol::Numbers:
    default:
        return QStringLiteral("numbers");
    }
}

QVector<QuickPlotWindow::PlotValue> QuickPlotWindow::extractValues(const QString &text) const
{
    switch (m_protocol) {
    case PlotProtocol::Delimited:
        return extractDelimitedValues(text);
    case PlotProtocol::KeyValue:
        return extractKeyValuePairs(text);
    case PlotProtocol::Json:
        return extractJsonValues(text);
    case PlotProtocol::Numbers:
    default:
        return extractNumberValues(text);
    }
}

QVector<QuickPlotWindow::PlotValue> QuickPlotWindow::extractNumberValues(const QString &text) const
{
    QVector<PlotValue> values;
    auto it = numberPattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const double value = match.captured(0).toDouble(&ok);
        if (ok && std::isfinite(value)) {
            values.append({QString(), value});
        }
    }
    return values;
}

QVector<QuickPlotWindow::PlotValue> QuickPlotWindow::extractDelimitedValues(const QString &text) const
{
    QVector<PlotValue> values;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral(R"([,;\s]+)")), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QString trimmed = token.trimmed();
        if (!wholeNumberPattern().match(trimmed).hasMatch()) {
            continue;
        }
        bool ok = false;
        const double value = trimmed.toDouble(&ok);
        if (ok && std::isfinite(value)) {
            values.append({QString(), value});
        }
    }
    return values;
}

QVector<QuickPlotWindow::PlotValue> QuickPlotWindow::extractKeyValuePairs(const QString &text) const
{
    static const QRegularExpression pairPattern(
        QStringLiteral(
            R"(([\p{L}_][\p{L}\p{N}_\.\-]*)\s*[:=]\s*([+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?))"),
        QRegularExpression::UseUnicodePropertiesOption);

    QVector<PlotValue> values;
    auto it = pairPattern.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const double value = match.captured(2).toDouble(&ok);
        if (ok && std::isfinite(value)) {
            values.append({match.captured(1), value});
        }
    }
    return values;
}

QVector<QuickPlotWindow::PlotValue> QuickPlotWindow::extractJsonValues(const QString &text) const
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.trimmed().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }

    QVector<PlotValue> values;
    if (document.isArray()) {
        const QJsonArray array = document.array();
        for (const QJsonValue &entry : array) {
            if (entry.isDouble()) {
                const double value = entry.toDouble();
                if (std::isfinite(value)) {
                    values.append({QString(), value});
                }
            }
        }
        return values;
    }

    if (!document.isObject()) {
        return {};
    }

    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (it.value().isDouble()) {
            const double value = it.value().toDouble();
            if (std::isfinite(value)) {
                values.append({it.key(), value});
            }
        } else if (it.value().isArray()) {
            const QJsonArray array = it.value().toArray();
            for (int index = 0; index < array.size(); ++index) {
                if (!array.at(index).isDouble()) {
                    continue;
                }
                const double value = array.at(index).toDouble();
                if (std::isfinite(value)) {
                    values.append({QStringLiteral("%1_%2").arg(it.key()).arg(index + 1), value});
                }
            }
        }
    }
    return values;
}

int QuickPlotWindow::channelIndexFor(const QString &name, int position)
{
    QString channelName = name.trimmed();
    if (channelName.isEmpty()) {
        channelName = QStringLiteral("CH%1").arg(position + 1);
    }

    const int existingIndex = m_channelNames.indexOf(channelName);
    if (existingIndex >= 0) {
        return existingIndex;
    }

    m_channelNames.append(channelName);
    ensureSeriesCount(m_channelNames.size());
    return m_channelNames.size() - 1;
}

void QuickPlotWindow::ensureSeriesCount(int count)
{
    if (count <= m_channelCount) {
        return;
    }

    while (m_plot->seriesCount() < count) {
        const int index = m_plot->seriesCount();
        const QString name =
            index < m_channelNames.size() ? m_channelNames.at(index) : QStringLiteral("CH%1").arg(index + 1);
        m_plot->addSeries(name);
    }
    for (int index = m_channelCount; index < count; ++index) {
        const QString name =
            index < m_channelNames.size() ? m_channelNames.at(index) : QStringLiteral("CH%1").arg(index + 1);
        m_plot->setSeriesName(index, name);
        m_plot->setSeriesVisible(index, true);
    }
    m_channelCount = count;
}

void QuickPlotWindow::updateStatus()
{
    const QString state = m_paused ? QStringLiteral("已暂停") : QStringLiteral("实时");
    m_statusLabel->setText(
        QStringLiteral("%1 · %2 点 · %3 通道").arg(state).arg(m_rows.size()).arg(qMax(1, m_channelCount)));
}

void QuickPlotWindow::setPaused(bool paused)
{
    m_paused = paused;
    if (m_pauseButton) {
        m_pauseButton->setIcon(FluentQt::icon(paused ? FluentQt::FluentIcon::Play : FluentQt::FluentIcon::Pause));
        m_pauseButton->setText(paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    }
    updateStatus();
}

void QuickPlotWindow::setProtocol(PlotProtocol protocol)
{
    if (m_protocol == protocol) {
        return;
    }

    m_protocol = protocol;
    QSettings().setValue(QStringLiteral("plot/protocol"), protocolKey(protocol));
    clearData();
    emit protocolChanged();
}

void QuickPlotWindow::showProtocolHelp(QWidget *target)
{
    const QString content = QStringLiteral("全部数字：提取每行里的所有数字\n"
                                           "  T=24.8 H=60.5  => CH1=24.8, CH2=60.5\n\n"
                                           "分隔值：读取逗号、分号、空格分隔的纯数字\n"
                                           "  24.8,60.5,101.3  => CH1, CH2, CH3\n\n"
                                           "键值对：读取 name=value 或 name:value，字段名作为曲线名\n"
                                           "  temp=24.8 hum=60.5  => temp, hum\n\n"
                                           "JSON 对象：读取数值字段，数组字段会展开\n"
                                           "  {\"temp\":24.8,\"hum\":60.5}  => temp, hum");
    FluentQt::TeachingTip::create(QStringLiteral("绘图协议示例"), content,
                                  FluentQt::icon(FluentQt::FluentIcon::Question), QPixmap(), true, target,
                                  FluentQt::TeachingTipTailPosition::Bottom, -1, this);
}

void QuickPlotWindow::exportCsv()
{
    if (m_rows.isEmpty()) {
        FluentQt::InfoBar::warning(QStringLiteral("暂无曲线数据"), QStringLiteral("接收文本中还没有可导出的数字"),
                                   Qt::Horizontal, true, 2200, FluentQt::InfoBarPosition::TopRight, this);
        return;
    }

    QSettings settings;
    const QString initialFolder = settings.value(QStringLiteral("plot/exportFolder"), QDir::homePath()).toString();
    const QString initialName =
        QDir(initialFolder)
            .filePath(QStringLiteral("quick_plot_%1.csv")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出曲线 CSV"), initialName,
                                                      QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        FluentQt::InfoBar::error(QStringLiteral("导出失败"), file.errorString(), Qt::Horizontal, true, 3500,
                                 FluentQt::InfoBarPosition::TopRight, this);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QChar(0xFEFF);
    out << "timestamp,sample";
    for (int index = 0; index < m_channelCount; ++index) {
        const QString name =
            index < m_channelNames.size() ? m_channelNames.at(index) : QStringLiteral("channel_%1").arg(index + 1);
        out << ',' << csvEscape(name);
    }
    out << '\n';

    for (const PlotRow &row : m_rows) {
        out << csvEscape(row.timestamp.toString(Qt::ISODateWithMs)) << ',' << row.sample;
        for (int index = 0; index < m_channelCount; ++index) {
            out << ',';
            if (index < row.values.size() && std::isfinite(row.values.at(index))) {
                out << QString::number(row.values.at(index), 'g', 16);
            }
        }
        out << '\n';
    }
    out.flush();
    if (file.error() != QFile::NoError) {
        FluentQt::InfoBar::error(QStringLiteral("导出失败"), file.errorString(), Qt::Horizontal, true, 3500,
                                 FluentQt::InfoBarPosition::TopRight, this);
        return;
    }

    settings.setValue(QStringLiteral("plot/exportFolder"), QFileInfo(path).absolutePath());
    FluentQt::InfoBar::success(QStringLiteral("导出完成"), path, Qt::Horizontal, true, 2200,
                               FluentQt::InfoBarPosition::TopRight, this);
}
