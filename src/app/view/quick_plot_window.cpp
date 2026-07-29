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
#include <QtCore/QSignalBlocker>
#include <QtCore/QTextStream>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QSizePolicy>
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
    setMinimumSize(760, 460);
    resize(980, 620);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    auto *protocolLabel = new BodyLabel(AppI18n::text("协议"), this);
    protocolLabel->setFixedHeight(32);
    m_protocolCombo = new ComboBox(this);
    m_protocolCombo->addItem(AppI18n::text("全部数字"), QIcon(), QStringLiteral("numbers"));
    m_protocolCombo->addItem(AppI18n::text("分隔值"), QIcon(), QStringLiteral("delimited"));
    m_protocolCombo->addItem(AppI18n::text("键值对"), QIcon(), QStringLiteral("keyValue"));
    m_protocolCombo->addItem(AppI18n::text("JSON 对象"), QIcon(), QStringLiteral("json"));
    m_protocolCombo->addItem(AppI18n::text("二进制字段"), QIcon(), QStringLiteral("binary"));
    m_protocolCombo->setFixedSize(132, 32);
    AppUi::setFluentToolTip(m_protocolCombo, AppI18n::text("选择接收数据如何转换为曲线数据"));
    AppSettings settings;
    const QString savedConfig = settings.value(QStringLiteral("plot/parserConfig")).toString();
    QJsonParseError savedConfigError;
    const QJsonDocument savedConfigDocument = QJsonDocument::fromJson(savedConfig.toUtf8(), &savedConfigError);
    QString configError;
    if (savedConfigError.error != QJsonParseError::NoError || !savedConfigDocument.isObject() ||
        !AppPlot::parserConfigFromJson(savedConfigDocument.object(), &m_parserConfig, &configError)) {
        const QString savedProtocol =
            settings.value(QStringLiteral("plot/protocol"), QStringLiteral("numbers")).toString();
        AppPlot::protocolFromKey(savedProtocol, &m_parserConfig.protocol);
    }
    const int protocolIndex = m_protocolCombo->findData(AppPlot::protocolKey(m_parserConfig.protocol));
    m_protocolCombo->setCurrentIndex(protocolIndex >= 0 ? protocolIndex : 0);
    auto *protocolHelpButton = new TransparentToolButton(icon(FluentIcon::Question), this);
    AppUi::setFluentToolTip(protocolHelpButton, AppI18n::text("绘图协议示例"));
    protocolHelpButton->setFixedSize(32, 32);
    protocolHelpButton->setIconSize(QSize(16, 16));
    auto *parserSettingsButton = new PushButton(icon(FluentIcon::Setting), AppI18n::text("解析设置"), this);
    parserSettingsButton->setFixedHeight(32);

    m_pauseButton = new PushButton(icon(FluentIcon::Pause), AppI18n::text("暂停"), this);
    m_pauseButton->setFixedHeight(32);
    auto *clearButton = new PushButton(icon(FluentIcon::Broom), AppI18n::text("清空"), this);
    clearButton->setFixedHeight(32);
    auto *resetButton = new PushButton(icon(FluentIcon::FitPage), AppI18n::text("复位视图"), this);
    resetButton->setFixedHeight(32);
    auto *exportButton = new PushButton(icon(FluentIcon::ImageExport), AppI18n::text("导出 CSV"), this);
    exportButton->setFixedHeight(32);

    m_statusLabel = new CaptionLabel(QString(), this);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    toolbar->addWidget(protocolLabel, 0, Qt::AlignVCenter);
    toolbar->addWidget(m_protocolCombo, 0, Qt::AlignVCenter);
    toolbar->addWidget(protocolHelpButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(parserSettingsButton, 0, Qt::AlignVCenter);
    toolbar->addSpacing(4);
    toolbar->addWidget(m_pauseButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(clearButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(resetButton, 0, Qt::AlignVCenter);
    toolbar->addWidget(exportButton, 0, Qt::AlignVCenter);
    toolbar->addStretch(1);
    toolbar->addWidget(m_statusLabel, 1, Qt::AlignVCenter);
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

    connect(m_protocolCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        AppPlot::ParserConfig config = m_parserConfig;
        AppPlot::protocolFromKey(m_protocolCombo->currentData().toString(), &config.protocol);
        config.fields.clear();
        configureParser(config);
    });
    connect(protocolHelpButton, &TransparentToolButton::clicked, this,
            [this, protocolHelpButton]() { showProtocolHelp(protocolHelpButton); });
    connect(parserSettingsButton, &PushButton::clicked, this, &QuickPlotWindow::showParserSettings);
    connect(m_pauseButton, &PushButton::clicked, this, [this]() { setPaused(!m_paused); });
    connect(clearButton, &PushButton::clicked, this, &QuickPlotWindow::clearData);
    connect(resetButton, &PushButton::clicked, m_plot, &RealtimePlotWidget::resetView);
    connect(exportButton, &PushButton::clicked, this, &QuickPlotWindow::exportCsv);

    updateStatus();
}

void QuickPlotWindow::appendRecord(const QDateTime &timestamp, const QString &text, const QByteArray &frame,
                                   const QByteArray &payload, bool ignorePause)
{
    if (m_paused && !ignorePause) {
        return;
    }

    const QVector<AppPlot::PlotSample> samples = AppPlot::extractSamples(m_parserConfig, text, frame, payload);
    for (const AppPlot::PlotSample &values : samples) {
        appendValues(timestamp, values);
    }
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

bool QuickPlotWindow::configureParser(const AppPlot::ParserConfig &config)
{
    const QString key = AppPlot::protocolKey(config.protocol);
    const int index = m_protocolCombo ? m_protocolCombo->findData(key) : -1;
    if (index < 0) {
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
    {
        const QSignalBlocker blocker(m_protocolCombo);
        m_protocolCombo->setCurrentIndex(index);
    }
    m_parserConfig = normalized;
    AppSettings settings;
    settings.setValue(QStringLiteral("plot/protocol"), key);
    settings.setValue(
        QStringLiteral("plot/parserConfig"),
        QString::fromUtf8(QJsonDocument(AppPlot::parserConfigToJson(m_parserConfig)).toJson(QJsonDocument::Compact)));
    clearData();
    emit protocolChanged();
    return true;
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
    if (dialog.exec() == QDialog::Accepted) {
        configureParser(dialog.parserConfig());
    }
}

void QuickPlotWindow::showProtocolHelp(QWidget *target)
{
    const QString content =
        AppI18n::text("全部数字：提取每行里的所有数字\n  T=24.8 H=60.5  => CH1=24.8, "
                      "CH2=60.5\n\n分隔值：读取逗号、分号、空格分隔的纯数字\n  24.8,60.5,101.3  => CH1, CH2, "
                      "CH3\n\n键值对：支持多词或中文字段名\n  free sram: 43815 minimal sram: 38791\n\nJSON "
                      "对象：递归读取对象和数组中的数值\n  {\"system\":{\"free\":43815}} => system.free\n\n"
                      "二进制字段：默认按字节绘图，可在解析设置中配置类型、字节序、比例和加值");
    FluentQt::TeachingTip::create(AppI18n::text("绘图协议示例"), content,
                                  FluentQt::icon(FluentQt::FluentIcon::Question), QPixmap(), true, target,
                                  FluentQt::TeachingTipTailPosition::Bottom, -1, this);
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
