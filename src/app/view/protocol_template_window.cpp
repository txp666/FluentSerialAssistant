#include "app/view/protocol_template_window.h"
#include "app/core/app_i18n.h"
#include "app/core/font_preferences.h"
#include "app/core/hex_utils.h"
#include "app/view/muted_text.h"
#include "app/view/protocol_payload_editor.h"
#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtCore/QMimeData>
#include <QtCore/QSignalBlocker>
#include <QtCore/QUrl>
#include <QtGui/QDrag>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QResizeEvent>
#include <QtGui/QShowEvent>
#include <QtGui/QStandardItemModel>
#include <QtGui/QTextDocument>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
constexpr auto PayloadFieldMime = "application/x-fluent-serial-payload-field";

class FieldResultModel final : public QStandardItemModel
{
  public:
    using QStandardItemModel::QStandardItemModel;
    std::function<bool(int, int)> moveField;

    QStringList mimeTypes() const override { return {QLatin1String(PayloadFieldMime)}; }
    Qt::DropActions supportedDropActions() const override { return Qt::MoveAction; }
    QMimeData *mimeData(const QModelIndexList &indexes) const override
    {
        auto *mime = new QMimeData;
        if (!indexes.isEmpty()) {
            const auto index = indexes.first().siblingAtColumn(0);
            if (index.data(Qt::UserRole).toString() == QLatin1String("field"))
                mime->setData(QLatin1String(PayloadFieldMime), index.data(Qt::UserRole + 1).toString().toUtf8());
        }
        return mime;
    }
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int,
                         const QModelIndex &parent) const override
    {
        if (action != Qt::MoveAction || !data->hasFormat(QLatin1String(PayloadFieldMime)) || parent.isValid())
            return false;
        int first = -1, last = -1;
        for (int i = 0; i < rowCount(); ++i) {
            if (index(i, 0).data(Qt::UserRole).toString() == QLatin1String("field")) {
                if (first < 0)
                    first = i;
                last = i;
            }
        }
        const int insertion = row < 0 ? rowCount() : row;
        return first >= 0 && insertion >= first && insertion <= last + 1;
    }
    bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column,
                      const QModelIndex &parent) override
    {
        if (!canDropMimeData(data, action, row, column, parent))
            return false;
        int before = -1;
        for (int i = row < 0 ? rowCount() : row; i < rowCount(); ++i) {
            const auto target = index(i, 0);
            if (target.data(Qt::UserRole).toString() == QLatin1String("field")) {
                before = target.data(Qt::UserRole + 1).toInt();
                break;
            }
        }
        return moveField(data->data(QLatin1String(PayloadFieldMime)).toInt(), before);
    }
};

class FieldResultTable final : public FluentQt::TableView
{
  public:
    using FluentQt::TableView::TableView;

  protected:
    void startDrag(Qt::DropActions) override
    {
        const auto index = currentIndex().siblingAtColumn(0);
        if (index.data(Qt::UserRole).toString() != QLatin1String("field"))
            return;
        QDrag drag(this);
        drag.setMimeData(model()->mimeData({index}));
        drag.setPixmap(viewport()->grab(visualRect(index)));
        // The protocol editor performs the move and rebuilds the preview; the view must not remove source cells.
        drag.exec(Qt::MoveAction);
    }
};

QString byteRange(int offset, int size)
{
    if (size <= 0)
        return QStringLiteral("—");
    return size == 1 ? QString::number(offset) : QStringLiteral("%1–%2").arg(offset).arg(offset + size - 1);
}
struct FieldRow
{
    QString kind, name, type, raw, value, status, details;
    int offset = 0, size = 0, field = -1;
    bool warning = false;
};
QString rowColor(const QString &kind, int field)
{
    if (kind == QLatin1String("field")) {
        const QStringList colors{QStringLiteral("#def3e5"), QStringLiteral("#dff2fb"), QStringLiteral("#eee2fb"),
                                 QStringLiteral("#fff1ba")};
        return colors.at(qMax(0, field) % colors.size());
    }
    if (kind == QLatin1String("header"))
        return QStringLiteral("#dff2fb");
    if (kind == QLatin1String("length"))
        return QStringLiteral("#fff1ba");
    if (kind == QLatin1String("checksum"))
        return QStringLiteral("#ffe5d8");
    if (kind == QLatin1String("trailer"))
        return QStringLiteral("#fbe1ee");
    if (kind == QLatin1String("address"))
        return QStringLiteral("#e8e6f8");
    if (kind == QLatin1String("command"))
        return QStringLiteral("#eee2fb");
    if (kind == QLatin1String("payload"))
        return QStringLiteral("#def3e5");
    return QStringLiteral("#eeeeee");
}
} // namespace

ProtocolTemplateWindow::ProtocolTemplateWindow(QWidget *editor, QWidget *parent, ProtocolPayloadEditor *payloadEditor)
    : QWidget(parent, Qt::Window), m_payloadEditor(payloadEditor), m_internalPayloadEditor(!payloadEditor)
{
    using namespace FluentQt;
    setObjectName(QStringLiteral("protocolTemplateWindow"));
    setWindowTitle(AppI18n::text("帧协议编辑器"));
    setMinimumSize(1000, 720);
    resize(1200, 850);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);
    m_templateTitle = new BodyLabel(AppI18n::text("协议结构与样例解析"), this);
    QFont titleFont = m_templateTitle->font();
    titleFont.setBold(true);
    titleFont.setPixelSize(18);
    m_templateTitle->setFont(titleFont);
    root->addWidget(m_templateTitle);
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    auto *listPanel = new QWidget(splitter);
    auto *listLayout = new QVBoxLayout(listPanel);
    listLayout->setContentsMargins(0, 0, 8, 0);
    listLayout->setSpacing(12);
    auto *sampleCard = new SimpleCardWidget(listPanel);
    auto *sampleLayout = new QVBoxLayout(sampleCard);
    sampleLayout->setContentsMargins(12, 12, 12, 12);
    sampleLayout->setSpacing(6);
    auto *sampleToolbar = new QHBoxLayout;
    sampleToolbar->addWidget(new StrongBodyLabel(AppI18n::text("样例帧 HEX"), sampleCard));
    sampleToolbar->addStretch();
    m_lastFrameButton = new PushButton(AppI18n::text("最新接收"), this);
    m_lastFrameButton->setObjectName(QStringLiteral("protocolTemplateLastFrameButton"));
    m_lastFrameButton->setEnabled(false);
    sampleToolbar->addWidget(m_lastFrameButton);
    m_createPlotButton = new PushButton(AppI18n::text("创建曲线"), sampleCard);
    m_createPlotButton->setObjectName(QStringLiteral("protocolTemplateCreatePlotButton"));
    sampleToolbar->addWidget(m_createPlotButton);
    connect(m_createPlotButton, &PushButton::clicked, this, &ProtocolTemplateWindow::createPlotRequested);
    sampleLayout->addLayout(sampleToolbar);
    m_sampleEdit = new PlainTextEdit(this);
    m_sampleEdit->setObjectName(QStringLiteral("protocolTemplateSampleEdit"));
    m_sampleEdit->setPlaceholderText(AppI18n::text("自动样例随配置更新；粘贴 HEX 后保留，清空可恢复自动生成"));
    m_sampleEdit->setFont(AppFontPreferences::inputMonospaceFont());
    m_sampleEdit->setFixedHeight(48);
    sampleLayout->addWidget(m_sampleEdit);
    m_validationLabel = new CaptionLabel(this);
    m_validationLabel->setObjectName(QStringLiteral("protocolTemplateValidationLabel"));
    m_validationLabel->setWordWrap(true);
    m_validationLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    sampleLayout->addWidget(m_validationLabel);
    m_structureView = new TextBrowser(this);
    m_structureView->setObjectName(QStringLiteral("protocolTemplateStructureView"));
    m_structureView->setFont(AppFontPreferences::inputMonospaceFont());
    m_structureView->setOpenLinks(false);
    m_structureView->setOpenExternalLinks(false);
    m_structureView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_structureView->viewport()->installEventFilter(this);
    sampleLayout->addWidget(m_structureView);
    m_parseStatusLabel = new CaptionLabel(this);
    m_parseStatusLabel->setObjectName(QStringLiteral("protocolTemplateParseStatusLabel"));
    m_parseStatusLabel->setWordWrap(true);
    m_parseStatusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    AppUi::setMutedTextColor(m_parseStatusLabel);
    sampleLayout->addWidget(m_parseStatusLabel);
    listLayout->addWidget(sampleCard);
    const QList<QPair<QString, QString>> frameKinds{
        {QStringLiteral("header"), AppI18n::text("帧头")},      {QStringLiteral("length"), AppI18n::text("长度")},
        {QStringLiteral("address"), AppI18n::text("设备地址")}, {QStringLiteral("command"), AppI18n::text("命令")},
        {QStringLiteral("payload"), AppI18n::text("载荷")},     {QStringLiteral("checksum"), AppI18n::text("校验")},
        {QStringLiteral("trailer"), AppI18n::text("帧尾")}};
    auto *resultsCard = new SimpleCardWidget(listPanel);
    auto *resultsLayout = new QVBoxLayout(resultsCard);
    resultsLayout->setContentsMargins(12, 12, 12, 12);
    resultsLayout->setSpacing(8);
    auto *resultsHeading = new QHBoxLayout;
    resultsHeading->addWidget(new StrongBodyLabel(AppI18n::text("字段解析"), resultsCard));
    resultsHeading->addStretch();
    auto *positionHint = new CaptionLabel(AppI18n::text("拖动数据字段调整顺序与偏移"), resultsCard);
    AppUi::setMutedTextColor(positionHint);
    resultsHeading->addWidget(positionHint);
    resultsLayout->addLayout(resultsHeading);
    auto *fieldButtons = new QHBoxLayout;
    fieldButtons->setSpacing(6);
    fieldButtons->addWidget(new CaptionLabel(AppI18n::text("添加数据"), listPanel));
    const QList<QPair<QString, AppPlot::BinaryType>> types{{QStringLiteral("1 B"), AppPlot::BinaryType::UInt8},
                                                           {QStringLiteral("2 B"), AppPlot::BinaryType::UInt16},
                                                           {QStringLiteral("4 B"), AppPlot::BinaryType::UInt32},
                                                           {QStringLiteral("8 B"), AppPlot::BinaryType::UInt64},
                                                           {QStringLiteral("Float32"), AppPlot::BinaryType::Float32},
                                                           {QStringLiteral("Float64"), AppPlot::BinaryType::Float64}};
    for (int i = 0; i < types.size(); ++i) {
        const auto entry = types.at(i);
        auto *button = new PushButton(entry.first, listPanel);
        button->setFixedHeight(32);
        button->setObjectName(i == 0 ? QStringLiteral("protocolPayloadAddField")
                                     : QStringLiteral("protocolPayloadAdd-") + AppPlot::binaryTypeKey(entry.second));
        button->setAccessibleName(AppI18n::text("添加 %1 字段").arg(entry.first));
        fieldButtons->addWidget(button);
        m_addFieldButtons.append(button);
        connect(button, &PushButton::clicked, this, [this, type = entry.second]() { m_payloadEditor->addField(type); });
    }
    fieldButtons->addSpacing(6);
    m_removeFieldButton = new PushButton(AppI18n::text("删除字段"), listPanel);
    m_removeFieldButton->setObjectName(QStringLiteral("protocolPayloadRemoveField"));
    fieldButtons->addWidget(m_removeFieldButton);
    fieldButtons->addStretch();
    resultsLayout->addLayout(fieldButtons);
    auto *resultModel = new FieldResultModel(this);
    resultModel->moveField = [this](int field, int before) { return m_payloadEditor->moveFieldBefore(field, before); };
    m_resultModel = resultModel;
    m_resultModel->setHorizontalHeaderLabels({AppI18n::text("字段"), AppI18n::text("类型"), AppI18n::text("整帧字节"),
                                              QStringLiteral("HEX"), AppI18n::text("解析值"), AppI18n::text("状态")});
    m_resultTable = new FieldResultTable(listPanel);
    m_resultTable->setObjectName(QStringLiteral("protocolTemplateResultsTable"));
    m_resultTable->setModel(m_resultModel);
    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_resultTable->setDragDropMode(QAbstractItemView::InternalMove);
    m_resultTable->setDragDropOverwriteMode(false);
    m_resultTable->setDefaultDropAction(Qt::MoveAction);
    m_resultTable->setAlternatingRowColors(true);
    m_resultTable->setWordWrap(false);
    m_resultTable->setIconSize(QSize(12, 12));
    m_resultTable->verticalHeader()->hide();
    m_resultTable->verticalHeader()->setDefaultSectionSize(32);
    m_resultTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_resultTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_resultTable->horizontalHeader()->setStyleSheet(
        QStringLiteral("QHeaderView::section { padding-left:16px; padding-right:16px; }"));
    m_resultTable->viewport()->installEventFilter(this);
    resultsLayout->addWidget(m_resultTable, 1);
    listLayout->addWidget(resultsCard, 1);
    auto *configurationPanel = new QWidget(splitter);
    configurationPanel->setObjectName(QStringLiteral("protocolTemplateConfiguration"));
    configurationPanel->setMinimumWidth(330);
    configurationPanel->setMaximumWidth(420);
    auto *configurationLayout = new QVBoxLayout(configurationPanel);
    configurationLayout->setContentsMargins(0, 0, 0, 0);
    configurationLayout->setSpacing(10);
    auto *metadata = editor->findChild<QWidget *>(QStringLiteral("protocolGroup-metadata"));
    if (metadata) {
        editor->layout()->removeWidget(metadata);
        metadata->setParent(configurationPanel);
        configurationLayout->addWidget(metadata);
    }
    if (auto *actionBar = editor->findChild<QWidget *>(QStringLiteral("protocolTemplateActionBar"))) {
        editor->layout()->removeWidget(actionBar);
        if (metadata)
            metadata->layout()->addWidget(actionBar);
        else
            configurationLayout->addWidget(actionBar);
    }
    auto *inspectorCard = new SimpleCardWidget(configurationPanel);
    auto *inspectorLayout = new QVBoxLayout(inspectorCard);
    inspectorLayout->setContentsMargins(12, 12, 12, 12);
    inspectorLayout->setSpacing(12);
    m_sectionCombo = new ComboBox(inspectorCard);
    m_sectionCombo->setObjectName(QStringLiteral("protocolTemplateSectionCombo"));
    m_sectionCombo->setAccessibleName(AppI18n::text("字段"));
    for (const auto &entry : frameKinds)
        m_sectionCombo->addItem(entry.second, QIcon(), entry.first);
    m_sectionCombo->addItem(AppI18n::text("数据字段"), QIcon(), QStringLiteral("field"));
    inspectorLayout->addWidget(m_sectionCombo);
    m_inspector = new QStackedWidget(inspectorCard);
    m_editorScroll = new SingleDirectionScrollArea(Qt::Vertical, m_inspector);
    m_editorScroll->setObjectName(QStringLiteral("protocolTemplateEditorScroll"));
    m_editorScroll->enableTransparentBackground();
    m_editorScroll->setWidgetResizable(true);
    m_editorScroll->setWidget(editor);
    for (const QString &kind : {QStringLiteral("header"), QStringLiteral("length"), QStringLiteral("command"),
                                QStringLiteral("payload"), QStringLiteral("checksum"), QStringLiteral("trailer")}) {
        if (auto *group = editor->findChild<QWidget *>(QStringLiteral("protocolGroup-") + kind))
            m_frameGroups.insert(kind, group);
    }
    m_inspector->addWidget(m_editorScroll);
    auto *payloadScroll = new SingleDirectionScrollArea(Qt::Vertical, m_inspector);
    payloadScroll->enableTransparentBackground();
    payloadScroll->setWidgetResizable(true);
    if (!m_payloadEditor)
        m_payloadEditor = new ProtocolPayloadEditor;
    payloadScroll->setWidget(m_payloadEditor);
    m_inspector->addWidget(payloadScroll);
    inspectorLayout->addWidget(m_inspector);
    configurationLayout->addWidget(inspectorCard, 1);
    splitter->addWidget(listPanel);
    splitter->addWidget(configurationPanel);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({790, 350});
    root->addWidget(splitter, 1);
    connect(m_sectionCombo, &ComboBox::currentIndexChanged, this, [this](int index) {
        const QString kind = m_sectionCombo->itemData(index).toString();
        selectSection(kind, kind == QLatin1String("field") ? m_payloadEditor->selectedField() : -1);
    });
    connect(m_removeFieldButton, &PushButton::clicked, m_payloadEditor, &ProtocolPayloadEditor::removeField);
    connect(m_sampleEdit, &QPlainTextEdit::textChanged, this, [this]() {
        m_generatedSample = m_sampleEdit->toPlainText().trimmed().isEmpty();
        updatePreview();
    });
    connect(m_lastFrameButton, &PushButton::clicked, this, [this]() {
        if (!m_lastFrame.isEmpty())
            setSampleHex(bytesToHex(m_lastFrame));
    });
    connect(m_payloadEditor, &ProtocolPayloadEditor::fieldSelected, this, [this](int field) {
        selectSection(field >= 0 ? QStringLiteral("field") : QStringLiteral("header"), field);
    });
    if (m_internalPayloadEditor)
        connect(m_payloadEditor, &ProtocolPayloadEditor::changed, this, [this]() {
            m_payloadEditor->applyTo(m_protocolTemplate);
            refreshGeneratedSample();
            updatePreview();
        });
    connect(m_resultTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &index) {
                if (!m_refreshing && index.isValid()) {
                    const auto key = m_resultModel->index(index.row(), 0);
                    selectSection(key.data(Qt::UserRole).toString(), key.data(Qt::UserRole + 1).toInt());
                }
            });
    connect(m_structureView, &TextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("field"))
            selectSection(QStringLiteral("field"), url.path().toInt());
        else if (url.scheme() == QLatin1String("section"))
            selectSection(url.path());
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChangedFinished, this, [this]() {
        if (isVisible() && !isMinimized())
            updatePreview();
    });
    m_protocolTemplate = AppProtocol::defaultTemplate();
    generateMatchingSample();
    updateInspector();
}

void ProtocolTemplateWindow::setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    const bool changedName = m_protocolTemplate.name != protocolTemplate.name;
    m_protocolTemplate = protocolTemplate;
    if (m_internalPayloadEditor)
        m_payloadEditor->setProtocolTemplate(protocolTemplate);
    m_payloadEditor->setPayloadOffset(protocolTemplate.payloadOffset);
    if (changedName && !isVisible()) {
        m_selectedKind = protocolTemplate.payloadFields.isEmpty() ? QStringLiteral("header") : QStringLiteral("field");
        m_selectedField = protocolTemplate.payloadFields.isEmpty() ? -1 : 0;
        updateInspector();
    } else if (m_selectedKind == QLatin1String("field") && m_selectedField >= protocolTemplate.payloadFields.size()) {
        m_selectedKind = protocolTemplate.payloadFields.isEmpty() ? QStringLiteral("header") : QStringLiteral("field");
        m_selectedField =
            protocolTemplate.payloadFields.isEmpty() ? -1 : int(protocolTemplate.payloadFields.size()) - 1;
        updateInspector();
    }
    m_templateTitle->setText(AppI18n::text("协议结构与样例解析") + QStringLiteral(" · ") + protocolTemplate.name);
    if (isVisible() && !isMinimized())
        updatePreview();
}
void ProtocolTemplateWindow::setConfigurationError(const QString &error)
{
    if (m_configurationError == error)
        return;
    m_configurationError = error;
    if (isVisible() && !isMinimized())
        updatePreview();
}
void ProtocolTemplateWindow::setSampleHex(const QString &hex)
{
    m_generatedSample = hex.trimmed().isEmpty();
    const QSignalBlocker blocker(m_sampleEdit);
    m_sampleEdit->setPlainText(hex);
    if (isVisible() && !isMinimized())
        updatePreview();
}
QString ProtocolTemplateWindow::sampleHex() const { return m_sampleEdit->toPlainText(); }
void ProtocolTemplateWindow::setLastFrame(const QByteArray &frame)
{
    m_lastFrame = frame;
    m_lastFrameButton->setEnabled(!frame.isEmpty());
}
void ProtocolTemplateWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    updatePreview();
    updateResultColumnWidths();
}
bool ProtocolTemplateWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        const auto *resize = static_cast<QResizeEvent *>(event);
        if (resize->size().width() != resize->oldSize().width()) {
            if (m_resultTable && watched == m_resultTable->viewport())
                updateResultColumnWidths();
            else if (watched == m_structureView->viewport() && m_resultTable)
                updatePreview();
        }
    }
    return QWidget::eventFilter(watched, event);
}
void ProtocolTemplateWindow::updateResultColumnWidths()
{
    QFont hexFont = AppFontPreferences::inputMonospaceFont();
    hexFont.setPixelSize(13);
    const QFontMetrics hexMetrics(hexFont);
    const QFontMetrics headerMetrics(m_resultTable->horizontalHeader()->font());
    QList<int> widths{132, 64, 84, hexMetrics.horizontalAdvance(QStringLiteral("00 00 00 00 00 00 00 00")) + 36,
                      104, 80};
    for (int row = 0; row < m_resultModel->rowCount(); ++row)
        widths[5] =
            qMax(widths[5],
                 m_resultTable->fontMetrics().horizontalAdvance(m_resultModel->index(row, 5).data().toString()) + 48);
    int total = 0;
    for (int column = 0; column < widths.size(); ++column) {
        widths[column] =
            qMax(widths[column],
                 headerMetrics.horizontalAdvance(m_resultModel->headerData(column, Qt::Horizontal).toString()) + 32);
        total += widths[column];
    }
    // Share extra room between the information columns instead of stretching only the field name.
    const int extra = qMax(0, m_resultTable->viewport()->width() - total);
    widths[0] += extra * 3 / 10;
    widths[3] += extra * 4 / 10;
    widths[4] += extra - extra * 3 / 10 - extra * 4 / 10;
    for (int column = 0; column < widths.size(); ++column)
        m_resultTable->setColumnWidth(column, widths[column]);
}
void ProtocolTemplateWindow::generateMatchingSample()
{
    m_generatedSample = true;
    const auto frame = AppProtocol::exampleFrame(m_protocolTemplate);
    const QSignalBlocker blocker(m_sampleEdit);
    m_sampleEdit->setPlainText(bytesToHex(frame));
    updatePreview();
}
void ProtocolTemplateWindow::refreshGeneratedSample()
{
    if (m_generatedSample)
        generateMatchingSample();
}
void ProtocolTemplateWindow::selectSection(const QString &kind, int field)
{
    if (m_refreshing)
        return;
    m_selectedKind = kind;
    m_selectedField = field;
    updateInspector();
    updatePreview();
}
void ProtocolTemplateWindow::updateInspector()
{
    const QSignalBlocker blocker(m_payloadEditor);
    const QSignalBlocker sectionBlocker(m_sectionCombo);
    m_sectionCombo->setCurrentIndex(m_sectionCombo->findData(m_selectedKind));
    if (m_selectedKind == QLatin1String("field") || m_selectedKind == QLatin1String("address")) {
        m_inspector->setCurrentIndex(1);
        if (m_selectedKind == QLatin1String("address"))
            m_payloadEditor->setAddressEditing(true);
        else
            m_payloadEditor->selectPayloadField(m_selectedField);
    } else {
        for (auto it = m_frameGroups.begin(); it != m_frameGroups.end(); ++it)
            it.value()->setVisible(it.key() == m_selectedKind);
        m_inspector->setCurrentWidget(m_editorScroll);
    }
    m_removeFieldButton->setEnabled(m_selectedKind == QLatin1String("field") && m_selectedField >= 0);
}

void ProtocolTemplateWindow::updatePreview()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    for (auto *button : m_addFieldButtons)
        button->setEnabled(m_protocolTemplate.payloadFields.size() < AppPlot::MaximumChannels);
    const auto sample = parseHexPayload(m_sampleEdit->toPlainText());
    const QString configurationError = m_configurationError.isEmpty()
                                           ? AppProtocol::payloadConfigurationError(m_protocolTemplate)
                                           : m_configurationError;
    m_createPlotButton->setEnabled(configurationError.isEmpty() && !m_protocolTemplate.payloadFields.isEmpty());
    if (!sample.ok || !configurationError.isEmpty()) {
        m_resultModel->setRowCount(0);
        const QString error =
            !configurationError.isEmpty()
                ? configurationError
                : AppI18n::text("样例 HEX 无效：%1，位置 %2").arg(sample.errorMessage).arg(sample.errorOffset + 1);
        m_validationLabel->setText(error);
        m_validationLabel->setTextColor(QColor(180, 62, 35), QColor(255, 150, 126));
        m_parseStatusLabel->setText(error);
        m_structureView->clear();
        m_structureView->setFixedHeight(88);
        m_refreshing = false;
        return;
    }
    const QByteArray frame = sample.bytes;
    const auto result = AppProtocol::parseFrame(frame, m_protocolTemplate);
    const auto layout = AppProtocol::frameLayout(frame, m_protocolTemplate);
    const int checksumSize = AppProtocol::checksumSize(m_protocolTemplate);
    const bool valid = result.ok && (!result.checksumChecked || result.checksumValid);
    const qint64 required = AppProtocol::requiredPayloadBytes(m_protocolTemplate);
    const bool fieldsFit = result.ok && result.payload.size() >= required;
    QString validation;
    if (!valid)
        validation = result.ok ? AppI18n::text("样例校验失败，请检查校验设置或重新生成样例。") : result.errorMessage;
    else if (!fieldsFit)
        validation =
            AppI18n::text("样例载荷不足：当前 %1 B，数据字段至少需要 %2 B。").arg(result.payload.size()).arg(required);
    else
        validation = AppI18n::text("样例解析通过 · %1 B 载荷 · %2 个数据字段")
                         .arg(result.payload.size())
                         .arg(m_protocolTemplate.payloadFields.size());
    m_validationLabel->setText(validation);
    m_validationLabel->setTextColor(valid && fieldsFit ? QColor(33, 117, 66) : QColor(180, 62, 35),
                                    valid && fieldsFit ? QColor(128, 217, 162) : QColor(255, 150, 126));
    m_parseStatusLabel->setText(result.ok
                                    ? AppI18n::text("整帧 %1 B · 载荷从字节 %2 开始 · %3")
                                          .arg(result.frameLength)
                                          .arg(m_protocolTemplate.payloadOffset)
                                          .arg(result.checksumChecked ? result.checksumValid ? AppI18n::text("校验正确")
                                                                                             : AppI18n::text("校验错误")
                                                                      : AppI18n::text("无校验"))
                                    : result.errorMessage);
    const auto samples =
        valid ? AppPlot::extractSamples(AppProtocol::payloadParserConfig(m_protocolTemplate), {}, frame, result.payload)
              : QVector<AppPlot::PlotSample>();
    const auto values = samples.isEmpty() ? AppPlot::PlotSample() : samples.first();
    QVector<FieldRow> rows;
    const auto rawBytes = [&frame](int offset, int count) {
        return offset >= 0 && offset < frame.size() ? bytesToHex(frame.mid(offset, qMax(0, count))) : QString();
    };
    const auto addFrameRow = [&](const QString &kind, const QString &name, int offset, int size, const QString &value) {
        if (size <= 0)
            return;
        FieldRow row;
        row.kind = kind;
        row.name = name;
        row.type = QStringLiteral("%1 B").arg(size);
        row.offset = offset;
        row.size = size;
        row.raw = rawBytes(offset, size);
        row.value = value;
        row.warning = offset < 0 || offset > frame.size() - size;
        row.status = row.warning ? AppI18n::text("样例不足") : AppI18n::text("已识别");
        if ((kind == QLatin1String("header") && !frame.startsWith(m_protocolTemplate.header)) ||
            (kind == QLatin1String("trailer") && !row.warning &&
             frame.mid(offset, size) != m_protocolTemplate.trailer)) {
            row.warning = true;
            row.status = AppI18n::text("不匹配");
        }
        rows.append(row);
    };
    addFrameRow(QStringLiteral("header"), AppI18n::text("帧头"), 0, m_protocolTemplate.header.size(),
                AppI18n::text("固定字节"));
    addFrameRow(QStringLiteral("length"), AppI18n::text("长度"), m_protocolTemplate.lengthOffset,
                m_protocolTemplate.lengthSize, QString::number(layout.lengthValue));
    addFrameRow(QStringLiteral("address"), AppI18n::text("设备地址"), m_protocolTemplate.addressOffset,
                m_protocolTemplate.addressSize,
                rawBytes(m_protocolTemplate.addressOffset, m_protocolTemplate.addressSize));
    addFrameRow(QStringLiteral("command"), AppI18n::text("命令"), m_protocolTemplate.commandOffset,
                m_protocolTemplate.commandSize, bytesToHex(result.command));
    if (m_protocolTemplate.payloadFields.isEmpty())
        addFrameRow(QStringLiteral("payload"), AppI18n::text("载荷"), m_protocolTemplate.payloadOffset,
                    int(qMax<qint64>(0, layout.payloadLength)), bytesToHex(result.payload));
    for (int index = 0; index < m_protocolTemplate.payloadFields.size(); ++index) {
        const auto &field = m_protocolTemplate.payloadFields.at(index);
        FieldRow row;
        row.kind = QStringLiteral("field");
        row.field = index;
        row.name = field.name;
        row.type = AppPlot::binaryTypeKey(field.type);
        row.offset = m_protocolTemplate.payloadOffset + field.byteOffset;
        row.size = AppPlot::binaryTypeSize(field.type);
        const int available = qMax(0, qMin(row.size, int(result.payload.size()) - field.byteOffset));
        row.raw = bytesToHex(result.payload.mid(field.byteOffset, available));
        const auto value =
            std::find_if(values.cbegin(), values.cend(), [&field](const auto &v) { return v.name == field.name; });
        if (!valid) {
            row.value = AppI18n::text("无法解析");
            row.status = AppI18n::text("帧无效");
            row.warning = true;
        } else if (available < row.size) {
            row.value = AppI18n::text("无法解析");
            row.status = AppI18n::text("缺少 %1 B").arg(row.size - available);
            row.warning = true;
        } else if (value == values.cend() || !std::isfinite(value->value)) {
            row.value = AppI18n::text("无法解析");
            row.status = AppI18n::text("数值无效");
            row.warning = true;
        } else {
            row.value = QString::number(value->value, 'g', 12);
            row.status = AppI18n::text("已解析");
        }
        rows.append(row);
    }
    addFrameRow(QStringLiteral("checksum"), AppI18n::text("校验"), int(qMax<qint64>(0, layout.checksumOffset)),
                checksumSize,
                result.checksumChecked ? result.checksumValid ? AppI18n::text("正确") : AppI18n::text("错误")
                                       : AppI18n::text("无法解析"));
    if (checksumSize > 0 && result.checksumChecked && valid)
        rows.last().status = AppI18n::text("已校验");
    if (checksumSize > 0 && !rows.isEmpty() && rows.last().kind == QLatin1String("checksum") && !valid) {
        rows.last().warning = true;
        rows.last().status = result.checksumChecked ? AppI18n::text("校验错误") : AppI18n::text("无法校验");
    }
    if (checksumSize > 0 && result.checksumChecked && !rows.isEmpty()) {
        const int start = m_protocolTemplate.checksumStart;
        const int end = int(layout.checksumOffset);
        const auto checksum = AppChecksum::calculate(
            frame.mid(start, end - start), m_protocolTemplate.checksumAlgorithm, m_protocolTemplate.checksumByteOrder);
        rows.last().details = AppI18n::text("预期 %1").arg(bytesToHex(checksum.bytes)) + QStringLiteral(" · ") +
                              AppI18n::text("覆盖 %1").arg(byteRange(start, end - start));
    }
    addFrameRow(QStringLiteral("trailer"), AppI18n::text("帧尾"), int(qMax<qint64>(0, layout.trailerOffset)),
                m_protocolTemplate.trailer.size(), AppI18n::text("固定字节"));
    std::stable_sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
    // Keep indexes and accessibility cells alive while selection callbacks run.
    // Rebuilding the model here can invalidate a native selection query in progress.
    m_resultModel->setRowCount(rows.size());
    int selectedRow = -1;
    for (int index = 0; index < rows.size(); ++index) {
        const auto &row = rows.at(index);
        QList<QStandardItem *> items;
        const QStringList texts{row.name, row.type, byteRange(row.offset, row.size), row.raw, row.value, row.status};
        for (int column = 0; column < texts.size(); ++column) {
            auto *item = m_resultModel->item(index, column);
            if (!item) {
                item = new QStandardItem;
                m_resultModel->setItem(index, column, item);
            }
            item->setText(texts.at(column));
            item->setToolTip(texts.at(column));
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled |
                           (row.kind == QLatin1String("field") ? Qt::ItemIsDragEnabled : Qt::NoItemFlags));
            item->setData(QVariant(), Qt::ForegroundRole);
            if (column == 2 || column == 3) {
                QFont font = AppFontPreferences::inputMonospaceFont();
                font.setPixelSize(13);
                item->setFont(font);
            }
            items.append(item);
        }
        items.first()->setData(row.kind, Qt::UserRole);
        items.first()->setData(row.field, Qt::UserRole + 1);
        const QColor color(rowColor(row.kind, row.field));
        QPixmap swatch(12, 12);
        swatch.fill(color);
        if (row.kind == QLatin1String("field")) {
            QPainter painter(&swatch);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#617d80")));
            for (int y : {2, 5, 8})
                for (int x : {3, 7})
                    painter.drawEllipse(QRect(x, y, 2, 2));
        }
        items.first()->setIcon(QIcon(swatch));
        items.at(3)->setBackground(color);
        items.at(3)->setForeground(QColor(QStringLiteral("#243740")));
        if (!row.details.isEmpty())
            items.at(4)->setToolTip(row.value + QLatin1Char('\n') + row.details);
        if (row.warning)
            items.last()->setForeground(FluentQt::ThemeManager::instance()->effectiveTheme() == FluentQt::Theme::Dark
                                            ? QColor(255, 150, 126)
                                            : QColor(180, 62, 35));
        if (row.kind == m_selectedKind && (row.kind != QLatin1String("field") || row.field == m_selectedField))
            selectedRow = index;
    }
    if (selectedRow >= 0 && (m_resultTable->currentIndex().row() != selectedRow ||
                             !m_resultTable->selectionModel()->isRowSelected(selectedRow)))
        m_resultTable->selectRow(selectedRow);
    else if (selectedRow < 0)
        m_resultTable->setCurrentIndex(QModelIndex());
    const auto ownerAt = [&rows, &layout](int byte) {
        const FieldRow *owner = nullptr;
        for (const auto &row : rows)
            if (byte >= row.offset && byte < row.offset + row.size) {
                if (row.kind == QLatin1String("field") && byte >= layout.checksumOffset)
                    continue;
                if (!owner || row.kind == QLatin1String("field") || row.kind == QLatin1String("address"))
                    owner = &row;
            }
        return owner;
    };
    const auto targetFor = [](const FieldRow *owner) {
        return owner ? owner->kind == QLatin1String("field") ? QStringLiteral("field:%1").arg(owner->field)
                                                             : QStringLiteral("section:") + owner->kind
                     : QStringLiteral("section:payload");
    };
    const int cellWidth = qMax(56, m_structureView->fontMetrics().horizontalAdvance(QStringLiteral("000")) + 16);
    const int availableWidth = qMax(cellWidth, m_structureView->viewport()->width() - 16);
    const int columns = qBound(8, availableWidth / (cellWidth + 3), 32);
    QFont labelFont = font();
    labelFont.setPixelSize(12);
    const QFontMetrics labelMetrics(labelFont);
    const int visibleBytes = qMin<qsizetype>(frame.size(), 1024);
    QString html = QStringLiteral("<table width='%1' cellspacing='3' cellpadding='4'>")
                       .arg(qMin(columns, visibleBytes) * (cellWidth + 3));
    for (int start = 0; start < visibleBytes; start += columns) {
        const int end = qMin(start + columns, visibleBytes);
        html += QStringLiteral("<tr>");
        for (int byte = start; byte < end;) {
            const auto *owner = ownerAt(byte);
            int next = byte + 1;
            while (next < end && ownerAt(next) == owner)
                ++next;
            const auto label = owner ? owner->name : AppI18n::text("其他");
            const auto shortLabel = labelMetrics.elidedText(label, Qt::ElideRight, (next - byte) * cellWidth - 8);
            html += QStringLiteral("<td colspan='%1' align='center' bgcolor='%2'><nobr><a href='%3' "
                                   "title='%4' style=\"text-decoration:none;color:#243740;"
                                   "font-family:'%6';font-size:12px;\">%5</a></nobr></td>")
                        .arg(next - byte)
                        .arg(owner ? rowColor(owner->kind, owner->field) : QStringLiteral("#eeeeee"), targetFor(owner),
                             label.toHtmlEscaped(), shortLabel.toHtmlEscaped(), labelFont.family().toHtmlEscaped());
            byte = next;
        }
        html += QStringLiteral("</tr><tr>");
        for (int byte = start; byte < end; ++byte) {
            const auto *owner = ownerAt(byte);
            const bool selected = owner && owner->kind == m_selectedKind &&
                                  (owner->kind != QLatin1String("field") || owner->field == m_selectedField);
            const auto byteText =
                QStringLiteral("%1").arg(static_cast<uchar>(frame.at(byte)), 2, 16, QLatin1Char('0')).toUpper();
            html += QStringLiteral(
                        "<td width='%6' align='center' bgcolor='%1' style='border:%2px solid #009faa;'><a href='%3' "
                        "style='text-decoration:none;color:#243740;'><small>%4</small><br/><b>%5</b></a></td>")
                        .arg(owner ? rowColor(owner->kind, owner->field) : QStringLiteral("#eeeeee"))
                        .arg(selected ? 2 : 0)
                        .arg(targetFor(owner), QString::number(byte), byteText)
                        .arg(cellWidth);
        }
        if (end - start < columns && start > 0)
            html += QStringLiteral("<td colspan='%1'></td>").arg(columns - (end - start));
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    m_structureView->setHtml(html);
    const int documentHeight = int(std::ceil(m_structureView->document()->size().height()));
    const int viewportMargins = m_structureView->height() - m_structureView->viewport()->height();
    m_structureView->setFixedHeight(qBound(88, documentHeight + viewportMargins, 240));
    m_refreshing = false;
}
