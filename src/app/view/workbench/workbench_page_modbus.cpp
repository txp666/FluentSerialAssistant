#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createModbusSection()
{
    auto *section = new HeaderCardWidget(QStringLiteral("Modbus RTU"), this);
    auto *root = cardBody(section);

    m_modbusSlaveSpin = new SpinBox(section);
    m_modbusSlaveSpin->setRange(1, 247);
    m_modbusSlaveSpin->setValue(1);
    addFormRow(root, QStringLiteral("站号"), m_modbusSlaveSpin);

    m_modbusFunctionCombo = new ComboBox(section);
    addModbusFunctionOptions(m_modbusFunctionCombo);
    makeCompactControl(m_modbusFunctionCombo);
    const int defaultFunctionIndex = m_modbusFunctionCombo->findData(AppModbus::defaultFunctionKey());
    if (defaultFunctionIndex >= 0) {
        m_modbusFunctionCombo->setCurrentIndex(defaultFunctionIndex);
    }
    addFormRow(root, QStringLiteral("功能"), m_modbusFunctionCombo);

    m_modbusAddressSpin = new SpinBox(section);
    m_modbusAddressSpin->setRange(0, 65535);
    m_modbusAddressSpin->setValue(0);
    addFormRow(root, QStringLiteral("地址"), m_modbusAddressSpin);

    m_modbusQuantitySpin = new SpinBox(section);
    m_modbusQuantitySpin->setRange(1, 2000);
    m_modbusQuantitySpin->setValue(1);
    addFormRow(root, QStringLiteral("数量"), m_modbusQuantitySpin);

    m_modbusValuesEdit = new PlainTextEdit(section);
    m_modbusValuesEdit->setPlaceholderText(QStringLiteral("写入值，例如：1 2 0x1234"));
    m_modbusValuesEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_modbusValuesEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_modbusValuesEdit->setFixedHeight(60);
    m_modbusValuesEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_modbusValuesEdit);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_modbusFillButton = new PushButton(icon(FluentIcon::Edit), QStringLiteral("填入发送"), section);
    m_modbusSendButton = new PrimaryPushButton(icon(FluentIcon::Send), QStringLiteral("发送"), section);
    setButtonRowControlPolicy(m_modbusFillButton);
    setButtonRowControlPolicy(m_modbusSendButton);
    actionRow->addWidget(m_modbusFillButton);
    actionRow->addWidget(m_modbusSendButton);
    root->addLayout(actionRow);

    m_modbusStatusLabel = new CaptionLabel(QStringLiteral("生成帧会自动附加 CRC16-Modbus"), section);
    m_modbusStatusLabel->setTextColor(QColor(96, 96, 96), QColor(180, 180, 180));
    m_modbusStatusLabel->setWordWrap(true);
    root->addWidget(m_modbusStatusLabel);

    connect(m_modbusFunctionCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        updateModbusUi();
        QSettings settings;
        settings.setValue(QStringLiteral("modbus/function"), m_modbusFunctionCombo->currentData().toString());
    });
    connect(m_modbusSlaveSpin, &SpinBox::valueChanged, this, [](int value) {
        QSettings settings;
        settings.setValue(QStringLiteral("modbus/slave"), value);
    });
    connect(m_modbusAddressSpin, &SpinBox::valueChanged, this, [](int value) {
        QSettings settings;
        settings.setValue(QStringLiteral("modbus/address"), value);
    });
    connect(m_modbusQuantitySpin, &SpinBox::valueChanged, this, [](int value) {
        QSettings settings;
        settings.setValue(QStringLiteral("modbus/quantity"), value);
    });
    connect(m_modbusValuesEdit, &PlainTextEdit::textChanged, this, [this]() {
        QSettings settings;
        settings.setValue(QStringLiteral("modbus/values"), m_modbusValuesEdit->toPlainText());
    });
    connect(m_modbusFillButton, &PushButton::clicked, this, &WorkbenchPage::fillModbusRequest);
    connect(m_modbusSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendModbusRequest);

    updateModbusUi();
    return section;
}

AppModbus::RequestConfig WorkbenchPage::currentModbusConfig() const
{
    AppModbus::RequestConfig config;
    config.slaveId = static_cast<quint8>(m_modbusSlaveSpin ? m_modbusSlaveSpin->value() : 1);
    config.functionKey =
        m_modbusFunctionCombo ? m_modbusFunctionCombo->currentData().toString() : AppModbus::defaultFunctionKey();
    config.address = static_cast<quint16>(m_modbusAddressSpin ? m_modbusAddressSpin->value() : 0);
    config.quantity = static_cast<quint16>(m_modbusQuantitySpin ? m_modbusQuantitySpin->value() : 1);
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
        showWarning(QStringLiteral("Modbus 帧生成失败"), result.errorMessage);
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
    setModbusStatusText(QStringLiteral("已填入发送区 · %1").arg(bytesToHex(frame)));
}

void WorkbenchPage::sendModbusRequest()
{
    if (!m_serial.isOpen()) {
        showWarning(QStringLiteral("无法发送 Modbus 请求"), QStringLiteral("请先连接串口"));
        return;
    }

    bool ok = false;
    const QByteArray frame = currentModbusFrame(&ok);
    if (!ok) {
        return;
    }

    QString error;
    if (!m_serial.writeData(frame, &error)) {
        showError(QStringLiteral("Modbus 发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = QStringLiteral("hex");
    historyItem.payload = bytesToHex(frame);
    historyItem.lineEnding = QStringLiteral("none");
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, frame);
    setModbusStatusText(QStringLiteral("已发送 · %1").arg(bytesToHex(frame)));
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
    if (m_modbusQuantitySpin) {
        m_modbusQuantitySpin->setEnabled(quantityEnabled);
        m_modbusQuantitySpin->setMaximum(function.coil ? 2000 : 125);
        if (!quantityEnabled) {
            m_modbusQuantitySpin->setValue(1);
        }
    }
    if (m_modbusValuesEdit) {
        m_modbusValuesEdit->setEnabled(needsValues);
        if (!needsValues) {
            m_modbusValuesEdit->setPlaceholderText(QStringLiteral("读取功能无需填写值"));
        } else if (function.coil) {
            m_modbusValuesEdit->setPlaceholderText(function.multiple
                                                       ? QStringLiteral("线圈值列表，例如：1 0 1 1")
                                                       : QStringLiteral("线圈值：0/1、true/false 或 on/off"));
        } else {
            m_modbusValuesEdit->setPlaceholderText(function.multiple ? QStringLiteral("寄存器值列表，例如：1 2 0x1234")
                                                                     : QStringLiteral("寄存器值：0-65535 或 0x1234"));
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
