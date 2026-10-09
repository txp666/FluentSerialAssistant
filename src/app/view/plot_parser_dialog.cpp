#include "app/view/plot_parser_dialog.h"

#include "app/core/app_i18n.h"
#include "app/core/hex_utils.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QRegularExpression>
#include <QtCore/QSignalBlocker>
#include <QtGui/QFontDatabase>
#include <QtGui/QIcon>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

QStringList splitFields(const QString &text)
{
    QStringList fields;
    const QStringList entries = text.split(QRegularExpression(QStringLiteral(R"([,;\r\n]+)")), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
        const QString field = entry.simplified();
        if (!field.isEmpty() && !fields.contains(field, Qt::CaseInsensitive)) {
            fields.append(field);
        }
    }
    return fields;
}

QWidget *createComboCell(QWidget *parent, const std::function<void(FluentQt::ComboBox *)> &configure)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *combo = new FluentQt::ComboBox(cell);
    combo->setFixedHeight(32);
    configure(combo);
    layout->addWidget(combo);
    return cell;
}

QWidget *createTypeCell(AppPlot::BinaryType selected, QWidget *parent)
{
    return createComboCell(parent, [selected](FluentQt::ComboBox *combo) {
        for (const QString &key :
             {QStringLiteral("u8"), QStringLiteral("i8"), QStringLiteral("u16"), QStringLiteral("i16"),
              QStringLiteral("u32"), QStringLiteral("i32"), QStringLiteral("u64"), QStringLiteral("i64"),
              QStringLiteral("f32"), QStringLiteral("f64")}) {
            combo->addItem(key, QIcon(), key);
        }
        combo->setCurrentIndex(combo->findData(AppPlot::binaryTypeKey(selected)));
    });
}

QWidget *createByteOrderCell(AppPlot::ByteOrder selected, QWidget *parent)
{
    return createComboCell(parent, [selected](FluentQt::ComboBox *combo) {
        combo->addItem(AppI18n::text("小端"), QIcon(), QStringLiteral("little"));
        combo->addItem(AppI18n::text("大端"), QIcon(), QStringLiteral("big"));
        combo->setCurrentIndex(combo->findData(AppPlot::byteOrderKey(selected)));
    });
}

FluentQt::ComboBox *comboFromCell(const QTableWidget *table, int row, int column)
{
    QWidget *cell = table->cellWidget(row, column);
    return cell ? cell->findChild<FluentQt::ComboBox *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

QTableWidgetItem *editableItem(const QString &text) { return new QTableWidgetItem(text); }

QString protocolName(AppPlot::Protocol protocol)
{
    switch (protocol) {
    case AppPlot::Protocol::Delimited: return AppI18n::text("分隔值");
    case AppPlot::Protocol::KeyValue: return AppI18n::text("键值对");
    case AppPlot::Protocol::Json: return AppI18n::text("JSON 对象");
    case AppPlot::Protocol::Binary: return AppI18n::text("二进制字段");
    default: return AppI18n::text("全部数字");
    }
}

QString protocolHint(AppPlot::Protocol protocol)
{
    switch (protocol) {
    case AppPlot::Protocol::Delimited:
        return AppI18n::text("逗号、分号或空格分隔的数字，每行对应一个采样点。");
    case AppPlot::Protocol::KeyValue:
        return AppI18n::text("读取 name=value 或 name: value，支持中文和含空格的字段名。");
    case AppPlot::Protocol::Json:
        return AppI18n::text("递归读取 JSON 对象和数组中的数字，字段使用路径名称。");
    case AppPlot::Protocol::Binary:
        return AppI18n::text("按字节偏移读取数值，换算结果 = 原始值 × 比例 + 加值。");
    default:
        return AppI18n::text("提取文本每行中的全部数字，按顺序生成 CH1、CH2 等通道。");
    }
}

QByteArray templateExample(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    const int payloadSize = protocolTemplate.payloadLength > 0 ? protocolTemplate.payloadLength : 4;
    const qint64 checksumOffset = static_cast<qint64>(protocolTemplate.payloadOffset) + payloadSize;
    const int checksumSize = AppProtocol::checksumSize(protocolTemplate);
    if (protocolTemplate.payloadOffset < 0 || checksumOffset > 4096 || checksumOffset <= 0 ||
        protocolTemplate.header.size() > checksumOffset || protocolTemplate.lengthOffset < 0 ||
        protocolTemplate.lengthOffset + protocolTemplate.lengthSize > checksumOffset ||
        protocolTemplate.commandOffset < 0 ||
        protocolTemplate.commandOffset + protocolTemplate.commandSize > checksumOffset) {
        return {};
    }
    QByteArray frame(static_cast<int>(checksumOffset), '\0');
    frame.replace(0, protocolTemplate.header.size(), protocolTemplate.header);
    if (protocolTemplate.commandSize > 0) {
        frame[protocolTemplate.commandOffset] = '\x01';
    }
    const QByteArray payload = QByteArray::fromHex("6400C800");
    for (int index = 0; index < payloadSize; ++index) {
        frame[protocolTemplate.payloadOffset + index] = payload.at(index % payload.size());
    }
    quint32 length = protocolTemplate.lengthMode == AppProtocol::LengthMode::FrameLength
                         ? frame.size() + checksumSize : payloadSize;
    for (int index = 0; index < protocolTemplate.lengthSize; ++index) {
        const int byteIndex = protocolTemplate.lengthByteOrder == AppChecksum::ByteOrder::LittleEndian
                                  ? index : protocolTemplate.lengthSize - index - 1;
        frame[protocolTemplate.lengthOffset + byteIndex] = static_cast<char>(length & 0xff);
        length >>= 8;
    }
    if (checksumSize > 0) {
        const AppChecksum::ChecksumResult checksum = AppChecksum::calculate(
            frame, protocolTemplate.checksumAlgorithm, protocolTemplate.checksumByteOrder);
        if (!checksum.ok) {
            return {};
        }
        frame.append(checksum.bytes);
    }
    return frame;
}

} // namespace

PlotParserDialog::PlotParserDialog(const AppPlot::ParserConfig &config, QWidget *parent, bool requireProtocolChoice)
    : QDialog(parent), m_config(config)
{
    using namespace FluentQt;

    setObjectName(QStringLiteral("plotProtocolDialog"));
    setWindowTitle(AppI18n::text("绘图协议"));
    setModal(true);
    resize(940, 730);
    setMinimumSize(820, 650);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *description = new BodyLabel(requireProtocolChoice
        ? AppI18n::text("选择本窗口的数据协议，用样例确认解析结果后创建曲线。")
        : AppI18n::text("本窗口独立使用以下协议；修改协议会清空已有曲线。"), this);
    description->setWordWrap(true);
    root->addWidget(description);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    m_protocolCombo = new ComboBox(this);
    m_protocolCombo->setObjectName(QStringLiteral("plotProtocolCombo"));
    m_protocolCombo->addItem(AppI18n::text("请选择协议"), QIcon(), QString());
    for (const QString &key : AppPlot::supportedProtocolKeys()) {
        AppPlot::Protocol protocol;
        AppPlot::protocolFromKey(key, &protocol);
        m_protocolCombo->addItem(protocolName(protocol), QIcon(), key);
    }
    m_protocolCombo->setCurrentIndex(requireProtocolChoice ? 0 : m_protocolCombo->findData(AppPlot::protocolKey(config.protocol)));
    form->addRow(AppI18n::text("协议类型"), m_protocolCombo);
    m_fieldsEdit = new LineEdit(this);
    m_fieldsEdit->setObjectName(QStringLiteral("plotFieldsEdit"));
    m_fieldsEdit->setText(config.fields.join(QStringLiteral(", ")));
    m_fieldsEdit->setPlaceholderText(AppI18n::text("留空显示全部；多个字段用逗号分隔"));
    form->addRow(AppI18n::text("字段筛选"), m_fieldsEdit);
    root->addLayout(form);
    m_protocolHint = new CaptionLabel(QString(), this);
    m_protocolHint->setWordWrap(true);
    root->addWidget(m_protocolHint);

    m_binaryContainer = new QWidget(this);
    auto *binaryLayout = new QVBoxLayout(m_binaryContainer);
    binaryLayout->setContentsMargins(0, 0, 0, 0);
    binaryLayout->setSpacing(10);

    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new BodyLabel(AppI18n::text("二进制来源"), m_binaryContainer));
    m_binarySourceCombo = new ComboBox(m_binaryContainer);
    m_binarySourceCombo->setObjectName(QStringLiteral("plotBinarySourceCombo"));
    m_binarySourceCombo->addItem(AppI18n::text("完整帧"), QIcon(), QStringLiteral("frame"));
    m_binarySourceCombo->addItem(AppI18n::text("协议载荷"), QIcon(), QStringLiteral("payload"));
    m_binarySourceCombo->setCurrentIndex(m_binarySourceCombo->findData(AppPlot::binarySourceKey(config.binarySource)));
    sourceRow->addWidget(m_binarySourceCombo);
    sourceRow->addStretch(1);
    binaryLayout->addLayout(sourceRow);

    m_templateContainer = new QWidget(m_binaryContainer);
    auto *templateLayout = new QHBoxLayout(m_templateContainer);
    templateLayout->setContentsMargins(0, 0, 0, 0);
    templateLayout->addWidget(new BodyLabel(AppI18n::text("帧模板"), m_templateContainer));
    m_templateCombo = new ComboBox(m_templateContainer);
    m_templateCombo->setObjectName(QStringLiteral("plotFrameTemplateCombo"));
    m_templateCombo->addItem(AppI18n::text("请选择帧模板"), QIcon(), -1);
    templateLayout->addWidget(m_templateCombo, 1);
    binaryLayout->addWidget(m_templateContainer);

    auto *binaryHint = new CaptionLabel(
        AppI18n::text("字段留空时按字节生成通道。协议载荷使用本窗口选择的帧模板，并验证帧校验。"), m_binaryContainer);
    binaryHint->setWordWrap(true);
    binaryLayout->addWidget(binaryHint);

    m_binaryFieldsTable = new TableWidget(m_binaryContainer);
    m_binaryFieldsTable->setObjectName(QStringLiteral("plotBinaryFieldsTable"));
    m_binaryFieldsTable->setBorderVisible(true);
    m_binaryFieldsTable->setBorderRadius(8);
    m_binaryFieldsTable->setWordWrap(false);
    m_binaryFieldsTable->setColumnCount(6);
    m_binaryFieldsTable->setHorizontalHeaderLabels({AppI18n::text("名称"), AppI18n::text("字节偏移"),
                                                    AppI18n::text("类型"), AppI18n::text("字节序"),
                                                    AppI18n::text("比例"), AppI18n::text("加值")});
    auto *binaryHeader = m_binaryFieldsTable->horizontalHeader();
    binaryHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    binaryHeader->setSectionResizeMode(1, QHeaderView::Fixed);
    binaryHeader->setSectionResizeMode(2, QHeaderView::Fixed);
    binaryHeader->setSectionResizeMode(3, QHeaderView::Fixed);
    binaryHeader->setSectionResizeMode(4, QHeaderView::Fixed);
    binaryHeader->setSectionResizeMode(5, QHeaderView::Fixed);
    binaryHeader->resizeSection(1, 108);
    binaryHeader->resizeSection(2, 108);
    binaryHeader->resizeSection(3, 120);
    binaryHeader->resizeSection(4, 108);
    binaryHeader->resizeSection(5, 108);
    m_binaryFieldsTable->verticalHeader()->setVisible(false);
    m_binaryFieldsTable->verticalHeader()->setDefaultSectionSize(40);
    m_binaryFieldsTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_binaryFieldsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_binaryFieldsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_binaryFieldsTable->setMinimumHeight(140);
    m_binaryFieldsTable->setMaximumHeight(210);
    binaryLayout->addWidget(m_binaryFieldsTable);

    for (const AppPlot::BinaryField &field : config.binaryFields) {
        addBinaryFieldRow(field);
    }

    auto *fieldButtons = new QHBoxLayout;
    auto *addFieldButton =
        new PushButton(FluentQt::icon(FluentIcon::Add), AppI18n::text("添加字段"), m_binaryContainer);
    auto *removeFieldButton =
        new PushButton(FluentQt::icon(FluentIcon::Delete), AppI18n::text("删除选中"), m_binaryContainer);
    fieldButtons->addWidget(addFieldButton);
    fieldButtons->addWidget(removeFieldButton);
    fieldButtons->addStretch(1);
    binaryLayout->addLayout(fieldButtons);
    connect(addFieldButton, &PushButton::clicked, this, &PlotParserDialog::addDefaultBinaryField);
    connect(removeFieldButton, &PushButton::clicked, this, &PlotParserDialog::removeSelectedBinaryFields);

    root->addWidget(m_binaryContainer);

    auto *previewLayout = new QHBoxLayout;
    previewLayout->setSpacing(14);
    auto *inputLayout = new QVBoxLayout;
    auto *inputToolbar = new QHBoxLayout;
    inputToolbar->addWidget(new BodyLabel(AppI18n::text("样例输入"), this));
    m_sampleModeCombo = new ComboBox(this);
    m_sampleModeCombo->setObjectName(QStringLiteral("plotSampleModeCombo"));
    m_sampleModeCombo->addItem(AppI18n::text("文本"), QIcon(), QStringLiteral("text"));
    m_sampleModeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
    m_sampleModeCombo->setFixedWidth(100);
    inputToolbar->addWidget(m_sampleModeCombo);
    inputToolbar->addStretch(1);
    auto *exampleButton = new PushButton(AppI18n::text("填入示例"), this);
    exampleButton->setObjectName(QStringLiteral("plotLoadExampleButton"));
    inputToolbar->addWidget(exampleButton);
    inputLayout->addLayout(inputToolbar);
    m_sampleEdit = new PlainTextEdit(this);
    m_sampleEdit->setObjectName(QStringLiteral("plotSampleEdit"));
    m_sampleEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sampleEdit->setPlaceholderText(AppI18n::text("粘贴设备数据，实时查看字段和值"));
    m_sampleEdit->setMinimumHeight(150);
    inputLayout->addWidget(m_sampleEdit, 1);
    previewLayout->addLayout(inputLayout, 1);

    auto *resultLayout = new QVBoxLayout;
    auto *resultToolbar = new QHBoxLayout;
    resultToolbar->addWidget(new BodyLabel(AppI18n::text("解析预览"), this));
    resultToolbar->addStretch(1);
    auto *useFieldsButton = new PushButton(AppI18n::text("使用选中字段"), this);
    useFieldsButton->setObjectName(QStringLiteral("plotUseFieldsButton"));
    resultToolbar->addWidget(useFieldsButton);
    resultLayout->addLayout(resultToolbar);
    m_previewTable = new TableWidget(this);
    m_previewTable->setObjectName(QStringLiteral("plotPreviewTable"));
    m_previewTable->setBorderVisible(true);
    m_previewTable->setBorderRadius(8);
    m_previewTable->setColumnCount(3);
    m_previewTable->setHorizontalHeaderLabels({AppI18n::text("采样"), AppI18n::text("字段"), AppI18n::text("值")});
    m_previewTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_previewTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_previewTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_previewTable->verticalHeader()->setVisible(false);
    m_previewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_previewTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_previewTable->setMinimumHeight(150);
    resultLayout->addWidget(m_previewTable, 1);
    previewLayout->addLayout(resultLayout, 1);
    root->addLayout(previewLayout, 1);
    m_previewStatus = new CaptionLabel(QString(), this);
    m_previewStatus->setObjectName(QStringLiteral("plotPreviewStatus"));
    m_previewStatus->setWordWrap(true);
    root->addWidget(m_previewStatus);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancelButton = new PushButton(AppI18n::text("取消"), this);
    m_applyButton = new PrimaryPushButton(requireProtocolChoice ? AppI18n::text("创建曲线") : AppI18n::text("应用"), this);
    m_applyButton->setObjectName(QStringLiteral("plotApplyButton"));
    buttons->addWidget(cancelButton);
    buttons->addWidget(m_applyButton);
    root->addLayout(buttons);
    connect(cancelButton, &PushButton::clicked, this, &QDialog::reject);
    connect(m_applyButton, &PrimaryPushButton::clicked, this, &PlotParserDialog::accept);
    connect(m_protocolCombo, &ComboBox::currentIndexChanged, this, [this](int) { updateProtocolControls(); });
    connect(m_binarySourceCombo, &ComboBox::currentIndexChanged, this, [this](int) { updateProtocolControls(); });
    connect(m_templateCombo, &ComboBox::currentIndexChanged, this, [this](int) { updatePreview(); });
    connect(m_fieldsEdit, &LineEdit::textChanged, this, [this]() { updatePreview(); });
    connect(m_sampleModeCombo, &ComboBox::currentIndexChanged, this, [this](int) { updatePreview(); });
    connect(m_sampleEdit, &PlainTextEdit::textChanged, this, &PlotParserDialog::updatePreview);
    connect(m_binaryFieldsTable, &QTableWidget::itemChanged, this, [this]() { updatePreview(); });
    connect(exampleButton, &PushButton::clicked, this, &PlotParserDialog::loadExample);
    connect(useFieldsButton, &PushButton::clicked, this, &PlotParserDialog::useSelectedFields);
    updateProtocolControls();
    if (!requireProtocolChoice && !usesProtocolTemplate()) {
        loadExample();
    }
}

AppPlot::ParserConfig PlotParserDialog::parserConfig() const { return m_config; }

void PlotParserDialog::setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &protocolTemplates)
{
    const bool hadSelection = m_templateCombo->currentData().toInt() >= 0;
    const QJsonObject previous = AppProtocol::toJson(protocolTemplate());
    m_protocolTemplates = protocolTemplates;
    const QSignalBlocker blocker(m_templateCombo);
    m_templateCombo->clear();
    m_templateCombo->addItem(AppI18n::text("请选择帧模板"), QIcon(), -1);
    int selectedIndex = 0;
    for (int index = 0; index < m_protocolTemplates.size(); ++index) {
        const AppProtocol::ProtocolTemplate &item = m_protocolTemplates.at(index);
        m_templateCombo->addItem(item.name, QIcon(), index);
        if (hadSelection && AppProtocol::toJson(item) == previous) {
            selectedIndex = index + 1;
        }
    }
    m_templateCombo->setCurrentIndex(selectedIndex);
    updatePreview();
}

void PlotParserDialog::setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    const QJsonObject selected = AppProtocol::toJson(protocolTemplate);
    int index = -1;
    for (int candidate = 0; candidate < m_protocolTemplates.size(); ++candidate) {
        if (AppProtocol::toJson(m_protocolTemplates.at(candidate)) == selected) {
            index = candidate;
            break;
        }
    }
    if (index < 0) {
        m_protocolTemplates.append(protocolTemplate);
        index = m_protocolTemplates.size() - 1;
        const bool hasSameName = std::any_of(m_protocolTemplates.cbegin(), m_protocolTemplates.cend() - 1,
            [&protocolTemplate](const AppProtocol::ProtocolTemplate &item) {
                return item.name == protocolTemplate.name;
            });
        m_templateCombo->addItem(hasSameName ? protocolTemplate.name + AppI18n::text("（当前窗口）")
                                            : protocolTemplate.name, QIcon(), index);
    }
    m_templateCombo->setCurrentIndex(index + 1);
    if (usesProtocolTemplate() && m_sampleEdit->toPlainText().trimmed().isEmpty()) {
        loadExample();
    }
}

bool PlotParserDialog::usesProtocolTemplate() const
{
    return m_protocolCombo->currentData().toString() == QStringLiteral("binary") &&
           m_binarySourceCombo->currentData().toString() == QStringLiteral("payload");
}

AppProtocol::ProtocolTemplate PlotParserDialog::protocolTemplate() const
{
    const int index = m_templateCombo->currentData().toInt();
    return index >= 0 && index < m_protocolTemplates.size() ? m_protocolTemplates.at(index)
                                                         : AppProtocol::ProtocolTemplate();
}

void PlotParserDialog::accept()
{
    updatePreview();
    AppPlot::ParserConfig config;
    QString error;
    if (!collectConfig(&config, &error) || !m_applyButton->isEnabled()) {
        return;
    }
    m_config = config;
    QDialog::accept();
}

bool PlotParserDialog::collectConfig(AppPlot::ParserConfig *config, QString *error) const
{
    *config = m_config;
    if (!AppPlot::protocolFromKey(m_protocolCombo->currentData().toString(), &config->protocol)) {
        *error = AppI18n::text("请先选择协议类型。");
        return false;
    }
    config->fields = splitFields(m_fieldsEdit->text());
    if (config->protocol == AppPlot::Protocol::Binary) {
        AppPlot::binarySourceFromKey(m_binarySourceCombo->currentData().toString(), &config->binarySource);
        if (!collectBinaryFields(&config->binaryFields, error)) {
            return false;
        }
        if (usesProtocolTemplate()) {
            const int index = m_templateCombo->currentData().toInt();
            if (index < 0 || index >= m_protocolTemplates.size()) {
                *error = AppI18n::text("请选择用于提取载荷的帧模板。");
                return false;
            }
        }
    }
    return true;
}

void PlotParserDialog::updateProtocolControls()
{
    AppPlot::Protocol protocol;
    const bool selected = AppPlot::protocolFromKey(m_protocolCombo->currentData().toString(), &protocol);
    const bool binary = selected && protocol == AppPlot::Protocol::Binary;
    m_binaryContainer->setVisible(binary);
    m_templateContainer->setVisible(binary && usesProtocolTemplate());
    m_protocolHint->setText(selected ? protocolHint(protocol) : AppI18n::text("每个曲线窗口都需要单独选择协议。"));
    m_fieldsEdit->setEnabled(selected);
    if (binary) {
        const QSignalBlocker blocker(m_sampleModeCombo);
        m_sampleModeCombo->setCurrentIndex(m_sampleModeCombo->findData(QStringLiteral("hex")));
    }
    updatePreview();
}

void PlotParserDialog::updatePreview()
{
    if (m_updatingPreview || !m_previewTable) {
        return;
    }
    m_updatingPreview = true;
    m_previewTable->setRowCount(0);
    AppPlot::ParserConfig config;
    QString error;
    const bool configValid = collectConfig(&config, &error);
    m_applyButton->setEnabled(configValid);
    if (!configValid) {
        m_previewStatus->setText(error);
        m_updatingPreview = false;
        return;
    }

    const QString sample = m_sampleEdit->toPlainText();
    if (sample.trimmed().isEmpty()) {
        m_previewStatus->setText(AppI18n::text("可粘贴样例或填入示例，确认字段与数值。"));
        m_updatingPreview = false;
        return;
    }
    QByteArray frame;
    QString text = sample;
    if (m_sampleModeCombo->currentData().toString() == QStringLiteral("hex")) {
        const HexParseResult parsed = parseHexPayload(sample);
        if (!parsed.ok) {
            m_previewStatus->setText(AppI18n::text("HEX 输入无效：%1").arg(parsed.errorMessage));
            m_applyButton->setEnabled(false);
            m_updatingPreview = false;
            return;
        }
        frame = parsed.bytes;
        text = QString::fromUtf8(frame);
    } else {
        frame = sample.toUtf8();
    }
    QByteArray payload;
    QString frameStatus;
    if (usesProtocolTemplate()) {
        const AppProtocol::ParseResult parsed = AppProtocol::parseFrame(frame, protocolTemplate());
        if (!parsed.ok || (parsed.checksumChecked && !parsed.checksumValid)) {
            m_previewStatus->setText(parsed.ok ? AppI18n::text("帧校验错误，请检查样例或模板。") : parsed.errorMessage);
            m_applyButton->setEnabled(false);
            m_updatingPreview = false;
            return;
        }
        payload = parsed.payload;
        frameStatus = parsed.summary + QStringLiteral("\n");
    }
    const QVector<AppPlot::PlotSample> samples = AppPlot::extractSamples(config, text, frame, payload);
    constexpr int MaximumPreviewRows = 256;
    int totalValues = 0;
    for (int sampleIndex = 0; sampleIndex < samples.size(); ++sampleIndex) {
        for (const AppPlot::PlotValue &value : samples.at(sampleIndex)) {
            ++totalValues;
            if (m_previewTable->rowCount() >= MaximumPreviewRows) {
                continue;
            }
            const int row = m_previewTable->rowCount();
            m_previewTable->insertRow(row);
            m_previewTable->setItem(row, 0, editableItem(QString::number(sampleIndex + 1)));
            m_previewTable->setItem(row, 1, editableItem(value.name));
            m_previewTable->setItem(row, 2, editableItem(QString::number(value.value, 'g', 14)));
        }
    }
    QString result = samples.isEmpty()
        ? AppI18n::text("未找到匹配的数字字段，请检查样例、字段筛选或字节偏移。")
        : AppI18n::text("%1 个采样 · %2 个字段值").arg(samples.size()).arg(totalValues);
    if (totalValues > MaximumPreviewRows) {
        result += AppI18n::text(" · 预览前 %1 项").arg(MaximumPreviewRows);
    }
    m_previewStatus->setText(frameStatus + result);
    m_updatingPreview = false;
}

void PlotParserDialog::loadExample()
{
    AppPlot::Protocol protocol;
    if (!AppPlot::protocolFromKey(m_protocolCombo->currentData().toString(), &protocol)) {
        return;
    }
    QString example;
    switch (protocol) {
    case AppPlot::Protocol::Delimited:
        example = QStringLiteral("24.8, 60.5, 101.3\n25.1, 61.2, 101.2");
        break;
    case AppPlot::Protocol::KeyValue:
        example = QStringLiteral("temperature=24.8 humidity=60.5\ntemperature=25.1 humidity=61.2");
        break;
    case AppPlot::Protocol::Json:
        example = QStringLiteral("{\"sensor\":{\"temperature\":24.8,\"humidity\":60.5},\"pressure\":101.3}");
        break;
    case AppPlot::Protocol::Binary:
        example = bytesToHex(usesProtocolTemplate() ? templateExample(protocolTemplate())
                                                   : QByteArray::fromHex("6400C80000004841"));
        break;
    default:
        example = QStringLiteral("24.8 60.5 101.3\n25.1 61.2 101.2");
        break;
    }
    {
        const QSignalBlocker blocker(m_sampleModeCombo);
        m_sampleModeCombo->setCurrentIndex(m_sampleModeCombo->findData(
            protocol == AppPlot::Protocol::Binary ? QStringLiteral("hex") : QStringLiteral("text")));
    }
    m_sampleEdit->setPlainText(example);
    updatePreview();
}

void PlotParserDialog::useSelectedFields()
{
    QStringList fields = splitFields(m_fieldsEdit->text());
    for (const QModelIndex &row : m_previewTable->selectionModel()->selectedRows()) {
        const QString field = m_previewTable->item(row.row(), 1)->text();
        if (!fields.contains(field, Qt::CaseInsensitive)) {
            fields.append(field);
        }
    }
    m_fieldsEdit->setText(fields.join(QStringLiteral(", ")));
}

void PlotParserDialog::addBinaryFieldRow(const AppPlot::BinaryField &field)
{
    const QSignalBlocker blocker(m_binaryFieldsTable);
    const int row = m_binaryFieldsTable->rowCount();
    m_binaryFieldsTable->insertRow(row);
    m_binaryFieldsTable->setItem(row, 0, editableItem(field.name));
    m_binaryFieldsTable->setItem(row, 1, editableItem(QString::number(field.byteOffset)));
    m_binaryFieldsTable->setCellWidget(row, 2, createTypeCell(field.type, m_binaryFieldsTable));
    m_binaryFieldsTable->setCellWidget(row, 3, createByteOrderCell(field.byteOrder, m_binaryFieldsTable));
    m_binaryFieldsTable->setItem(row, 4, editableItem(QString::number(field.scale, 'g', 12)));
    m_binaryFieldsTable->setItem(row, 5, editableItem(QString::number(field.add, 'g', 12)));
    for (int column : {2, 3}) {
        if (auto *combo = comboFromCell(m_binaryFieldsTable, row, column)) {
            connect(combo, &FluentQt::ComboBox::currentIndexChanged, this, [this](int) { updatePreview(); });
        }
    }
}

void PlotParserDialog::addDefaultBinaryField()
{
    AppPlot::BinaryField field;
    field.name = QStringLiteral("field_%1").arg(m_binaryFieldsTable->rowCount() + 1);
    field.type = AppPlot::BinaryType::UInt16;
    addBinaryFieldRow(field);
    updatePreview();
}

void PlotParserDialog::removeSelectedBinaryFields()
{
    QModelIndexList rows = m_binaryFieldsTable->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &left, const QModelIndex &right) { return left.row() > right.row(); });
    for (const QModelIndex &index : rows) {
        m_binaryFieldsTable->removeRow(index.row());
    }
    updatePreview();
}

bool PlotParserDialog::collectBinaryFields(QVector<AppPlot::BinaryField> *fields, QString *error) const
{
    fields->clear();
    QStringList names;
    for (int row = 0; row < m_binaryFieldsTable->rowCount(); ++row) {
        AppPlot::BinaryField field;
        field.name = m_binaryFieldsTable->item(row, 0)->text().simplified();
        if (field.name.isEmpty()) {
            *error = AppI18n::text("第 %1 行缺少字段名称").arg(row + 1);
            return false;
        }
        if (names.contains(field.name, Qt::CaseInsensitive)) {
            *error = AppI18n::text("第 %1 行的字段名称重复").arg(row + 1);
            return false;
        }
        names.append(field.name);
        bool offsetOk = false;
        field.byteOffset = m_binaryFieldsTable->item(row, 1)->text().toInt(&offsetOk);
        if (!offsetOk || field.byteOffset < 0) {
            *error = AppI18n::text("第 %1 行的字节偏移无效").arg(row + 1);
            return false;
        }
        const auto *typeCombo = comboFromCell(m_binaryFieldsTable, row, 2);
        const auto *orderCombo = comboFromCell(m_binaryFieldsTable, row, 3);
        if (!typeCombo || !AppPlot::binaryTypeFromKey(typeCombo->currentData().toString(), &field.type) ||
            !orderCombo || !AppPlot::byteOrderFromKey(orderCombo->currentData().toString(), &field.byteOrder)) {
            *error = AppI18n::text("第 %1 行的类型或字节序无效").arg(row + 1);
            return false;
        }
        bool scaleOk = false;
        bool addOk = false;
        field.scale = m_binaryFieldsTable->item(row, 4)->text().toDouble(&scaleOk);
        field.add = m_binaryFieldsTable->item(row, 5)->text().toDouble(&addOk);
        if (!scaleOk || !addOk || !std::isfinite(field.scale) || !std::isfinite(field.add)) {
            *error = AppI18n::text("第 %1 行的比例或加值无效").arg(row + 1);
            return false;
        }
        fields->append(field);
    }
    return true;
}
