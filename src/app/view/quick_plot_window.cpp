#include "app/view/quick_plot_window.h"
#include "app/core/app_i18n.h"
#include "app/view/fluent_tooltip_helper.h"
#include "app/view/plot_parser_dialog.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include "app/core/app_settings.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QTextStream>
#include <QtGui/QHideEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>

#include <cmath>
#include <limits>

namespace {

bool sameBinaryField(const AppPlot::BinaryField &left, const AppPlot::BinaryField &right)
{
    return left.name == right.name && left.byteOffset == right.byteOffset && left.type == right.type &&
           left.byteOrder == right.byteOrder && qFuzzyCompare(left.scale, right.scale) &&
           qFuzzyCompare(left.add, right.add);
}

bool sameParserConfig(const AppPlot::ParserConfig &left, const AppPlot::ParserConfig &right)
{
    if (left.protocol != right.protocol || left.fields != right.fields || left.binarySource != right.binarySource ||
        left.binaryFields.size() != right.binaryFields.size()) {
        return false;
    }
    for (int index = 0; index < left.binaryFields.size(); ++index) {
        if (!sameBinaryField(left.binaryFields.at(index), right.binaryFields.at(index))) {
            return false;
        }
    }
    return true;
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

    setWindowTitle(AppI18n::text("快速绘图"));
    setObjectName(QStringLiteral("quickPlotWindow"));
    setMinimumSize(760, 460);
    resize(980, 620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(4);

    // The protocol button names the current protocol and opens its settings.
    m_protocolButton = new PushButton(icon(FluentIcon::Setting), QString(), this);
    m_protocolButton->setObjectName(QStringLiteral("quickPlotProtocolButton"));
    m_protocolButton->setFixedHeight(32);
    m_parserConfig = savedParserConfig();
    updateProtocolName();

    m_pauseButton = new PushButton(icon(FluentIcon::Pause), AppI18n::text("暂停"), this);
    m_pauseButton->setFixedHeight(32);
    auto *clearButton = new TransparentToolButton(icon(FluentIcon::Broom), this);
    auto *resetButton = new TransparentToolButton(icon(FluentIcon::FitPage), this);
    auto *exportButton = new TransparentToolButton(icon(FluentIcon::SaveAs), this);
    for (const auto &action : {qMakePair(clearButton, AppI18n::text("清空")),
                               qMakePair(resetButton, AppI18n::text("复位视图")),
                               qMakePair(exportButton, AppI18n::text("导出 CSV"))}) {
        action.first->setFixedSize(32, 32);
        action.first->setIconSize(QSize(16, 16));
        action.first->setAccessibleName(action.second);
        AppUi::setFluentToolTip(action.first, action.second);
    }

    toolbar->addWidget(m_protocolButton, 0, Qt::AlignVCenter);
    toolbar->addStretch(1);
    toolbar->addWidget(m_pauseButton, 0, Qt::AlignVCenter);
    toolbar->addSpacing(4);
    toolbar->addWidget(clearButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(resetButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(exportButton, 0, Qt::AlignVCenter);
    root->addLayout(toolbar);

    // Below the chart the status has the full width, so a long field list wraps
    // instead of pushing the buttons out of view.
    m_statusLabel = new CaptionLabel(QString(), this);
    m_statusLabel->setObjectName(QStringLiteral("quickPlotStatus"));
    m_statusLabel->setWordWrap(true);

    m_plot = new RealtimePlotWidget(this);
    m_plot->setObjectName(QStringLiteral("quickPlotChart"));
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
    m_plotRefreshRate = m_plot->refreshRate();
    m_plot->setUpdatesEnabled(false);
    m_plot->setRefreshRate(0);
    root->addWidget(m_plot, 1);
    root->addWidget(m_statusLabel);

    connect(m_protocolButton, &PushButton::clicked, this, &QuickPlotWindow::showParserSettings);
    connect(m_pauseButton, &PushButton::clicked, this, [this]() { setPaused(!m_paused); });
    connect(clearButton, &TransparentToolButton::clicked, this, &QuickPlotWindow::clearData);
    connect(resetButton, &TransparentToolButton::clicked, m_plot, &RealtimePlotWidget::resetView);
    connect(exportButton, &TransparentToolButton::clicked, this, &QuickPlotWindow::exportCsv);

    updateStatus();
}

void QuickPlotWindow::appendRecord(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                                   const AppProtocol::ParseResult *parsed)
{
    if (!isPlottingActive() || m_paused) {
        return;
    }

    QByteArray payload;
    if (requiresProtocolPayload()) {
        if (!m_hasProtocolTemplate) {
            return;
        }
        const AppProtocol::ParseResult result = parsed ? *parsed : AppProtocol::parseFrame(frame, m_protocolTemplate);
        if (!result.ok || (result.checksumChecked && !result.checksumValid)) {
            return;
        }
        payload = result.payload;
    }
    // Without fields, binary data plots a channel per byte, up to the limit.
    if (m_parserConfig.protocol == AppPlot::Protocol::Binary && m_parserConfig.binaryFields.isEmpty()) {
        const QByteArray &data = m_parserConfig.binarySource == AppPlot::BinarySource::Payload ? payload : frame;
        m_channelsLimited = m_channelsLimited || data.size() > AppPlot::MaximumChannels;
    }
    const QVector<AppPlot::PlotSample> samples = AppPlot::extractSamples(m_parserConfig, text, frame, payload);
    for (const AppPlot::PlotSample &values : samples) {
        appendValues(timestamp, values);
    }
}

const AppProtocol::ProtocolTemplate *QuickPlotWindow::payloadTemplate() const
{
    return requiresProtocolPayload() && m_hasProtocolTemplate ? &m_protocolTemplate : nullptr;
}

bool QuickPlotWindow::isPlottingActive() const { return m_plottingActive; }

bool QuickPlotWindow::isPaused() const { return m_paused; }

void QuickPlotWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setPlottingActive(isVisible() && !isMinimized());
}

void QuickPlotWindow::hideEvent(QHideEvent *event)
{
    setPlottingActive(false);
    QWidget::hideEvent(event);
}

void QuickPlotWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        setPlottingActive(isVisible() && !isMinimized());
    }
}

void QuickPlotWindow::setPlottingActive(bool active)
{
    if (m_plottingActive == active) {
        return;
    }
    m_plottingActive = active;
    if (!active) {
        m_plot->setUpdatesEnabled(false);
        m_plotRefreshRate = m_plot->refreshRate();
        // Setting a zero refresh rate cancels the widget's pending single-shot refresh.
        m_plot->setRefreshRate(0);
        return;
    }

    m_plot->setRefreshRate(m_plotRefreshRate);
    m_plot->setUpdatesEnabled(true);
}

void QuickPlotWindow::appendValues(const QDateTime &timestamp, const AppPlot::PlotSample &values)
{
    const int sample = m_nextSample++;
    PlotRow row;
    row.timestamp = timestamp;
    row.sample = sample;

    for (int index = 0; index < values.size(); ++index) {
        const AppPlot::PlotValue &plotValue = values.at(index);
        const int channelIndex = channelIndexFor(plotValue.name, index);
        if (channelIndex < 0) {
            continue;
        }
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
    // Keep the export in step with the samples the plot retains (0 keeps all).
    const int capacity = m_plot->capacity();
    while (capacity > 0 && m_rows.size() > capacity) {
        m_rows.removeFirst();
    }
    updateStatus();
}

void QuickPlotWindow::clearData()
{
    m_rows.clear();
    m_channelNames.clear();
    m_channelCount = 0;
    m_channelsLimited = false;
    m_nextSample = 0;
    if (m_plot) {
        m_plot->clearSeries();
        m_plot->addSeries(QStringLiteral("CH1"));
        m_plot->resetView();
    }
    updateStatus();
}

AppPlot::ParserConfig QuickPlotWindow::savedParserConfig()
{
    AppSettings settings;
    const QString savedConfig = settings.value(QStringLiteral("plot/parserConfig")).toString();
    QJsonParseError savedConfigError;
    const QJsonDocument savedConfigDocument = QJsonDocument::fromJson(savedConfig.toUtf8(), &savedConfigError);
    AppPlot::ParserConfig config;
    QString configError;
    if (savedConfigError.error != QJsonParseError::NoError || !savedConfigDocument.isObject() ||
        !AppPlot::parserConfigFromJson(savedConfigDocument.object(), &config, &configError)) {
        const QString savedProtocol =
            settings.value(QStringLiteral("plot/protocol"), QStringLiteral("numbers")).toString();
        AppPlot::protocolFromKey(savedProtocol, &config.protocol);
    }
    return config;
}

bool QuickPlotWindow::configureParser(const AppPlot::ParserConfig &config)
{
    const QString key = AppPlot::protocolKey(config.protocol);
    if (!AppPlot::supportedProtocolKeys().contains(key)) {
        return false;
    }
    AppPlot::ParserConfig normalized = config;
    for (QString &field : normalized.fields) {
        field = field.simplified();
    }
    normalized.fields.removeAll(QString());
    if (sameParserConfig(m_parserConfig, normalized)) {
        return true;
    }
    m_parserConfig = normalized;
    updateProtocolName();
    AppSettings settings;
    settings.setValue(QStringLiteral("plot/protocol"), key);
    settings.setValue(
        QStringLiteral("plot/parserConfig"),
        QString::fromUtf8(QJsonDocument(AppPlot::parserConfigToJson(m_parserConfig)).toJson(QJsonDocument::Compact)));
    clearData();
    return true;
}

void QuickPlotWindow::setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &protocolTemplates)
{
    m_protocolTemplates = protocolTemplates;
}

void QuickPlotWindow::setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    if (m_hasProtocolTemplate && m_protocolTemplate == protocolTemplate) {
        return;
    }
    m_protocolTemplate = protocolTemplate;
    m_hasProtocolTemplate = true;
    updateProtocolName();
    if (requiresProtocolPayload()) {
        clearData();
    }
}

void QuickPlotWindow::updateProtocolName()
{
    QString name = PlotParserDialog::protocolName(m_parserConfig.protocol);
    if (requiresProtocolPayload() && m_hasProtocolTemplate) {
        name += QStringLiteral(" · ") + m_protocolTemplate.name;
    }
    if (m_protocolButton) {
        // A long template name would crowd out the other buttons.
        m_protocolButton->setText(m_protocolButton->fontMetrics().elidedText(name, Qt::ElideRight, 240));
        AppUi::setFluentToolTip(m_protocolButton, AppI18n::text("解析设置") + QStringLiteral(" · ") + name);
    }
}

bool QuickPlotWindow::requiresProtocolPayload() const
{
    return m_parserConfig.protocol == AppPlot::Protocol::Binary &&
           m_parserConfig.binarySource == AppPlot::BinarySource::Payload;
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
    // Every row stores a value per channel, so new names past the limit are
    // dropped instead of widening every row.
    if (m_channelNames.size() >= AppPlot::MaximumChannels) {
        m_channelsLimited = true;
        return -1;
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
    const QString state = m_paused ? AppI18n::text("已暂停") : AppI18n::text("实时");
    QString status = AppI18n::text("%1 · %2 点 · %3 通道").arg(state).arg(m_rows.size()).arg(qMax(1, m_channelCount));
    if (!m_parserConfig.fields.isEmpty()) {
        status += AppI18n::text(" · 字段：%1").arg(m_parserConfig.fields.join(QStringLiteral(", ")));
    }
    if (m_channelsLimited) {
        status += AppI18n::text(" · 最多显示 %1 个通道").arg(AppPlot::MaximumChannels);
    }
    m_statusLabel->setText(status);
}

void QuickPlotWindow::setPaused(bool paused)
{
    m_paused = paused;
    if (m_pauseButton) {
        m_pauseButton->setIcon(FluentQt::icon(paused ? FluentQt::FluentIcon::Play : FluentQt::FluentIcon::Pause));
        m_pauseButton->setText(paused ? AppI18n::text("继续") : AppI18n::text("暂停"));
    }
    updateStatus();
}

void QuickPlotWindow::showParserSettings()
{
    PlotParserDialog dialog(m_parserConfig, this);
    dialog.setProtocolTemplates(m_protocolTemplates);
    if (m_hasProtocolTemplate) {
        dialog.setProtocolTemplate(m_protocolTemplate);
    }
    if (dialog.exec() == QDialog::Accepted) {
        configureParser(dialog.parserConfig());
        if (dialog.usesProtocolTemplate()) {
            setProtocolTemplate(dialog.protocolTemplate());
        }
    }
}

void QuickPlotWindow::exportCsv()
{
    if (m_rows.isEmpty()) {
        FluentQt::InfoBar::warning(AppI18n::text("暂无曲线数据"), AppI18n::text("接收文本中还没有可导出的数字"),
                                   Qt::Horizontal, true, 2200, FluentQt::InfoBarPosition::TopRight, this);
        return;
    }

    AppSettings settings;
    const QString initialFolder = settings.value(QStringLiteral("plot/exportFolder"), QDir::homePath()).toString();
    const QString initialName =
        QDir(initialFolder)
            .filePath(QStringLiteral("quick_plot_%1.csv")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    const QString path = QFileDialog::getSaveFileName(this, AppI18n::text("导出曲线 CSV"), initialName,
                                                      AppI18n::text("CSV 文件 (*.csv)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        FluentQt::InfoBar::error(AppI18n::text("导出失败"), file.errorString(), Qt::Horizontal, true, 3500,
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
        FluentQt::InfoBar::error(AppI18n::text("导出失败"), file.errorString(), Qt::Horizontal, true, 3500,
                                 FluentQt::InfoBarPosition::TopRight, this);
        return;
    }

    settings.setValue(QStringLiteral("plot/exportFolder"), QFileInfo(path).absolutePath());
    FluentQt::InfoBar::success(AppI18n::text("导出完成"), path, Qt::Horizontal, true, 2200,
                               FluentQt::InfoBarPosition::TopRight, this);
}
