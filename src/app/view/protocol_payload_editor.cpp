#include "app/view/protocol_payload_editor.h"
#include "app/core/app_i18n.h"
#include "app/view/muted_text.h"
#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QVBoxLayout>
#include <algorithm>
#include <numeric>

namespace {
class FieldNumberSpinBox : public FluentQt::DoubleSpinBox
{
  public:
    using FluentQt::DoubleSpinBox::DoubleSpinBox;

  protected:
    QString textFromValue(double value) const override
    {
        QString text = FluentQt::DoubleSpinBox::textFromValue(value);
        const auto decimal = locale().decimalPoint();
        if (text.contains(decimal)) {
            while (text.endsWith(QLatin1Char('0')))
                text.chop(1);
            if (text.endsWith(decimal))
                text.chop(decimal.size());
        }
        return text;
    }
};
QString typeLabel(AppPlot::BinaryType type)
{
    const int bits = AppPlot::binaryTypeSize(type) * 8;
    QString label;
    if (type == AppPlot::BinaryType::Float32 || type == AppPlot::BinaryType::Float64)
        label = AppI18n::text("%1 位浮点数").arg(bits);
    else if (type == AppPlot::BinaryType::Int8 || type == AppPlot::BinaryType::Int16 ||
             type == AppPlot::BinaryType::Int32 || type == AppPlot::BinaryType::Int64)
        label = AppI18n::text("%1 位有符号整数").arg(bits);
    else
        label = AppI18n::text("%1 位无符号整数").arg(bits);
    return label + QStringLiteral(" · ") + AppPlot::binaryTypeKey(type);
}
} // namespace

ProtocolPayloadEditor::ProtocolPayloadEditor(QWidget *parent) : QWidget(parent)
{
    using namespace FluentQt;
    setObjectName(QStringLiteral("protocolPayloadEditor"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(12);
    m_title = new StrongBodyLabel(AppI18n::text("字段属性"), this);
    root->addWidget(m_title);
    m_addressPanel = new QWidget(this);
    auto *addressForm = new QFormLayout(m_addressPanel);
    addressForm->setContentsMargins(0, 0, 0, 0);
    addressForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    addressForm->setVerticalSpacing(8);
    m_addressSize = new ComboBox(m_addressPanel);
    m_addressSize->setObjectName(QStringLiteral("protocolAddressSize"));
    m_addressSize->addItem(AppI18n::text("不区分设备地址"), QIcon(), 0);
    for (int size : {1, 2, 4})
        m_addressSize->addItem(QStringLiteral("%1 B").arg(size), QIcon(), size);
    m_addressOffset = new SpinBox(m_addressPanel);
    m_addressOffset->setObjectName(QStringLiteral("protocolAddressOffset"));
    m_addressOffset->setRange(0, 1024 * 1024 - 4);
    addressForm->addRow(AppI18n::text("字节数"), m_addressSize);
    addressForm->addRow(AppI18n::text("整帧起点"), m_addressOffset);
    root->addWidget(m_addressPanel);
    m_form = new QWidget(this);
    auto *form = new QFormLayout(m_form);
    form->setContentsMargins(0, 0, 0, 0);
    form->setVerticalSpacing(8);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_name = new LineEdit(m_form);
    m_name->setObjectName(QStringLiteral("protocolPayloadFieldName"));
    m_offset = new SpinBox(m_form);
    m_offset->setObjectName(QStringLiteral("protocolPayloadFieldOffset"));
    m_offset->setRange(0, 1024 * 1024 - 1);
    m_type = new ComboBox(m_form);
    m_type->setObjectName(QStringLiteral("protocolPayloadFieldType"));
    for (const auto type : {AppPlot::BinaryType::UInt8, AppPlot::BinaryType::Int8, AppPlot::BinaryType::UInt16,
                            AppPlot::BinaryType::Int16, AppPlot::BinaryType::UInt32, AppPlot::BinaryType::Int32,
                            AppPlot::BinaryType::UInt64, AppPlot::BinaryType::Int64, AppPlot::BinaryType::Float32,
                            AppPlot::BinaryType::Float64})
        m_type->addItem(typeLabel(type), QIcon(), AppPlot::binaryTypeKey(type));
    m_order = new ComboBox(m_form);
    m_order->setObjectName(QStringLiteral("protocolPayloadFieldByteOrder"));
    m_order->addItem(AppI18n::text("小端"), QIcon(), QStringLiteral("little"));
    m_order->addItem(AppI18n::text("大端"), QIcon(), QStringLiteral("big"));
    m_scale = new FieldNumberSpinBox(m_form);
    m_scale->setObjectName(QStringLiteral("protocolPayloadFieldScale"));
    m_add = new FieldNumberSpinBox(m_form);
    m_add->setObjectName(QStringLiteral("protocolPayloadFieldAdd"));
    for (auto *spin : {m_scale, m_add}) {
        spin->setDecimals(9);
        spin->setRange(-1e12, 1e12);
        spin->setKeyboardTracking(false);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    m_type->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    form->addRow(AppI18n::text("名称"), m_name);
    form->addRow(AppI18n::text("载荷内起点"), m_offset);
    form->addRow(AppI18n::text("数值类型"), m_type);
    form->addRow(AppI18n::text("字节序"), m_order);
    form->addRow(AppI18n::text("倍率"), m_scale);
    form->addRow(AppI18n::text("加值"), m_add);
    root->addWidget(m_form);
    m_fieldSummary = new CaptionLabel(this);
    m_fieldSummary->setObjectName(QStringLiteral("protocolPayloadFieldSummary"));
    m_fieldSummary->setWordWrap(true);
    m_fieldSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    AppUi::setMutedTextColor(m_fieldSummary);
    root->addWidget(m_fieldSummary);
    root->addStretch(1);
    connect(m_name, &LineEdit::textChanged, this, &ProtocolPayloadEditor::updateField);
    connect(m_offset, &SpinBox::valueChanged, this, &ProtocolPayloadEditor::updateField);
    connect(m_type, &ComboBox::currentIndexChanged, this, &ProtocolPayloadEditor::updateField);
    connect(m_order, &ComboBox::currentIndexChanged, this, &ProtocolPayloadEditor::updateField);
    connect(m_scale, &DoubleSpinBox::valueChanged, this, &ProtocolPayloadEditor::updateField);
    connect(m_add, &DoubleSpinBox::valueChanged, this, &ProtocolPayloadEditor::updateField);
    connect(m_addressOffset, &SpinBox::valueChanged, this, [this]() {
        updateFieldSummary();
        if (!m_loading)
            emit changed();
    });
    connect(m_addressSize, &ComboBox::currentIndexChanged, this, [this]() {
        m_addressOffset->setEnabled(m_addressSize->currentData().toInt() > 0);
        updateFieldSummary();
        if (!m_loading)
            emit changed();
    });
    setProtocolTemplate(AppProtocol::ProtocolTemplate());
}

void ProtocolPayloadEditor::setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate)
{
    m_loading = true;
    m_fields = protocolTemplate.payloadFields;
    m_payloadOffset = protocolTemplate.payloadOffset;
    m_addressOffset->setValue(protocolTemplate.addressOffset);
    m_addressSize->setCurrentIndex(m_addressSize->findData(protocolTemplate.addressSize));
    m_addressOffset->setEnabled(protocolTemplate.addressSize > 0);
    m_selected = m_fields.isEmpty() ? -1 : qBound(0, m_selected, int(m_fields.size()) - 1);
    m_loading = false;
    updateControls();
}
void ProtocolPayloadEditor::applyTo(AppProtocol::ProtocolTemplate &protocolTemplate) const
{
    protocolTemplate.payloadFields = m_fields;
    protocolTemplate.addressOffset = m_addressOffset->value();
    protocolTemplate.addressSize = m_addressSize->currentData().toInt();
}
int ProtocolPayloadEditor::selectedField() const { return m_selected; }
const QVector<AppPlot::BinaryField> &ProtocolPayloadEditor::fields() const { return m_fields; }
void ProtocolPayloadEditor::setPayloadOffset(int offset)
{
    m_payloadOffset = offset;
    updateFieldSummary();
}
void ProtocolPayloadEditor::setAddressEditing(bool address)
{
    m_addressEditing = address;
    updateControls();
}
void ProtocolPayloadEditor::selectPayloadField(int index)
{
    m_selected = index >= 0 && index < m_fields.size() ? index : -1;
    m_addressEditing = false;
    updateControls();
    emit fieldSelected(m_selected);
}
void ProtocolPayloadEditor::addField(AppPlot::BinaryType type)
{
    if (m_fields.size() >= AppPlot::MaximumChannels)
        return;
    AppPlot::BinaryField field;
    field.type = type;
    for (int suffix = 1; suffix <= m_fields.size() + 1; ++suffix) {
        field.name = QStringLiteral("field_%1").arg(suffix);
        bool exists = false;
        for (const auto &existing : m_fields)
            exists |= existing.name == field.name;
        if (!exists)
            break;
    }
    if (!m_fields.isEmpty()) {
        qint64 next = 0;
        for (const auto &existing : m_fields)
            next = qMax(next, qint64(existing.byteOffset) + AppPlot::binaryTypeSize(existing.type));
        field.byteOffset = int(qMin<qint64>(1024 * 1024 - 1, next));
    }
    m_fields.append(field);
    m_selected = m_fields.size() - 1;
    m_addressEditing = false;
    updateControls();
    emit changed();
    emit fieldSelected(m_selected);
}
void ProtocolPayloadEditor::removeField()
{
    if (m_selected < 0 || m_selected >= m_fields.size())
        return;
    m_fields.removeAt(m_selected);
    m_selected = m_fields.isEmpty() ? -1 : qMin(m_selected, int(m_fields.size()) - 1);
    updateControls();
    emit changed();
    emit fieldSelected(m_selected);
}
bool ProtocolPayloadEditor::moveFieldBefore(int field, int before)
{
    if (field < 0 || field >= m_fields.size() || before < -1 || before >= m_fields.size() || field == before)
        return false;
    QList<int> order(m_fields.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(),
                     [this](int a, int b) { return m_fields.at(a).byteOffset < m_fields.at(b).byteOffset; });
    const int from = order.indexOf(field);
    int to = before < 0 ? order.size() : order.indexOf(before);
    if (to > from)
        --to;
    if (from == to)
        return false;
    QList<int> gaps;
    int end = 0;
    for (int index : order) {
        const auto &item = m_fields.at(index);
        gaps.append(qMax(0, item.byteOffset - end));
        end = item.byteOffset + AppPlot::binaryTypeSize(item.type);
    }
    order.move(from, to);
    QVector<AppPlot::BinaryField> reordered;
    int offset = 0;
    for (int i = 0; i < order.size(); ++i) {
        auto item = m_fields.at(order.at(i));
        offset += gaps.at(i);
        item.byteOffset = offset;
        offset += AppPlot::binaryTypeSize(item.type);
        reordered.append(item);
    }
    m_fields = reordered;
    m_selected = to;
    m_addressEditing = false;
    updateControls();
    emit changed();
    emit fieldSelected(m_selected);
    return true;
}
void ProtocolPayloadEditor::updateControls()
{
    const bool selected = m_selected >= 0 && m_selected < m_fields.size();
    m_addressPanel->setVisible(m_addressEditing);
    m_form->setVisible(!m_addressEditing && selected);
    m_title->setText(m_addressEditing ? AppI18n::text("设备地址")
                     : selected       ? AppI18n::text("字段属性 · %1").arg(m_fields.at(m_selected).name)
                                      : AppI18n::text("字段属性"));
    if (selected) {
        m_loading = true;
        const auto &field = m_fields.at(m_selected);
        m_name->setText(field.name);
        m_offset->setValue(field.byteOffset);
        m_type->setCurrentIndex(m_type->findData(AppPlot::binaryTypeKey(field.type)));
        m_order->setCurrentIndex(m_order->findData(AppPlot::byteOrderKey(field.byteOrder)));
        m_order->setEnabled(AppPlot::binaryTypeSize(field.type) > 1);
        m_scale->setValue(field.scale);
        m_add->setValue(field.add);
        m_loading = false;
    }
    updateFieldSummary();
}
void ProtocolPayloadEditor::updateField()
{
    if (m_loading || m_selected < 0 || m_selected >= m_fields.size())
        return;
    auto &field = m_fields[m_selected];
    field.name = m_name->text().simplified();
    field.byteOffset = m_offset->value();
    AppPlot::binaryTypeFromKey(m_type->currentData().toString(), &field.type);
    AppPlot::byteOrderFromKey(m_order->currentData().toString(), &field.byteOrder);
    m_order->setEnabled(AppPlot::binaryTypeSize(field.type) > 1);
    field.scale = m_scale->value();
    field.add = m_add->value();
    m_title->setText(AppI18n::text("字段属性 · %1").arg(field.name));
    updateFieldSummary();
    emit changed();
}
void ProtocolPayloadEditor::updateFieldSummary()
{
    if (!m_fieldSummary)
        return;
    if (m_addressEditing)
        m_fieldSummary->setText(AppI18n::text("地址起点按整帧计算，从 0 开始。接收窗口按地址分组显示设备数据。"));
    else if (m_selected >= 0 && m_selected < m_fields.size()) {
        const auto &field = m_fields.at(m_selected);
        m_fieldSummary->setText(AppI18n::text("整帧字节 %1–%2\n显示值 = 原始值 × %3 + %4")
                                    .arg(m_payloadOffset + field.byteOffset)
                                    .arg(m_payloadOffset + field.byteOffset + AppPlot::binaryTypeSize(field.type) - 1)
                                    .arg(QString::number(field.scale, 'g', 12), QString::number(field.add, 'g', 12)));
    } else
        m_fieldSummary->setText(AppI18n::text("从左侧选择字段，或点击上方的字节类型添加数据字段。"));
}
