#include "app/view/protocol_analysis_window.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/font_preferences.h"
#include "app/core/hex_utils.h"
#include "app/view/muted_text.h"

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtCore/QJsonDocument>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtGui/QHideEvent>
#include <QtGui/QPixmap>
#include <QtGui/QResizeEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int MaximumRows = 256;
constexpr int DefaultPanelWidth = 380;
constexpr auto SelectedTemplatesKey = "protocolAnalysis/templates";
constexpr auto CurveKeysKey = "protocolAnalysis/curves";

enum ValueColumn
{
    CurveColumn,
    SourceColumn,
    FieldColumn,
    ReadingColumn,
    UpdatedColumn,
    ValueColumnCount
};

class AnalysisValuesTable final : public FluentQt::TableWidget
{
  public:
    using TableWidget::TableWidget;

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        TableWidget::resizeEvent(event);
        setColumnHidden(UpdatedColumn, width() < 490);
    }
};

QString channelKey(const QString &templateName, const QString &address, const QString &fieldName)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonArray{templateName, address, fieldName}).toJson(QJsonDocument::Compact));
}
} // namespace

ProtocolAnalysisWindow::ProtocolAnalysisWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    using namespace FluentQt;
    setObjectName(QStringLiteral("protocolAnalysisWindow"));
    setWindowTitle(AppI18n::text("协议解析"));
    setMinimumSize(980, 560);
    resize(1280, 720);
    AppSettings settings;
    const auto selected = settings.value(QLatin1String(SelectedTemplatesKey)).toStringList();
    m_selectedTemplates = QSet<QString>(selected.cbegin(), selected.cend());
    const auto curves = settings.value(QLatin1String(CurveKeysKey)).toStringList();
    m_curveKeys = QSet<QString>(curves.cbegin(), curves.cend());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);
    auto *toolbar = new QHBoxLayout;
    m_enabledCheck = new CheckBox(AppI18n::text("启用协议解析"), this);
    m_enabledCheck->setObjectName(QStringLiteral("protocolAnalysisEnabled"));
    m_pauseButton = new PushButton(icon(FluentIcon::Pause), AppI18n::text("暂停"), this);
    m_pauseButton->setObjectName(QStringLiteral("protocolAnalysisPause"));
    auto *clear = new PushButton(icon(FluentIcon::Broom), AppI18n::text("清空"), this);
    clear->setObjectName(QStringLiteral("protocolAnalysisClear"));
    auto *reset = new PushButton(icon(FluentIcon::FitPage), AppI18n::text("复位视图"), this);
    auto *configuration = new ToggleButton(icon(FluentIcon::View), AppI18n::text("数据面板"), this);
    configuration->setObjectName(QStringLiteral("protocolAnalysisConfigurationToggle"));
    configuration->setChecked(true);
    toolbar->addWidget(configuration);
    toolbar->addWidget(m_enabledCheck);
    toolbar->addStretch();
    toolbar->addWidget(m_pauseButton);
    toolbar->addWidget(clear);
    toolbar->addWidget(reset);
    root->addLayout(toolbar);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    auto *panel = new SimpleCardWidget(splitter);
    panel->setObjectName(QStringLiteral("protocolAnalysisConfiguration"));
    panel->setMinimumWidth(322);
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(10, 8, 10, 10);
    panelLayout->setSpacing(6);
    auto *templateBar = new QHBoxLayout;
    auto *templateLabel = new StrongBodyLabel(AppI18n::text("参与解析的模板"), panel);
    auto *edit = new TransparentPushButton(icon(FluentIcon::Edit), AppI18n::text("编辑协议"), panel);
    edit->setObjectName(QStringLiteral("protocolAnalysisEdit"));
    templateBar->addWidget(templateLabel);
    templateBar->addStretch();
    templateBar->addWidget(edit);
    panelLayout->addLayout(templateBar);

    m_templateTable = new TableWidget(panel);
    m_templateTable->setObjectName(QStringLiteral("protocolAnalysisTemplates"));
    m_templateTable->setAccessibleName(templateLabel->text());
    m_templateTable->setColumnCount(1);
    m_templateTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_templateTable->horizontalHeader()->hide();
    m_templateTable->verticalHeader()->hide();
    m_templateTable->verticalHeader()->setDefaultSectionSize(32);
    m_templateTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_templateTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_templateTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_templateTable->setFixedHeight(32);
    panelLayout->addWidget(m_templateTable);

    panelLayout->addSpacing(4);
    m_valueTable = new AnalysisValuesTable(panel);
    m_valueTable->setObjectName(QStringLiteral("protocolAnalysisValues"));
    m_valueTable->setColumnCount(ValueColumnCount);
    m_valueTable->setHorizontalHeaderLabels({AppI18n::text("曲线"), AppI18n::text("来源"), AppI18n::text("字段"),
                                             AppI18n::text("当前值"), AppI18n::text("更新时间")});
    auto *header = m_valueTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(SourceColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(FieldColumn, QHeaderView::Stretch);
    m_valueTable->setColumnWidth(CurveColumn, 46);
    m_valueTable->setColumnWidth(ReadingColumn, 102);
    m_valueTable->setColumnWidth(UpdatedColumn, 118);
    m_valueTable->setColumnHidden(UpdatedColumn, true);
    for (int column : {SourceColumn, FieldColumn})
        m_valueTable->horizontalHeaderItem(column)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const auto cellPadding =
        QStringLiteral("QTableWidget#protocolAnalysisValues::item { padding-left: 12px; padding-right: 10px; }");
    FluentStyleSheet::setCustomStyleSheet(m_valueTable, cellPadding, cellPadding);
    m_valueTable->verticalHeader()->hide();
    m_valueTable->verticalHeader()->setDefaultSectionSize(32);
    m_valueTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_valueTable->setWordWrap(false);
    m_valueTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    panelLayout->addWidget(m_valueTable, 1);

    m_plotStack = new QStackedWidget(splitter);
    m_plot = new RealtimePlotWidget(m_plotStack);
    m_plot->setObjectName(QStringLiteral("protocolAnalysisPlot"));
    m_plot->setCapacity(12000);
    m_plot->setVisibleSpan(10);
    m_plot->setMaximumVisiblePoints(2000);
    m_plot->setLegendVisible(false);
    m_plot->setValueTagsVisible(false);
    m_plot->setFillVisible(false);
    m_plot->setCrosshairVisible(true);
    m_plot->setAutoScroll(true);
    m_plot->setAutoYRange(true);
    m_plot->setSeriesVisible(0, false);
    m_refreshRate = m_plot->refreshRate();
    m_plot->setRefreshRate(0);
    m_plot->setUpdatesEnabled(false);
    m_plotStack->addWidget(m_plot);
    auto *emptyPlot = new SimpleCardWidget(m_plotStack);
    auto *emptyLayout = new QVBoxLayout(emptyPlot);
    auto *plotIcon = new IconWidget(FluentIcon::Market, emptyPlot);
    plotIcon->setFixedSize(30, 30);
    m_plotHint = new BodyLabel(emptyPlot);
    m_plotHint->setWordWrap(true);
    m_plotHint->setAlignment(Qt::AlignCenter);
    AppUi::setMutedTextColor(m_plotHint);
    emptyLayout->addStretch();
    emptyLayout->addWidget(plotIcon, 0, Qt::AlignHCenter);
    emptyLayout->addWidget(m_plotHint);
    emptyLayout->addStretch();
    m_plotStack->addWidget(emptyPlot);

    splitter->addWidget(panel);
    splitter->addWidget(m_plotStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({DefaultPanelWidth, width() - DefaultPanelWidth});
    root->addWidget(splitter, 1);
    connect(configuration, &PushButton::toggled, this, [this, panel](bool visible) {
        panel->setVisible(visible);
        m_plot->setLegendVisible(!visible);
        m_plot->setValueTagsVisible(!visible);
    });
    m_status = new CaptionLabel(this);
    m_status->setObjectName(QStringLiteral("protocolAnalysisStatus"));
    m_status->setWordWrap(true);
    AppUi::setMutedTextColor(m_status);
    root->addWidget(m_status);
    m_valueTimer = new QTimer(this);
    m_valueTimer->setSingleShot(true);
    m_valueTimer->setInterval(50);
    connect(m_valueTimer, &QTimer::timeout, this, &ProtocolAnalysisWindow::updateValues);
    connect(m_enabledCheck, &CheckBox::toggled, this, [this](bool enabled) {
        updateActivity();
        emit parsingEnabledChanged(enabled);
    });
    connect(m_pauseButton, &PushButton::clicked, this, [this]() {
        m_paused = !m_paused;
        m_pauseButton->setText(m_paused ? AppI18n::text("继续") : AppI18n::text("暂停"));
        m_pauseButton->setIcon(icon(m_paused ? FluentIcon::Play : FluentIcon::Pause));
        updateActivity();
    });
    connect(clear, &PushButton::clicked, this, &ProtocolAnalysisWindow::clearData);
    connect(reset, &PushButton::clicked, m_plot, &RealtimePlotWidget::resetView);
    connect(edit, &PushButton::clicked, this, [this]() {
        const int row = m_templateTable->currentRow();
        emit editTemplateRequested(row >= 0 && row < m_templates.size() ? m_templates.at(row).name : QString());
    });
    connect(m_templateTable, &TableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        const QString name = m_templates.at(item->row()).name;
        if (item->checkState() == Qt::Checked) {
            m_selectedTemplates.insert(name);
        } else {
            m_selectedTemplates.remove(name);
        }
        AppSettings settings;
        settings.setValue(QLatin1String(SelectedTemplatesKey), m_selectedTemplates.values());
        resetChannels();
    });
    connect(m_valueTable, &TableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item->column() == CurveColumn) {
            setCurveVisible(item->row(), item->checkState() == Qt::Checked);
        }
    });
    updateStatus();
}

void ProtocolAnalysisWindow::setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &templates,
                                                  const QString &selectedTemplate)
{
    const bool changed = m_templates != templates;
    const bool added = !selectedTemplate.isEmpty() && !m_selectedTemplates.contains(selectedTemplate);
    if (!changed && !added) {
        return;
    }
    m_templates = templates;
    if (!selectedTemplate.isEmpty()) {
        m_selectedTemplates.insert(selectedTemplate);
    }
    const QSignalBlocker blocker(m_templateTable);
    m_templateTable->setRowCount(0);
    for (int row = 0; row < m_templates.size(); ++row) {
        const auto &item = m_templates.at(row);
        m_templateTable->insertRow(row);
        auto *name = new QTableWidgetItem(item.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        name->setCheckState(m_selectedTemplates.contains(item.name) ? Qt::Checked : Qt::Unchecked);
        name->setToolTip(item.name);
        m_templateTable->setItem(row, 0, name);
        if (item.name == selectedTemplate) {
            m_templateTable->selectRow(row);
        }
    }
    m_templateTable->setFixedHeight(m_templateTable->verticalHeader()->defaultSectionSize() *
                                    qBound(1, int(m_templates.size()), 3));
    resetChannels();
}

void ProtocolAnalysisWindow::setParsingEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_enabledCheck);
    m_enabledCheck->setChecked(enabled);
    updateActivity();
}

bool ProtocolAnalysisWindow::isCapturing() const
{
    return isVisible() && !isMinimized() && !m_paused && m_enabledCheck->isChecked();
}

void ProtocolAnalysisWindow::updateActivity()
{
    const bool active = isCapturing();
    m_plot->setRefreshRate(active ? m_refreshRate : 0);
    m_plot->setUpdatesEnabled(active);
    if (!active) {
        m_streamBuffers.clear();
        m_valueTimer->stop();
    }
    updateValues();
}

void ProtocolAnalysisWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updateActivity();
}

void ProtocolAnalysisWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    updateActivity();
}

void ProtocolAnalysisWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateActivity();
    }
}

void ProtocolAnalysisWindow::resetChannels()
{
    const auto isSelected = [this](const auto &item) { return m_selectedTemplates.contains(item.name); };
    const auto first = std::find_if(m_templates.cbegin(), m_templates.cend(), isSelected);
    const bool singleTemplate = first != m_templates.cend() && std::none_of(first + 1, m_templates.cend(), isSelected);
    m_addressOnly = singleTemplate && first->addressSize > 0;
    m_valueTable->horizontalHeaderItem(SourceColumn)->setText(AppI18n::text(m_addressOnly ? "地址" : "来源"));
    auto *header = m_valueTable->horizontalHeader();
    header->setSectionResizeMode(SourceColumn, m_addressOnly ? QHeaderView::Fixed : QHeaderView::Stretch);
    if (m_addressOnly)
        m_valueTable->setColumnWidth(SourceColumn, qMax(68, 36 + 21 * first->addressSize));

    m_channels.clear();
    m_channelIndexes.clear();
    m_valueTable->setRowCount(0);
    m_plot->clearSeries();
    m_plot->setSeriesVisible(0, false);
    m_allocatedSeries = 0;
    m_limited = false;
    clearData();
    for (const auto &item : m_templates) {
        if (isSelected(item) && item.addressSize == 0) {
            for (const auto &field : item.payloadFields) {
                channelFor(item.name, {}, field.name);
            }
        }
    }
    updateValues();
}

void ProtocolAnalysisWindow::resetStream() { m_streamBuffers.clear(); }

void ProtocolAnalysisWindow::clearData()
{
    m_streamBuffers.clear();
    m_plot->clear();
    m_startTime = {};
    m_lastX = 0;
    m_frameCount = 0;
    for (auto &channel : m_channels) {
        channel.value = std::numeric_limits<double>::quiet_NaN();
        channel.timestamp = {};
        channel.x = 0;
    }
    updateValues();
}

int ProtocolAnalysisWindow::channelFor(const QString &templateName, const QString &address, const QString &fieldName)
{
    const QString key = channelKey(templateName, address, fieldName);
    const auto existing = m_channelIndexes.constFind(key);
    if (existing != m_channelIndexes.cend()) {
        return existing.value();
    }
    if (m_channels.size() >= MaximumRows) {
        m_limited = true;
        return -1;
    }
    const int row = m_channels.size();
    const QString source = address.isEmpty() ? templateName : address + QStringLiteral(" · ") + templateName;
    Channel channel;
    channel.key = key;
    channel.label = source + QStringLiteral(" / ") + fieldName;
    channel.value = std::numeric_limits<double>::quiet_NaN();
    m_channels.append(channel);
    m_channelIndexes.insert(key, row);
    {
        const QSignalBlocker blocker(m_valueTable);
        m_valueTable->insertRow(row);
        for (int column = 0; column < m_valueTable->columnCount(); ++column) {
            m_valueTable->setItem(row, column, new QTableWidgetItem);
        }
        auto *check = m_valueTable->item(row, CurveColumn);
        check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        check->setCheckState(m_curveKeys.contains(key) ? Qt::Checked : Qt::Unchecked);
        m_valueTable->item(row, SourceColumn)->setText(m_addressOnly ? address : source);
        m_valueTable->item(row, SourceColumn)->setToolTip(source);
        m_valueTable->item(row, FieldColumn)->setText(fieldName);
        m_valueTable->item(row, FieldColumn)->setToolTip(fieldName);
        QFont numberFont = AppFontPreferences::inputMonospaceFont();
        numberFont.setPixelSize(13);
        for (int column : {ReadingColumn, UpdatedColumn}) {
            m_valueTable->item(row, column)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_valueTable->item(row, column)->setFont(numberFont);
        }
    }
    if (m_curveKeys.contains(key)) {
        setCurveVisible(row, true);
    }
    return row;
}

void ProtocolAnalysisWindow::setCurveVisible(int row, bool visible)
{
    auto &channel = m_channels[row];
    const QSignalBlocker blocker(m_valueTable);
    int checked = 0;
    for (int index = 0; index < m_valueTable->rowCount(); ++index) {
        checked += m_valueTable->item(index, CurveColumn)->checkState() == Qt::Checked;
    }
    if (visible && checked > AppPlot::MaximumChannels) {
        m_valueTable->item(row, CurveColumn)->setCheckState(Qt::Unchecked);
        m_limited = true;
        updateStatus();
        return;
    }
    if (visible && channel.series < 0) {
        channel.series = m_allocatedSeries++;
        if (channel.series >= m_plot->seriesCount()) {
            m_plot->addSeries(channel.label);
        } else {
            m_plot->setSeriesName(channel.series, channel.label);
        }
        if (channel.timestamp.isValid()) {
            m_plot->appendPoint(channel.series, channel.x, channel.value);
        }
        QPixmap swatch(10, 10);
        swatch.fill(m_plot->seriesColor(channel.series));
        m_valueTable->item(row, FieldColumn)->setIcon(QIcon(swatch));
    }
    if (channel.series >= 0) {
        m_plot->setSeriesVisible(channel.series, visible);
    }
    if (visible) {
        m_curveKeys.insert(channel.key);
    } else {
        m_curveKeys.remove(channel.key);
    }
    AppSettings settings;
    settings.setValue(QLatin1String(CurveKeysKey), m_curveKeys.values());
    updateStatus();
}

void ProtocolAnalysisWindow::appendRecord(const QDateTime &timestamp, const QByteArray &bytes, bool stream)
{
    if (!isCapturing() || bytes.isEmpty()) {
        return;
    }
    for (const auto &item : m_templates) {
        if (!m_selectedTemplates.contains(item.name) || !AppProtocol::payloadConfigurationError(item).isEmpty()) {
            continue;
        }
        for (const auto &frame : AppProtocol::takeFrames(m_streamBuffers[item.name], bytes, item, stream)) {
            appendFrame(timestamp, frame.bytes, item, frame.parsed);
        }
    }
    if (!m_valueTimer->isActive()) {
        m_valueTimer->start();
    }
}

void ProtocolAnalysisWindow::appendFrame(const QDateTime &timestamp, const QByteArray &frame,
                                         const AppProtocol::ProtocolTemplate &item,
                                         const AppProtocol::ParseResult &parsed)
{
    if (item.addressSize > 0 && item.addressOffset > frame.size() - item.addressSize) {
        return;
    }
    const QString address =
        item.addressSize > 0 ? bytesToHex(frame.mid(item.addressOffset, item.addressSize)) : QString();
    const auto samples = AppPlot::extractSamples(AppProtocol::payloadParserConfig(item), {}, frame, parsed.payload);
    const AppPlot::PlotSample values = samples.isEmpty() ? AppPlot::PlotSample() : samples.first();
    if (!m_startTime.isValid()) {
        m_startTime = timestamp;
    }
    m_lastX = qMax(m_lastX, m_startTime.msecsTo(timestamp) / 1000.0);
    ++m_frameCount;
    // Clear absent fields in shorter frames instead of leaving an older value
    // looking like a measurement from this frame.
    QStringList names;
    if (item.payloadFields.isEmpty()) {
        for (const auto &value : values) {
            names.append(value.name);
        }
    } else {
        for (const auto &field : item.payloadFields) {
            names.append(field.name);
        }
    }
    for (const auto &name : names) {
        const int row = channelFor(item.name, address, name);
        if (row < 0) {
            continue;
        }
        auto &channel = m_channels[row];
        const auto value =
            std::find_if(values.cbegin(), values.cend(), [&name](const auto &v) { return v.name == name; });
        channel.value = value == values.cend() ? std::numeric_limits<double>::quiet_NaN() : value->value;
        channel.timestamp = timestamp;
        channel.x = m_lastX;
        if (channel.series >= 0) {
            m_plot->appendPoint(channel.series, channel.x, channel.value);
        }
    }
}

void ProtocolAnalysisWindow::updateValues()
{
    const QSignalBlocker blocker(m_valueTable);
    const QString updatedLabel = AppI18n::text("更新时间");
    for (int row = 0; row < m_channels.size(); ++row) {
        const auto &channel = m_channels.at(row);
        const bool hasValue = std::isfinite(channel.value);
        const bool hasTime = channel.timestamp.isValid();
        const auto time = hasTime ? channel.timestamp.toString(QStringLiteral("HH:mm:ss.zzz")) : QStringLiteral("—");
        auto *reading = m_valueTable->item(row, ReadingColumn);
        reading->setText(hasValue ? QString::number(channel.value, 'g', 12) : QStringLiteral("—"));
        reading->setToolTip(
            hasTime ? QStringLiteral("%1\n%2 %3")
                          .arg(hasValue ? QString::number(channel.value, 'g', 16) : reading->text(), updatedLabel, time)
                    : QString());
        m_valueTable->item(row, UpdatedColumn)->setText(time);
    }
    updateStatus();
}

void ProtocolAnalysisWindow::updateStatus()
{
    QString state = !m_enabledCheck->isChecked() ? AppI18n::text("协议模板未启用")
                    : m_paused                   ? AppI18n::text("已暂停")
                                                 : AppI18n::text("实时");
    QString text =
        AppI18n::text("%1 · %2 帧 · %3 个数据字段 · 横轴为秒").arg(state).arg(m_frameCount).arg(m_channels.size());
    if (m_limited) {
        text += QStringLiteral(" · ") +
                AppI18n::text("最多显示 %1 个字段，同时绘制 %2 条曲线").arg(MaximumRows).arg(AppPlot::MaximumChannels);
    }
    m_status->setText(text);

    const bool plotting = m_plot->sampleCount() > 0 &&
                          std::any_of(m_channels.cbegin(), m_channels.cend(),
                                      [this](const Channel &channel) { return m_curveKeys.contains(channel.key); });
    if (plotting) {
        m_plotStack->setCurrentWidget(m_plot);
        return;
    }
    const bool templateSelected = std::any_of(m_templates.cbegin(), m_templates.cend(), [this](const auto &item) {
        return m_selectedTemplates.contains(item.name);
    });
    if (!m_enabledCheck->isChecked()) {
        m_plotHint->setText(AppI18n::text("启用协议解析以接收数据"));
    } else if (m_paused) {
        m_plotHint->setText(AppI18n::text("点击“继续”恢复采集"));
    } else if (!templateSelected) {
        m_plotHint->setText(AppI18n::text("在数据面板中勾选要解析的模板"));
    } else if (m_frameCount == 0 || m_channels.isEmpty()) {
        m_plotHint->setText(AppI18n::text("等待接收所选协议的数据"));
    } else {
        m_plotHint->setText(AppI18n::text("在数据面板中勾选要绘制的字段"));
    }
    m_plotStack->setCurrentWidget(m_plotHint->parentWidget());
}
