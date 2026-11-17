#include "app/view/quick_plot_window.h"
#include "app/core/app_i18n.h"
#include "app/view/fluent_tooltip_helper.h"
#include "app/view/plot_parser_dialog.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"
#include "app/view/muted_text.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTextStream>
#include <QtCore/QTimer>
#include <QtGui/QHideEvent>
#include <QtGui/QPixmap>
#include <QtGui/QShowEvent>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
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

QString valueText(double value) { return std::isfinite(value) ? QString::number(value, 'g', 12) : QStringLiteral("—"); }

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
    resize(1120, 620);

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
    m_channelToggleButton = new ToggleButton(icon(FluentIcon::View), AppI18n::text("通道数据"), this);
    m_channelToggleButton->setObjectName(QStringLiteral("quickPlotChannelToggle"));
    m_channelToggleButton->setFixedHeight(32);
    m_channelToggleButton->setChecked(true);
    auto *clearButton = new TransparentToolButton(icon(FluentIcon::Broom), this);
    auto *resetButton = new TransparentToolButton(icon(FluentIcon::FitPage), this);
    auto *exportButton = new TransparentToolButton(icon(FluentIcon::SaveAs), this);
    for (const auto &action :
         {qMakePair(clearButton, AppI18n::text("清空")), qMakePair(resetButton, AppI18n::text("复位视图")),
          qMakePair(exportButton, AppI18n::text("导出 CSV"))}) {
        action.first->setFixedSize(32, 32);
        action.first->setIconSize(QSize(16, 16));
        action.first->setAccessibleName(action.second);
        AppUi::setFluentToolTip(action.first, action.second);
    }

    toolbar->addWidget(m_protocolButton, 0, Qt::AlignVCenter);
    toolbar->addStretch(1);
    toolbar->addWidget(m_channelToggleButton, 0, Qt::AlignVCenter);
    toolbar->addSpacing(4);
    toolbar->addWidget(m_pauseButton, 0, Qt::AlignVCenter);
    toolbar->addSpacing(4);
    toolbar->addWidget(clearButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(resetButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(exportButton, 0, Qt::AlignVCenter);
    root->addLayout(toolbar);

    // Below the chart the status has the full width, so a long field list wraps
    // instead of pushing the buttons out of view.
    m_statusLabel = new CaptionLabel(QString(), this);
    AppUi::setMutedTextColor(m_statusLabel);
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
    m_plot->setLegendVisible(false);
    m_plot->setCrosshairVisible(true);
    m_plot->setPointsVisible(false);
    m_plot->setSeriesName(0, QStringLiteral("CH1"));
    m_plotRefreshRate = m_plot->refreshRate();
    m_plot->setUpdatesEnabled(false);
    m_plot->setRefreshRate(0);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName(QStringLiteral("quickPlotSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_plot);
    m_channelPanel = createChannelPanel();
    m_channelPanel->setObjectName(QStringLiteral("quickPlotChannelPanel"));
    splitter->addWidget(m_channelPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({780, 300});
    root->addWidget(splitter, 1);
    root->addWidget(m_statusLabel);

    connect(m_protocolButton, &PushButton::clicked, this, &QuickPlotWindow::showParserSettings);
    connect(m_pauseButton, &PushButton::clicked, this, [this]() { setPaused(!m_paused); });
    connect(m_channelToggleButton, &PushButton::toggled, this, &QuickPlotWindow::setChannelPanelVisible);
    connect(clearButton, &TransparentToolButton::clicked, this, &QuickPlotWindow::clearData);
    connect(resetButton, &TransparentToolButton::clicked, m_plot, &RealtimePlotWidget::resetView);
    connect(exportButton, &TransparentToolButton::clicked, this, &QuickPlotWindow::exportCsv);
    connect(m_plot, &RealtimePlotWidget::capacityChanged, this, [this](int capacity) {
        for (ChannelTransform &transform : m_channelTransforms) {
            if (capacity > 0 && transform.samples.size() > capacity) {
                transform.samples.remove(0, transform.samples.size() - capacity);
            }
        }
    });

    setChannelPanelVisible(true);
    updateStatus();
}

QWidget *QuickPlotWindow::createChannelPanel()
{
    using namespace FluentQt;

    auto *panel = new QWidget(this);
    panel->setMinimumWidth(280);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(new StrongBodyLabel(AppI18n::text("通道数据"), panel));

    m_channelTable = new TableWidget(panel);
    m_channelTable->setObjectName(QStringLiteral("quickPlotChannelTable"));
    m_channelTable->setBorderVisible(true);
    m_channelTable->setBorderRadius(8);
    m_channelTable->setColumnCount(2);
    m_channelTable->setHorizontalHeaderLabels({AppI18n::text("通道"), AppI18n::text("当前值")});
    m_channelTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_channelTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_channelTable->horizontalHeader()->resizeSection(1, 110);
    m_channelTable->verticalHeader()->hide();
    m_channelTable->verticalHeader()->setDefaultSectionSize(36);
    m_channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_channelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_channelTable->setWordWrap(false);
    m_channelTable->setMinimumHeight(120);
    layout->addWidget(m_channelTable, 1);

    m_channelSettingsToggleButton =
        new TransparentTogglePushButton(icon(FluentIcon::ChevronRightMed), AppI18n::text("通道设置"), panel);
    m_channelSettingsToggleButton->setObjectName(QStringLiteral("quickPlotChannelSettingsToggle"));
    m_channelSettingsToggleButton->setFixedHeight(32);
    layout->addWidget(m_channelSettingsToggleButton);

    m_channelSettings = new QWidget(panel);
    m_channelSettings->setObjectName(QStringLiteral("quickPlotChannelSettings"));
    auto *settingsLayout = new QVBoxLayout(m_channelSettings);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(8);
    m_parsedValueLabel = new CaptionLabel(QString(), m_channelSettings);
    m_parsedValueLabel->setObjectName(QStringLiteral("quickPlotParsedValue"));
    AppUi::setMutedTextColor(m_parsedValueLabel);
    settingsLayout->addWidget(m_parsedValueLabel);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_channelScaleSpin = new DoubleSpinBox(m_channelSettings);
    m_channelScaleSpin->setObjectName(QStringLiteral("quickPlotChannelScale"));
    m_channelOffsetSpin = new DoubleSpinBox(m_channelSettings);
    m_channelOffsetSpin->setObjectName(QStringLiteral("quickPlotChannelOffset"));
    for (DoubleSpinBox *spin : {m_channelScaleSpin, m_channelOffsetSpin}) {
        spin->setDecimals(9);
        spin->setRange(-1e12, 1e12);
        spin->setSingleStep(0.1);
        spin->setKeyboardTracking(false);
        spin->setMinimumWidth(120);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    m_channelScaleSpin->setValue(1.0);
    m_channelScaleSpin->setAccessibleName(AppI18n::text("倍率"));
    m_channelOffsetSpin->setAccessibleName(AppI18n::text("偏移"));
    form->addRow(AppI18n::text("倍率"), m_channelScaleSpin);
    form->addRow(AppI18n::text("偏移"), m_channelOffsetSpin);
    settingsLayout->addLayout(form);

    auto *hint = new CaptionLabel(AppI18n::text("显示值 = 解析值 × 倍率 + 偏移"), m_channelSettings);
    hint->setWordWrap(true);
    AppUi::setMutedTextColor(hint);
    settingsLayout->addWidget(hint);
    auto *resetButton = new PushButton(AppI18n::text("重置当前通道"), m_channelSettings);
    resetButton->setObjectName(QStringLiteral("quickPlotResetChannel"));
    settingsLayout->addWidget(resetButton);
    layout->addWidget(m_channelSettings);

    m_channelValueTimer = new QTimer(this);
    m_channelValueTimer->setSingleShot(true);
    m_channelValueTimer->setInterval(50);
    connect(m_channelValueTimer, &QTimer::timeout, this, &QuickPlotWindow::updateChannelValues);
    connect(m_channelSettingsToggleButton, &PushButton::toggled, this, &QuickPlotWindow::setChannelSettingsVisible);
    connect(m_channelTable, &TableWidget::itemSelectionChanged, this, &QuickPlotWindow::updateChannelSelection);
    connect(m_channelTable, &TableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == 0) {
            m_plot->setSeriesVisible(item->row(), item->checkState() == Qt::Checked);
        }
    });
    connect(m_channelScaleSpin, &DoubleSpinBox::valueChanged, this,
            [this](double value) { applyChannelTransform(value, m_channelOffsetSpin->value()); });
    connect(m_channelOffsetSpin, &DoubleSpinBox::valueChanged, this,
            [this](double value) { applyChannelTransform(m_channelScaleSpin->value(), value); });
    connect(resetButton, &PushButton::clicked, this, [this]() { applyChannelTransform(1.0, 0.0); });
    connect(m_plot, &RealtimePlotWidget::seriesChanged, this, &QuickPlotWindow::updateChannelValues);
    connect(m_plot, &RealtimePlotWidget::appearanceChanged, this, &QuickPlotWindow::updateChannelValues);
    setChannelSettingsVisible(false);
    updateChannelSelection();
    return panel;
}

void QuickPlotWindow::setChannelPanelVisible(bool visible)
{
    m_channelPanel->setVisible(visible);
    const QSignalBlocker blocker(m_channelToggleButton);
    m_channelToggleButton->setChecked(visible);
    const QString action = visible ? AppI18n::text("隐藏通道数据") : AppI18n::text("展开通道数据");
    m_channelToggleButton->setAccessibleName(action);
    AppUi::setFluentToolTip(m_channelToggleButton, action);
    if (visible) {
        updateChannelValues();
    } else {
        m_channelValueTimer->stop();
    }
}

void QuickPlotWindow::setChannelSettingsVisible(bool visible)
{
    m_channelSettings->setVisible(visible);
    const QSignalBlocker blocker(m_channelSettingsToggleButton);
    m_channelSettingsToggleButton->setChecked(visible);
    m_channelSettingsToggleButton->setIcon(
        FluentQt::icon(visible ? FluentQt::FluentIcon::ChevronDownMed : FluentQt::FluentIcon::ChevronRightMed));
    const QString action = visible ? AppI18n::text("隐藏通道设置") : AppI18n::text("展开通道设置");
    m_channelSettingsToggleButton->setAccessibleName(action);
    AppUi::setFluentToolTip(m_channelSettingsToggleButton, action);
}

void QuickPlotWindow::updateChannelValues()
{
    if (!m_channelToggleButton->isChecked()) {
        return;
    }
    const QSignalBlocker blocker(m_channelTable);
    for (int index = 0; index < m_channelTable->rowCount(); ++index) {
        auto *name = m_channelTable->item(index, 0);
        name->setCheckState(m_plot->isSeriesVisible(index) ? Qt::Checked : Qt::Unchecked);
        QPixmap swatch(10, 10);
        swatch.fill(m_plot->seriesColor(index));
        name->setIcon(QIcon(swatch));
        const double value = transformedValue(index, m_latestValues.at(index));
        auto *item = m_channelTable->item(index, 1);
        item->setText(valueText(value));
        item->setToolTip(std::isfinite(value) ? QString::number(value, 'g', 16) : QString());
    }
    const int channel = m_channelTable->currentRow();
    const double parsed = channel >= 0 && channel < m_latestValues.size() ? m_latestValues.at(channel) : blankValue();
    m_parsedValueLabel->setText(AppI18n::text("解析值：%1").arg(valueText(parsed)));
}

void QuickPlotWindow::updateChannelSelection()
{
    const int channel = m_channelTable->currentRow();
    const bool selected = channel >= 0 && channel < m_channelNames.size();
    m_channelSettings->setEnabled(selected);
    m_channelSettingsToggleButton->setEnabled(selected);
    const ChannelTransform transform =
        selected ? m_channelTransforms.value(m_channelNames.at(channel)) : ChannelTransform{};
    const QSignalBlocker scaleBlocker(m_channelScaleSpin);
    const QSignalBlocker offsetBlocker(m_channelOffsetSpin);
    m_channelScaleSpin->setValue(transform.scale);
    m_channelOffsetSpin->setValue(transform.offset);
    updateChannelValues();
}

void QuickPlotWindow::applyChannelTransform(double scale, double offset)
{
    const int channel = m_channelTable->currentRow();
    if (channel < 0 || channel >= m_channelNames.size() || !std::isfinite(scale) || !std::isfinite(offset)) {
        return;
    }
    const QString name = m_channelNames.at(channel);
    auto transform = m_channelTransforms.find(name);
    if (transform == m_channelTransforms.end()) {
        if (scale == 1.0 && offset == 0.0) {
            return;
        }
        // Unadjusted series already contain their parsed values, including
        // sparse points older than the frame-based CSV buffer.
        ChannelTransform initial;
        initial.samples = m_plot->points(channel);
        transform = m_channelTransforms.insert(name, initial);
    } else if (transform->scale == scale && transform->offset == offset) {
        return;
    }
    transform->scale = scale;
    transform->offset = offset;
    rebuildPlot(channel);
    if (scale == 1.0 && offset == 0.0) {
        m_channelTransforms.remove(name);
    }
    updateChannelSelection();
}

double QuickPlotWindow::transformedValue(int channel, double value) const
{
    const auto transform = m_channelTransforms.constFind(m_channelNames.at(channel));
    const double result =
        transform == m_channelTransforms.cend() ? value : value * transform->scale + transform->offset;
    return std::isfinite(result) ? result : blankValue();
}

void QuickPlotWindow::rebuildPlot(int channel)
{
    const bool updatesEnabled = m_plot->updatesEnabled();
    const int refreshRate = m_plot->refreshRate();
    const QSignalBlocker blocker(m_plot);
    m_plot->setUpdatesEnabled(false);
    m_plot->setRefreshRate(0);
    m_plot->setSamples(channel, {});
    // Keep parsed samples only for adjusted channels. Replaying one channel
    // preserves the others and lets zero scales and overflows be reversed.
    const auto transform = m_channelTransforms.constFind(m_channelNames.at(channel));
    for (const QPointF &sample : transform->samples) {
        m_plot->appendPoint(channel, sample.x(), transformedValue(channel, sample.y()));
    }
    m_plot->setRefreshRate(refreshRate);
    m_plot->setUpdatesEnabled(updatesEnabled);
}

void QuickPlotWindow::appendRecord(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                                   bool stream)
{
    if (!isPlottingActive() || m_paused) {
        return;
    }

    if (requiresProtocolPayload()) {
        if (!m_hasProtocolTemplate) {
            return;
        }
        for (const auto &decoded : AppProtocol::takeFrames(m_frameBuffer, frame, m_protocolTemplate, stream)) {
            appendParsedValues(timestamp, {}, decoded.bytes, decoded.parsed.payload);
        }
    } else {
        appendParsedValues(timestamp, text, frame);
    }
}

void QuickPlotWindow::appendParsedValues(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                                         const QByteArray &payload)
{
    // Without fields, binary data plots a channel per byte, up to the limit.
    if (m_parserConfig.protocol == AppPlot::Protocol::Binary && m_parserConfig.binaryFields.isEmpty()) {
        const QByteArray &data = m_parserConfig.binarySource == AppPlot::BinarySource::Payload ? payload : frame;
        m_channelsLimited = m_channelsLimited || data.size() > AppPlot::MaximumChannels;
    }
    QString address;
    if (requiresProtocolPayload() && m_protocolTemplate.addressSize > 0) {
        if (m_protocolTemplate.addressOffset > frame.size() - m_protocolTemplate.addressSize)
            return;
        address = bytesToHex(frame.mid(m_protocolTemplate.addressOffset, m_protocolTemplate.addressSize));
    }
    auto samples = AppPlot::extractSamples(m_parserConfig, text, frame, payload);
    for (auto &values : samples) {
        if (!address.isEmpty()) {
            for (auto &value : values)
                value.name = address + QStringLiteral(" / ") + value.name;
        }
        appendValues(timestamp, values);
    }
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
        m_frameBuffer.clear();
        m_channelValueTimer->stop();
        updateChannelValues();
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
        m_latestValues[channelIndex] = plotValue.value;
        auto transform = m_channelTransforms.find(m_channelNames.at(channelIndex));
        if (transform != m_channelTransforms.end() && std::isfinite(plotValue.value)) {
            transform->samples.append(QPointF(sample, plotValue.value));
            const int capacity = m_plot->capacity();
            if (capacity > 0 && transform->samples.size() > capacity) {
                transform->samples.remove(0, transform->samples.size() - capacity);
            }
        }
        m_plot->appendPoint(channelIndex, sample, transformedValue(channelIndex, plotValue.value));
    }

    m_rows.append(row);
    // Bound exported frames by the configured sample capacity (0 keeps all).
    const int capacity = m_plot->capacity();
    while (capacity > 0 && m_rows.size() > capacity) {
        m_rows.removeFirst();
    }
    if (m_channelToggleButton->isChecked() && !m_channelValueTimer->isActive()) {
        m_channelValueTimer->start();
    }
    updateStatus();
}

void QuickPlotWindow::clearData()
{
    m_frameBuffer.clear();
    m_channelValueTimer->stop();
    m_channelTable->setRowCount(0);
    m_rows.clear();
    m_channelNames.clear();
    m_latestValues.clear();
    for (ChannelTransform &transform : m_channelTransforms) {
        transform.samples.clear();
    }
    m_channelCount = 0;
    m_channelsLimited = false;
    m_nextSample = 0;
    if (m_plot) {
        m_plot->clearSeries();
        m_plot->addSeries(QStringLiteral("CH1"));
        m_plot->resetView();
    }
    updateChannelSelection();
    updateStatus();
}

void QuickPlotWindow::resetStream() { m_frameBuffer.clear(); }

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
    m_followTemplateFields = config.protocol == AppPlot::Protocol::Binary &&
                             config.binarySource == AppPlot::BinarySource::Payload && m_hasProtocolTemplate &&
                             (m_followTemplateFields || !m_protocolTemplate.payloadFields.isEmpty());
    if (m_followTemplateFields)
        normalized.binaryFields = m_protocolTemplate.payloadFields;
    for (QString &field : normalized.fields) {
        field = field.simplified();
    }
    normalized.fields.removeAll(QString());
    if (sameParserConfig(m_parserConfig, normalized)) {
        return true;
    }
    m_parserConfig = normalized;
    m_channelTransforms.clear();
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
    if (m_followTemplateFields) {
        const auto matches = [this](const auto &item) { return item.name == m_protocolTemplate.name; };
        const auto previous = std::find_if(m_protocolTemplates.cbegin(), m_protocolTemplates.cend(), matches);
        const auto updated = std::find_if(protocolTemplates.cbegin(), protocolTemplates.cend(), matches);
        // A plot may use an unsaved draft. Only a change to its saved template
        // should replace it; publishing another template must leave it alone.
        if (updated != protocolTemplates.cend() &&
            (previous == m_protocolTemplates.cend() || !(*previous == *updated))) {
            setProtocolTemplate(*updated);
        }
    }
    m_protocolTemplates = protocolTemplates;
}

void QuickPlotWindow::setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    if (m_hasProtocolTemplate && m_protocolTemplate == protocolTemplate) {
        return;
    }
    m_followTemplateFields = !protocolTemplate.payloadFields.isEmpty() ||
                             (m_followTemplateFields && m_protocolTemplate.name == protocolTemplate.name);
    m_protocolTemplate = protocolTemplate;
    m_hasProtocolTemplate = true;
    updateProtocolName();
    if (requiresProtocolPayload()) {
        if (m_followTemplateFields) {
            m_parserConfig.binaryFields = m_protocolTemplate.payloadFields;
            m_parserConfig.fields.removeIf([this](const QString &name) {
                return std::none_of(
                    m_protocolTemplate.payloadFields.cbegin(), m_protocolTemplate.payloadFields.cend(),
                    [&name](const auto &field) { return field.name.compare(name, Qt::CaseInsensitive) == 0; });
            });
        }
        m_channelTransforms.clear();
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
    m_latestValues.append(blankValue());
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
    {
        const QSignalBlocker blocker(m_channelTable);
        while (m_channelTable->rowCount() < count) {
            const int row = m_channelTable->rowCount();
            m_channelTable->insertRow(row);
            auto *name = new QTableWidgetItem(m_channelNames.at(row));
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            name->setCheckState(Qt::Checked);
            name->setToolTip(m_channelNames.at(row));
            m_channelTable->setItem(row, 0, name);
            auto *value = new QTableWidgetItem(valueText(blankValue()));
            value->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_channelTable->setItem(row, 1, value);
        }
    }
    if (m_channelTable->currentRow() < 0) {
        m_channelTable->selectRow(0);
    }
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
    m_frameBuffer.clear();
    m_channelValueTimer->stop();
    updateChannelValues();
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
    const int result = dialog.exec();
    if (result == PlotParserDialog::EditTemplate) {
        emit editTemplateRequested(dialog.protocolTemplate().name);
    } else if (result == QDialog::Accepted) {
        if (dialog.usesProtocolTemplate()) {
            setProtocolTemplate(dialog.protocolTemplate());
        }
        configureParser(dialog.parserConfig());
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
    writeCsv(out);
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

void QuickPlotWindow::writeCsv(QTextStream &out) const
{
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
            const double value =
                index < row.values.size() ? transformedValue(index, row.values.at(index)) : blankValue();
            if (std::isfinite(value)) {
                out << QString::number(value, 'g', 16);
            }
        }
        out << '\n';
    }
}
