#include "app/view/protocol_template_window.h"

#include "app/core/app_i18n.h"
#include "app/core/hex_utils.h"

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtCore/QSignalBlocker>
#include <QtGui/QFontDatabase>
#include <QtGui/QShowEvent>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

namespace {

struct FrameArea
{
    const char *name;
    QString color;
    QString background;
};

const QList<FrameArea> &frameAreas()
{
    static const QList<FrameArea> areas = {
        {"帧头", QStringLiteral("#176388"), QStringLiteral("#dff2fb")},
        {"长度", QStringLiteral("#775c00"), QStringLiteral("#fff1ba")},
        {"命令", QStringLiteral("#70449a"), QStringLiteral("#eee2fb")},
        {"载荷", QStringLiteral("#21673e"), QStringLiteral("#def3e5")},
        {"校验", QStringLiteral("#a34728"), QStringLiteral("#ffe5d8")},
        {"其他", QStringLiteral("#555555"), QStringLiteral("#eeeeee")},
    };
    return areas;
}

QString byteRange(int offset, int size)
{
    if (size <= 0) {
        return QStringLiteral("—");
    }
    return size == 1 ? QString::number(offset) : QStringLiteral("%1–%2").arg(offset).arg(offset + size - 1);
}

quint32 unsignedField(const QByteArray &frame, const AppProtocol::ProtocolTemplate &item)
{
    quint32 value = 0;
    if (item.lengthOffset < 0 || item.lengthSize <= 0 || item.lengthOffset + item.lengthSize > frame.size()) {
        return 0;
    }
    for (int i = 0; i < item.lengthSize; ++i) {
        const int index = item.lengthByteOrder == AppChecksum::ByteOrder::LittleEndian ? item.lengthSize - i - 1 : i;
        value = (value << 8) | static_cast<unsigned char>(frame.at(item.lengthOffset + index));
    }
    return value;
}

} // namespace

ProtocolTemplateWindow::ProtocolTemplateWindow(QWidget *editor, QWidget *parent) : QWidget(parent, Qt::Window)
{
    using namespace FluentQt;

    setObjectName(QStringLiteral("protocolTemplateWindow"));
    setWindowTitle(AppI18n::text("帧协议编辑器"));
    setMinimumSize(920, 640);
    resize(1100, 760);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);
    m_templateTitle = new BodyLabel(AppI18n::text("协议结构与样例解析"), this);
    QFont titleFont = m_templateTitle->font();
    titleFont.setBold(true);
    titleFont.setPixelSize(18);
    m_templateTitle->setFont(titleFont);
    root->addWidget(m_templateTitle);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    auto *editorScroll = new SingleDirectionScrollArea(Qt::Vertical, splitter);
    editorScroll->setObjectName(QStringLiteral("protocolTemplateEditorScroll"));
    editorScroll->enableTransparentBackground();
    editorScroll->setWidgetResizable(true);
    editorScroll->setMinimumWidth(355);
    editorScroll->setWidget(editor);
    auto *preview = new QWidget(splitter);
    auto *previewLayout = new QVBoxLayout(preview);
    previewLayout->setContentsMargins(8, 0, 0, 0);
    previewLayout->setSpacing(10);

    m_validationLabel = new CaptionLabel(preview);
    m_validationLabel->setObjectName(QStringLiteral("protocolTemplateValidationLabel"));
    m_validationLabel->setWordWrap(true);
    previewLayout->addWidget(m_validationLabel);

    auto *sampleToolbar = new QHBoxLayout;
    sampleToolbar->addWidget(new BodyLabel(AppI18n::text("样例帧 HEX"), preview));
    sampleToolbar->addStretch(1);
    auto *exampleButton = new PushButton(AppI18n::text("内置示例帧"), preview);
    exampleButton->setObjectName(QStringLiteral("protocolTemplateSampleExampleButton"));
    m_lastFrameButton = new PushButton(AppI18n::text("最新接收"), preview);
    m_lastFrameButton->setObjectName(QStringLiteral("protocolTemplateLastFrameButton"));
    m_lastFrameButton->setEnabled(false);
    sampleToolbar->addWidget(exampleButton);
    sampleToolbar->addWidget(m_lastFrameButton);
    previewLayout->addLayout(sampleToolbar);

    m_sampleEdit = new PlainTextEdit(preview);
    m_sampleEdit->setObjectName(QStringLiteral("protocolTemplateSampleEdit"));
    m_sampleEdit->setPlaceholderText(QStringLiteral("AA 55 03 10 01 02 03 4D 6E"));
    m_sampleEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sampleEdit->setMaximumHeight(88);
    m_sampleEdit->setMinimumHeight(70);
    previewLayout->addWidget(m_sampleEdit);

    auto *hint = new CaptionLabel(AppI18n::text("编辑字段或样例即可更新解析；字节位置从 0 开始。"), preview);
    hint->setWordWrap(true);
    previewLayout->addWidget(hint);

    auto *legend = new QHBoxLayout;
    for (const FrameArea &area : frameAreas()) {
        auto *label = new CaptionLabel(AppI18n::text(area.name), preview);
        label->setStyleSheet(QStringLiteral("color:%1;background:%2;border-radius:4px;padding:4px 8px;")
                                 .arg(area.color, area.background));
        legend->addWidget(label);
    }
    legend->addStretch();
    previewLayout->addLayout(legend);

    m_structureView = new TextBrowser(preview);
    m_structureView->setObjectName(QStringLiteral("protocolTemplateStructureView"));
    m_structureView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_structureView->setOpenExternalLinks(false);
    m_structureView->setMinimumHeight(96);
    m_structureView->setMaximumHeight(136);
    previewLayout->addWidget(m_structureView);

    m_parseStatusLabel = new CaptionLabel(preview);
    m_parseStatusLabel->setObjectName(QStringLiteral("protocolTemplateParseStatusLabel"));
    m_parseStatusLabel->setWordWrap(true);
    previewLayout->addWidget(m_parseStatusLabel);

    m_resultModel = new QStandardItemModel(this);
    m_resultModel->setHorizontalHeaderLabels({AppI18n::text("字段"), AppI18n::text("位置"), AppI18n::text("解析值")});
    m_resultTable = new TableView(preview);
    m_resultTable->setObjectName(QStringLiteral("protocolTemplateResultsTable"));
    m_resultTable->setModel(m_resultModel);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->setWordWrap(false);
    m_resultTable->verticalHeader()->hide();
    m_resultTable->verticalHeader()->setDefaultSectionSize(34);
    m_resultTable->horizontalHeader()->setMinimumSectionSize(80);
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_resultTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_resultTable->setColumnWidth(0, 80);
    m_resultTable->setColumnWidth(1, 80);
    m_resultTable->horizontalHeader()->setStretchLastSection(true);
    m_resultTable->setMinimumHeight(200);
    previewLayout->addWidget(m_resultTable, 1);

    splitter->addWidget(editorScroll);
    splitter->addWidget(preview);
    splitter->setSizes({370, 690});
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);
    if (auto *actionBar = editor->findChild<QWidget *>(QStringLiteral("protocolTemplateActionBar"))) {
        if (editor->layout()) {
            editor->layout()->removeWidget(actionBar);
        }
        root->addWidget(actionBar);
    }

    connect(m_sampleEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_manualSample = true;
        updatePreview();
    });
    connect(exampleButton, &PushButton::clicked, this, &ProtocolTemplateWindow::useExampleFrame);
    connect(m_lastFrameButton, &PushButton::clicked, this, [this]() {
        if (m_lastFrame.isEmpty()) {
            return;
        }
        setSampleHex(bytesToHex(m_lastFrame));
    });
    m_protocolTemplate = AppProtocol::defaultTemplate();
    useExampleFrame();
}

void ProtocolTemplateWindow::setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    m_protocolTemplate = protocolTemplate;
    m_templateTitle->setText(AppI18n::text("协议结构与样例解析") + QStringLiteral(" · ") + protocolTemplate.name);
    if (isVisible() && !isMinimized()) {
        updatePreview();
    }
}

void ProtocolTemplateWindow::setConfigurationError(const QString &error)
{
    if (m_configurationError == error) {
        return;
    }
    m_configurationError = error;
    if (isVisible() && !isMinimized()) {
        updatePreview();
    }
}

void ProtocolTemplateWindow::setSampleHex(const QString &hex)
{
    const QSignalBlocker blocker(m_sampleEdit);
    m_sampleEdit->setPlainText(hex);
    m_manualSample = true;
    if (isVisible() && !isMinimized()) {
        updatePreview();
    }
}

QString ProtocolTemplateWindow::sampleHex() const { return m_sampleEdit->toPlainText(); }

void ProtocolTemplateWindow::setLastFrame(const QByteArray &frame)
{
    m_lastFrame = frame;
    m_lastFrameButton->setEnabled(!frame.isEmpty());
    if (m_manualSample || !isVisible() || isMinimized()) {
        return;
    }
    const QSignalBlocker blocker(m_sampleEdit);
    m_sampleEdit->setPlainText(bytesToHex(frame));
    updatePreview();
}

void ProtocolTemplateWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePreview();
}

void ProtocolTemplateWindow::useExampleFrame()
{
    const QSignalBlocker blocker(m_sampleEdit);
    m_sampleEdit->setPlainText(QStringLiteral("AA 55 03 10 01 02 03 4D 6E"));
    m_manualSample = true;
    updatePreview();
}

void ProtocolTemplateWindow::updatePreview()
{
    m_validationLabel->setText(m_configurationError.isEmpty() ? AppI18n::text("配置有效") : m_configurationError);
    m_validationLabel->setTextColor(m_configurationError.isEmpty() ? QColor(33, 117, 66) : QColor(180, 62, 35),
                                    m_configurationError.isEmpty() ? QColor(128, 217, 162) : QColor(255, 150, 126));
    m_resultModel->removeRows(0, m_resultModel->rowCount());
    const HexParseResult sample = parseHexPayload(m_sampleEdit->toPlainText());
    if (!sample.ok || !m_configurationError.isEmpty()) {
        const QString error = !m_configurationError.isEmpty()
                                  ? m_configurationError
                                  : AppI18n::text("样例 HEX 无效：%1，位置 %2")
                                        .arg(sample.errorMessage)
                                        .arg(sample.errorOffset + 1);
        m_parseStatusLabel->setText(error);
        m_parseStatusLabel->setTextColor(QColor(180, 62, 35), QColor(255, 150, 126));
        m_structureView->clear();
        return;
    }

    const QByteArray frame = sample.bytes;
    const AppProtocol::ParseResult result = AppProtocol::parseFrame(frame, m_protocolTemplate);
    const int checksumSize = AppProtocol::checksumSize(m_protocolTemplate);
    const quint32 lengthValue = unsignedField(frame, m_protocolTemplate);
    qint64 expectedFrameLength = frame.size();
    if (m_protocolTemplate.lengthSize > 0 && m_protocolTemplate.lengthMode == AppProtocol::LengthMode::FrameLength) {
        expectedFrameLength = lengthValue;
    } else if (m_protocolTemplate.payloadLength > 0) {
        expectedFrameLength =
            qint64(m_protocolTemplate.payloadOffset) + m_protocolTemplate.payloadLength + checksumSize;
    } else if (m_protocolTemplate.lengthSize > 0) {
        expectedFrameLength = qint64(m_protocolTemplate.payloadOffset) + lengthValue + checksumSize;
    }
    const qint64 checksumOffset = qMax<qint64>(0, expectedFrameLength - checksumSize);
    const qint64 payloadLength = m_protocolTemplate.payloadLength > 0
                                     ? m_protocolTemplate.payloadLength
                                     : qMax<qint64>(0, checksumOffset - m_protocolTemplate.payloadOffset);

    QString html = QStringLiteral("<table cellspacing='4' cellpadding='5'>");
    const int visibleBytes = int(qMin<qsizetype>(frame.size(), 1024));
    for (int i = 0; i < visibleBytes; ++i) {
        if (i % 12 == 0) {
            html += QStringLiteral("<tr>");
        }
        int areaIndex = 5;
        if (checksumSize > 0 && i >= checksumOffset && i < expectedFrameLength) {
            areaIndex = 4;
        } else if (i < m_protocolTemplate.header.size()) {
            areaIndex = 0;
        } else if (m_protocolTemplate.lengthSize > 0 && i >= m_protocolTemplate.lengthOffset &&
                   i < m_protocolTemplate.lengthOffset + m_protocolTemplate.lengthSize) {
            areaIndex = 1;
        } else if (i >= m_protocolTemplate.commandOffset &&
                   i < m_protocolTemplate.commandOffset + m_protocolTemplate.commandSize) {
            areaIndex = 2;
        } else if (i >= m_protocolTemplate.payloadOffset && i < m_protocolTemplate.payloadOffset + payloadLength) {
            areaIndex = 3;
        }
        const FrameArea &area = frameAreas().at(areaIndex);
        const QString byteText =
            QStringLiteral("%1").arg(static_cast<unsigned char>(frame.at(i)), 2, 16, QLatin1Char('0')).toUpper();
        html += QStringLiteral("<td align='center' bgcolor='%1'><font color='%2'>"
                               "<b>%3</b><br/><small>%4</small></font></td>")
                    .arg(area.background, area.color,
                         byteText, QString::number(i));
        if (i % 12 == 11 || i + 1 == visibleBytes) {
            html += QStringLiteral("</tr>");
        }
    }
    html += QStringLiteral("</table>");
    if (frame.size() > visibleBytes) {
        html += AppI18n::text("仅显示前 %1 B；解析使用完整样例。 ").arg(visibleBytes).toHtmlEscaped();
    }
    m_structureView->setHtml(html);

    const bool valid = result.ok && (!result.checksumChecked || result.checksumValid);
    m_parseStatusLabel->setText(result.ok ? result.summary : result.errorMessage);
    m_parseStatusLabel->setTextColor(valid ? QColor(33, 117, 66) : QColor(180, 62, 35),
                                    valid ? QColor(128, 217, 162) : QColor(255, 150, 126));
    const auto addRow = [this](const QString &field, const QString &position, const QString &value) {
        m_resultModel->appendRow({new QStandardItem(field), new QStandardItem(position), new QStandardItem(value)});
    };
    addRow(AppI18n::text("帧头"), byteRange(0, m_protocolTemplate.header.size()),
           bytesToHex(frame.left(m_protocolTemplate.header.size())));
    addRow(AppI18n::text("长度"), byteRange(m_protocolTemplate.lengthOffset, m_protocolTemplate.lengthSize),
           m_protocolTemplate.lengthSize > 0
               ? QString::number(lengthValue) + QStringLiteral(" · ") +
                     AppProtocol::lengthModeLabel(m_protocolTemplate.lengthMode)
               : AppI18n::text("无长度字段"));
    addRow(AppI18n::text("命令"), byteRange(m_protocolTemplate.commandOffset, m_protocolTemplate.commandSize),
           bytesToHex(result.command));
    addRow(AppI18n::text("载荷"),
           byteRange(m_protocolTemplate.payloadOffset, int(qMin<qint64>(payloadLength, 1024 * 1024))),
           bytesToHex(result.payload));
    if (checksumSize > 0) {
        QString checksumValue = bytesToHex(result.checksum);
        if (result.checksumChecked) {
            const AppChecksum::ChecksumResult expected =
                AppChecksum::calculate(frame.left(result.frameLength - checksumSize),
                                       m_protocolTemplate.checksumAlgorithm, m_protocolTemplate.checksumByteOrder);
            checksumValue += QStringLiteral(" · ") + (result.checksumValid ? AppI18n::text("正确") : AppI18n::text("错误"));
            if (expected.ok) {
                checksumValue += QStringLiteral(" · ") + AppI18n::text("预期 %1").arg(bytesToHex(expected.bytes));
            }
        }
        addRow(AppI18n::text("校验"), byteRange(int(qMin<qint64>(checksumOffset, 1024 * 1024)), checksumSize),
               checksumValue);
    } else {
        addRow(AppI18n::text("校验"), QStringLiteral("—"), AppI18n::text("无校验"));
    }
    addRow(AppI18n::text("整帧"), QStringLiteral("0–%1").arg(qMax<qsizetype>(0, frame.size() - 1)),
           AppI18n::text("当前 %1 B / 需要 %2 B").arg(frame.size()).arg(expectedFrameLength));
}
