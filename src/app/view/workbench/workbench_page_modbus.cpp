#include "app/view/workbench/workbench_page_internal.h"
#include "app/core/app_i18n.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createModbusSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::CommandPrompt, QStringLiteral("Modbus RTU"), QString(), this);
    auto *root = cardBody(section);

    m_modbusSlaveEdit = createNumberEdit(section, 1, 1, 247);
    addFormRow(root, AppI18n::text("站号"), m_modbusSlaveEdit);

    m_modbusFunctionCombo = new ComboBox(section);
    addModbusFunctionOptions(m_modbusFunctionCombo);
    makeCompactControl(m_modbusFunctionCombo);
    const int defaultFunctionIndex = m_modbusFunctionCombo->findData(AppModbus::defaultFunctionKey());
    if (defaultFunctionIndex >= 0) {
        m_modbusFunctionCombo->setCurrentIndex(defaultFunctionIndex);
    }
    addFormRow(root, AppI18n::text("功能"), m_modbusFunctionCombo);

    m_modbusAddressEdit = createNumberEdit(section, 0, 0, 65535);
    addFormRow(root, AppI18n::text("地址"), m_modbusAddressEdit);

    m_modbusQuantityEdit = createNumberEdit(section, 1, 1, 2000);
    addFormRow(root, AppI18n::text("数量"), m_modbusQuantityEdit);

    m_modbusValuesEdit = new PlainTextEdit(section);
    m_modbusValuesEdit->setPlaceholderText(AppI18n::text("写入值，例如：1 2 0x1234"));
    m_modbusValuesEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_modbusValuesEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_modbusValuesEdit->setFixedHeight(60);
    m_modbusValuesEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_modbusValuesEdit);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_modbusFillButton = new PushButton(icon(FluentIcon::Edit), AppI18n::text("填入发送"), section);
    m_modbusSendButton = new PrimaryPushButton(icon(FluentIcon::Send), AppI18n::text("发送"), section);
    setButtonRowControlPolicy(m_modbusFillButton);
    setButtonRowControlPolicy(m_modbusSendButton);
    actionRow->addWidget(m_modbusFillButton);
    actionRow->addWidget(m_modbusSendButton);
    root->addLayout(actionRow);

    m_modbusStatusLabel = new CaptionLabel(AppI18n::text("生成帧会自动附加 CRC16-Modbus"), section);
    m_modbusStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_modbusStatusLabel->setWordWrap(true);
    root->addWidget(m_modbusStatusLabel);

    connect(m_modbusFunctionCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        updateModbusUi();
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/function"), m_modbusFunctionCombo->currentData().toString());
    });
    connect(m_modbusSlaveEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/slave"), numberEditValue(m_modbusSlaveEdit, 1, 1, 247));
    });
    connect(m_modbusAddressEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/address"), numberEditValue(m_modbusAddressEdit, 0, 0, 65535));
    });
    connect(m_modbusQuantityEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/quantity"), numberEditValue(m_modbusQuantityEdit, 1, 1, 2000));
    });
    connect(m_modbusValuesEdit, &PlainTextEdit::textChanged, this, [this]() {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/values"), m_modbusValuesEdit->toPlainText());
    });
    connect(m_modbusFillButton, &PushButton::clicked, this, &WorkbenchPage::fillModbusRequest);
    connect(m_modbusSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendModbusRequest);

    updateModbusUi();
    makeCollapsibleCard(section, QStringLiteral("modbus"));
    return section;
}

AppModbus::RequestConfig WorkbenchPage::currentModbusConfig() const
{
    AppModbus::RequestConfig config;
    config.slaveId = static_cast<quint8>(numberEditValue(m_modbusSlaveEdit, 1, 1, 247));
    config.functionKey =
        m_modbusFunctionCombo ? m_modbusFunctionCombo->currentData().toString() : AppModbus::defaultFunctionKey();
    const AppModbus::FunctionOption function = AppModbus::functionForKey(config.functionKey);
    config.address = static_cast<quint16>(numberEditValue(m_modbusAddressEdit, 0, 0, 65535));
    config.quantity =
        static_cast<quint16>(numberEditValue(m_modbusQuantityEdit, 1, 1, function.coil ? 2000 : 125));
    config.valuesText = m_modbusValuesEdit ? m_modbusValuesEdit->toPlainText() : QString();
    return config;
}

QByteArray WorkbenchPage::currentModbusFrame(bool *ok)
{
    if (ok) {
        *ok = false;
    }

    const AppModbus::BuildResult result = AppModbus::buildRequest(currentModbusConfig());
    if (!result.ok) {
        setModbusStatusText(result.errorMessage);
        showWarning(AppI18n::text("Modbus 帧生成失败"), result.errorMessage);
        return {};
    }

    if (ok) {
        *ok = true;
    }
    setModbusStatusText(QStringLiteral("%1 · %2").arg(result.summary, bytesToHex(result.frame)));
    return result.frame;
}

void WorkbenchPage::fillModbusRequest()
{
    bool ok = false;
    const QByteArray frame = currentModbusFrame(&ok);
    if (!ok) {
        return;
    }

    m_hexSendCheck->setChecked(true);
    const int eolIndex = m_lineEndingCombo->findData(QStringLiteral("none"));
    if (eolIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(eolIndex);
    }
    if (m_checksumAppendCheck) {
        m_checksumAppendCheck->setChecked(false);
    }
    m_sendEdit->setPlainText(bytesToHex(frame));
    setModbusStatusText(AppI18n::text("已填入发送区 · %1").arg(bytesToHex(frame)));
}

void WorkbenchPage::sendModbusRequest()
{
    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法发送 Modbus 请求"), AppI18n::text("请先连接串口"));
        return;
    }

    bool ok = false;
    const QByteArray frame = currentModbusFrame(&ok);
    if (!ok) {
        return;
    }

    QString error;
    if (!m_serial.writeData(frame, &error)) {
        showError(AppI18n::text("Modbus 发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = QStringLiteral("hex");
    historyItem.payload = bytesToHex(frame);
    historyItem.lineEnding = QStringLiteral("none");
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, frame);
    setModbusStatusText(AppI18n::text("已发送 · %1").arg(bytesToHex(frame)));
}

void WorkbenchPage::updateModbusUi()
{
    if (!m_modbusFunctionCombo) {
        return;
    }

    const AppModbus::FunctionOption function =
        AppModbus::functionForKey(m_modbusFunctionCombo->currentData().toString());
    const bool needsValues = function.write;
    const bool quantityEnabled = !function.write || function.multiple;
    if (m_modbusQuantityEdit) {
        const int maximumQuantity = function.coil ? 2000 : 125;
        m_modbusQuantityEdit->setEnabled(quantityEnabled);
        m_modbusQuantityEdit->setValidator(new QIntValidator(1, maximumQuantity, m_modbusQuantityEdit));
        if (!quantityEnabled) {
            setNumberEditValue(m_modbusQuantityEdit, 1, 1, maximumQuantity);
        } else {
            setNumberEditValue(m_modbusQuantityEdit, numberEditValue(m_modbusQuantityEdit, 1, 1, maximumQuantity), 1,
                               maximumQuantity);
        }
    }
    if (m_modbusValuesEdit) {
        m_modbusValuesEdit->setEnabled(needsValues);
        if (!needsValues) {
            m_modbusValuesEdit->setPlaceholderText(AppI18n::text("读取功能无需填写值"));
        } else if (function.coil) {
            m_modbusValuesEdit->setPlaceholderText(function.multiple
                                                       ? AppI18n::text("线圈值列表，例如：1 0 1 1")
                                                       : AppI18n::text("线圈值：0/1、true/false 或 on/off"));
        } else {
            m_modbusValuesEdit->setPlaceholderText(function.multiple ? AppI18n::text("寄存器值列表，例如：1 2 0x1234")
                                                                     : AppI18n::text("寄存器值：0-65535 或 0x1234"));
        }
    }
}

void WorkbenchPage::updateModbusResponseStatus(const QByteArray &data)
{
    if (!m_modbusStatusLabel || data.isEmpty()) {
        return;
    }

    const AppModbus::ParseResult result = AppModbus::parseResponse(data);
    if (result.recognized) {
        setModbusStatusText(result.summary);
    }
}

void WorkbenchPage::setModbusStatusText(const QString &text)
{
    if (m_modbusStatusLabel) {
        m_modbusStatusLabel->setText(text);
    }
}
