#include "app/view/plot_parser_dialog.h"

#include "app/core/app_i18n.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QRegularExpression>
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

} // namespace

PlotParserDialog::PlotParserDialog(const AppPlot::ParserConfig &config, QWidget *parent)
    : QDialog(parent), m_config(config)
{
    using namespace FluentQt;

    setWindowTitle(AppI18n::text("解析设置"));
    setModal(true);
    resize(config.protocol == AppPlot::Protocol::Binary ? QSize(820, 500) : QSize(620, 230));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *description = new BodyLabel(AppI18n::text("字段筛选留空时显示全部解析结果。"), this);
    description->setWordWrap(true);
    root->addWidget(description);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    m_fieldsEdit = new LineEdit(this);
    m_fieldsEdit->setText(config.fields.join(QStringLiteral(", ")));
    m_fieldsEdit->setPlaceholderText(AppI18n::text("例如：free sram, minimal sram"));
    form->addRow(AppI18n::text("字段筛选"), m_fieldsEdit);
    root->addLayout(form);

    m_binaryContainer = new QWidget(this);
    auto *binaryLayout = new QVBoxLayout(m_binaryContainer);
    binaryLayout->setContentsMargins(0, 0, 0, 0);
    binaryLayout->setSpacing(10);

    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(new BodyLabel(AppI18n::text("二进制来源"), m_binaryContainer));
    m_binarySourceCombo = new ComboBox(m_binaryContainer);
    m_binarySourceCombo->addItem(AppI18n::text("完整帧"), QIcon(), QStringLiteral("frame"));
    m_binarySourceCombo->addItem(AppI18n::text("协议载荷"), QIcon(), QStringLiteral("payload"));
    m_binarySourceCombo->setCurrentIndex(m_binarySourceCombo->findData(AppPlot::binarySourceKey(config.binarySource)));
    sourceRow->addWidget(m_binarySourceCombo);
    sourceRow->addStretch(1);
    binaryLayout->addLayout(sourceRow);

    auto *binaryHint = new CaptionLabel(
        AppI18n::text("未定义字段时按字节绘制 CH1、CH2……；协议载荷需要先启用协议模板。"), m_binaryContainer);
    binaryHint->setWordWrap(true);
    binaryLayout->addWidget(binaryHint);

    m_binaryFieldsTable = new TableWidget(m_binaryContainer);
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
    binaryLayout->addWidget(m_binaryFieldsTable, 1);

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

    m_binaryContainer->setVisible(config.protocol == AppPlot::Protocol::Binary);
    root->addWidget(m_binaryContainer, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancelButton = new PushButton(AppI18n::text("取消"), this);
    auto *applyButton = new PrimaryPushButton(AppI18n::text("应用"), this);
    buttons->addWidget(cancelButton);
    buttons->addWidget(applyButton);
    root->addLayout(buttons);
    connect(cancelButton, &PushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &PrimaryPushButton::clicked, this, &PlotParserDialog::accept);
}

AppPlot::ParserConfig PlotParserDialog::parserConfig() const { return m_config; }

void PlotParserDialog::accept()
{
    AppPlot::ParserConfig config = m_config;
    config.fields = splitFields(m_fieldsEdit->text());
    if (config.protocol == AppPlot::Protocol::Binary) {
        AppPlot::binarySourceFromKey(m_binarySourceCombo->currentData().toString(), &config.binarySource);
        QString error;
        if (!collectBinaryFields(&config.binaryFields, &error)) {
            FluentQt::InfoBar::error(AppI18n::text("配置无效"), error, Qt::Horizontal, true, 3500,
                                     FluentQt::InfoBarPosition::TopRight, this);
            return;
        }
    }
    m_config = config;
    QDialog::accept();
}

void PlotParserDialog::addBinaryFieldRow(const AppPlot::BinaryField &field)
{
    const int row = m_binaryFieldsTable->rowCount();
    m_binaryFieldsTable->insertRow(row);
    m_binaryFieldsTable->setItem(row, 0, editableItem(field.name));
    m_binaryFieldsTable->setItem(row, 1, editableItem(QString::number(field.byteOffset)));
    m_binaryFieldsTable->setCellWidget(row, 2, createTypeCell(field.type, m_binaryFieldsTable));
    m_binaryFieldsTable->setCellWidget(row, 3, createByteOrderCell(field.byteOrder, m_binaryFieldsTable));
    m_binaryFieldsTable->setItem(row, 4, editableItem(QString::number(field.scale, 'g', 12)));
    m_binaryFieldsTable->setItem(row, 5, editableItem(QString::number(field.add, 'g', 12)));
}

void PlotParserDialog::addDefaultBinaryField()
{
    AppPlot::BinaryField field;
    field.name = QStringLiteral("field_%1").arg(m_binaryFieldsTable->rowCount() + 1);
    field.type = AppPlot::BinaryType::UInt16;
    addBinaryFieldRow(field);
}

void PlotParserDialog::removeSelectedBinaryFields()
{
    QModelIndexList rows = m_binaryFieldsTable->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(),
              [](const QModelIndex &left, const QModelIndex &right) { return left.row() > right.row(); });
    for (const QModelIndex &index : rows) {
        m_binaryFieldsTable->removeRow(index.row());
    }
}

bool PlotParserDialog::collectBinaryFields(QVector<AppPlot::BinaryField> *fields, QString *error) const
{
    fields->clear();
    for (int row = 0; row < m_binaryFieldsTable->rowCount(); ++row) {
        AppPlot::BinaryField field;
        field.name = m_binaryFieldsTable->item(row, 0)->text().simplified();
        if (field.name.isEmpty()) {
            *error = AppI18n::text("第 %1 行缺少字段名称").arg(row + 1);
            return false;
        }
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
