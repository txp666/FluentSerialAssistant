#include "app/view/workbench/workbench_page_internal.h"

#include <QtCore/QStringList>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

constexpr const char *ProtocolTemplateSettingsKey = "protocolTemplate/templates";

bool setComboCurrentData(ComboBox *combo, const QVariant &data)
{
    if (!combo) {
        return false;
    }
    const int index = combo->findData(data);
    if (index < 0) {
        return false;
    }
    combo->setCurrentIndex(index);
    return true;
}

QString uniqueProtocolTemplateName(const QList<AppProtocol::ProtocolTemplate> &templates, const QString &baseName)
{
    QString name = baseName.trimmed().isEmpty() ? AppI18n::text("示例模板") : baseName.trimmed();
    bool exists = false;
    for (const AppProtocol::ProtocolTemplate &item : templates) {
        if (item.name == name) {
            exists = true;
            break;
        }
    }
    if (!exists) {
        return name;
    }

    for (int suffix = 2; suffix < 10000; ++suffix) {
        const QString candidate = QStringLiteral("%1 %2").arg(name).arg(suffix);
        bool used = false;
        for (const AppProtocol::ProtocolTemplate &item : templates) {
            if (item.name == candidate) {
                used = true;
                break;
            }
        }
        if (!used) {
            return candidate;
        }
    }
    return name;
}

int intFromEdit(const LineEdit *edit, int fallback = 0)
{
    if (!edit) {
        return fallback;
    }

    bool ok = false;
    const int value = edit->text().trimmed().toInt(&ok);
    return ok ? qMax(0, value) : fallback;
}

void setEditValue(LineEdit *edit, int value)
{
    if (edit) {
        edit->setText(QString::number(qMax(0, value)));
    }
}

QString protocolTemplateExampleDetails()
{
    const QStringList lines = {
        AppI18n::text("示例帧："),
        QStringLiteral("AA 55 03 10 01 02 03 4D 6E"),
        QString(),
        AppI18n::text("字节位置从 0 开始："),
        AppI18n::text("0-1：帧头 AA 55"),
        AppI18n::text("2：长度字段 03，表示载荷长度 3 B"),
        AppI18n::text("3：命令字 10"),
        AppI18n::text("4-6：载荷 01 02 03"),
        AppI18n::text("7-8：CRC16-Modbus，低字节在前 4D 6E"),
        QString(),
        AppI18n::text("对应配置："),
        AppI18n::text("帧头 = AA 55"),
        AppI18n::text("长度偏移 = 2，长度 = 1 B，含义 = 载荷长度"),
        AppI18n::text("命令偏移 = 3，命令长度 = 1"),
        AppI18n::text("载荷偏移 = 4，载荷长度 = 0"),
        AppI18n::text("校验 = CRC16-Modbus，校验序 = 低字节在前"),
        QString(),
        AppI18n::text("载荷长度填 0 表示按长度字段自动计算。"),
        AppI18n::text("没有长度字段时，长度选择 0 B，并填写固定载荷长度。"),
        AppI18n::text("长度字段表示整帧总长时，含义选择整帧长度。"),
    };
    return lines.join(QLatin1Char('\n'));
}

} // namespace

void WorkbenchPage::loadProtocolTemplates()
{
    AppSettings settings;
    const QByteArray json = settings.value(QLatin1String(ProtocolTemplateSettingsKey)).toString().toUtf8();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (!json.isEmpty() && parseError.error == QJsonParseError::NoError && document.isArray()) {
        m_protocolTemplates = AppProtocol::listFromJson(document.array());
    } else {
        m_protocolTemplates = AppProtocol::defaultTemplates();
    }

    const QString currentName = settings.value(QStringLiteral("protocolTemplate/current")).toString();
    int selectedIndex = -1;
    for (int i = 0; i < m_protocolTemplates.size(); ++i) {
        if (m_protocolTemplates.at(i).name == currentName) {
            selectedIndex = i;
            break;
        }
    }
    updateProtocolTemplateCombo(selectedIndex);
    if (m_protocolEnabledCheck) {
        const QSignalBlocker blocker(m_protocolEnabledCheck);
        m_protocolEnabledCheck->setChecked(settings.value(QStringLiteral("protocolTemplate/enabled"), false).toBool());
    }
    updateProtocolTemplateActionState();
}

void WorkbenchPage::saveProtocolTemplates() const
{
    AppSettings settings;
    const QJsonDocument document(AppProtocol::listToJson(m_protocolTemplates));
    settings.setValue(QLatin1String(ProtocolTemplateSettingsKey), QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
    if (m_protocolTemplateCombo && m_protocolTemplateCombo->currentIndex() >= 0) {
        settings.setValue(QStringLiteral("protocolTemplate/current"), m_protocolTemplateCombo->currentText());
    }
    if (m_protocolEnabledCheck) {
        settings.setValue(QStringLiteral("protocolTemplate/enabled"), m_protocolEnabledCheck->isChecked());
    }
}

void WorkbenchPage::updateProtocolTemplateCombo(int selectedIndex)
{
    if (!m_protocolTemplateCombo) {
        return;
    }
    if (m_protocolTemplates.isEmpty()) {
        m_protocolTemplates = AppProtocol::defaultTemplates();
    }

    const int previousIndex = m_protocolTemplateCombo->currentIndex();
    const int targetIndex = qBound(0, selectedIndex >= 0 ? selectedIndex : previousIndex, m_protocolTemplates.size() - 1);

    const QSignalBlocker blocker(m_protocolTemplateCombo);
    m_protocolTemplateCombo->clear();
    for (int i = 0; i < m_protocolTemplates.size(); ++i) {
        m_protocolTemplateCombo->addItem(m_protocolTemplates.at(i).name, QIcon(), i);
    }
    m_protocolTemplateCombo->setCurrentIndex(targetIndex);
    applyProtocolTemplate(targetIndex);
}

void WorkbenchPage::updateProtocolTemplateUi()
{
    applyProtocolTemplate(m_protocolTemplateCombo ? m_protocolTemplateCombo->currentIndex() : -1);
}

void WorkbenchPage::updateProtocolTemplateActionState()
{
    const bool hasUi = m_protocolNameEdit && m_protocolHeaderEdit;
    const bool hasName = hasUi && !m_protocolNameEdit->text().trimmed().isEmpty();
    const bool enabled = m_protocolEnabledCheck && m_protocolEnabledCheck->isChecked();
    const int lengthSize = m_protocolLengthSizeCombo ? m_protocolLengthSizeCombo->currentData().toInt() : 0;
    const bool checksumEnabled =
        m_protocolChecksumAlgorithmCombo &&
        m_protocolChecksumAlgorithmCombo->currentData().toString() != AppProtocol::checksumNoneKey();

    if (m_protocolSaveButton) {
        m_protocolSaveButton->setEnabled(hasName);
    }
    if (m_protocolDeleteButton) {
        m_protocolDeleteButton->setEnabled(m_protocolTemplates.size() > 1);
    }
    if (m_protocolLengthModeCombo) {
        m_protocolLengthModeCombo->setEnabled(lengthSize > 0);
    }
    if (m_protocolLengthByteOrderCombo) {
        m_protocolLengthByteOrderCombo->setEnabled(lengthSize > 1);
    }
    if (m_protocolChecksumByteOrderCombo) {
        m_protocolChecksumByteOrderCombo->setEnabled(checksumEnabled);
    }
    if (m_protocolStatusLabel && !enabled) {
        m_protocolStatusLabel->setText(AppI18n::text("协议模板未启用"));
    }
}

AppProtocol::ProtocolTemplate WorkbenchPage::currentProtocolTemplateFromUi() const
{
    AppProtocol::ProtocolTemplate item;
    item.name = m_protocolNameEdit ? m_protocolNameEdit->text().trimmed() : AppI18n::text("未命名模板");
    if (m_protocolHeaderEdit) {
        const HexParseResult result = parseHexPayload(m_protocolHeaderEdit->text());
        if (result.ok) {
            item.header = result.bytes;
        }
    }
    item.lengthOffset = intFromEdit(m_protocolLengthOffsetEdit);
    item.lengthSize = m_protocolLengthSizeCombo ? m_protocolLengthSizeCombo->currentData().toInt() : 0;
    item.lengthMode =
        AppProtocol::lengthModeFromKey(m_protocolLengthModeCombo ? m_protocolLengthModeCombo->currentData().toString()
                                                                 : AppProtocol::lengthModeKey(AppProtocol::LengthMode::PayloadLength));
    item.lengthByteOrder =
        AppChecksum::byteOrderFromKey(m_protocolLengthByteOrderCombo ? m_protocolLengthByteOrderCombo->currentData().toString()
                                                                     : AppChecksum::byteOrderKey(AppChecksum::ByteOrder::BigEndian));
    item.commandOffset = intFromEdit(m_protocolCommandOffsetEdit);
    item.commandSize = intFromEdit(m_protocolCommandSizeEdit);
    item.payloadOffset = intFromEdit(m_protocolPayloadOffsetEdit);
    item.payloadLength = intFromEdit(m_protocolPayloadLengthEdit);
    item.checksumAlgorithm = m_protocolChecksumAlgorithmCombo
                                 ? m_protocolChecksumAlgorithmCombo->currentData().toString()
                                 : AppProtocol::checksumNoneKey();
    item.checksumByteOrder =
        AppChecksum::byteOrderFromKey(m_protocolChecksumByteOrderCombo ? m_protocolChecksumByteOrderCombo->currentData().toString()
                                                                       : AppChecksum::byteOrderKey(AppChecksum::ByteOrder::LittleEndian));
    return item;
}

void WorkbenchPage::applyProtocolTemplate(int index)
{
    if (m_updatingProtocolTemplateUi || index < 0 || index >= m_protocolTemplates.size()) {
        updateProtocolTemplateActionState();
        return;
    }

    m_updatingProtocolTemplateUi = true;
    const AppProtocol::ProtocolTemplate item = m_protocolTemplates.at(index);
    if (m_protocolNameEdit) {
        m_protocolNameEdit->setText(item.name);
    }
    if (m_protocolHeaderEdit) {
        m_protocolHeaderEdit->setText(bytesToHex(item.header));
    }
    setEditValue(m_protocolLengthOffsetEdit, item.lengthOffset);
    setComboCurrentData(m_protocolLengthSizeCombo, item.lengthSize);
    setComboCurrentData(m_protocolLengthModeCombo, AppProtocol::lengthModeKey(item.lengthMode));
    setComboCurrentData(m_protocolLengthByteOrderCombo, AppChecksum::byteOrderKey(item.lengthByteOrder));
    setEditValue(m_protocolCommandOffsetEdit, item.commandOffset);
    setEditValue(m_protocolCommandSizeEdit, item.commandSize);
    setEditValue(m_protocolPayloadOffsetEdit, item.payloadOffset);
    setEditValue(m_protocolPayloadLengthEdit, item.payloadLength);
    setComboCurrentData(m_protocolChecksumAlgorithmCombo, item.checksumAlgorithm);
    setComboCurrentData(m_protocolChecksumByteOrderCombo, AppChecksum::byteOrderKey(item.checksumByteOrder));
    if (m_protocolStatusLabel && (!m_protocolEnabledCheck || !m_protocolEnabledCheck->isChecked())) {
        m_protocolStatusLabel->setText(AppI18n::text("协议模板未启用"));
    }
    m_updatingProtocolTemplateUi = false;
    updateProtocolTemplateActionState();
}

void WorkbenchPage::saveCurrentProtocolTemplate()
{
    if (!m_protocolNameEdit || !m_protocolHeaderEdit) {
        return;
    }
    if (m_protocolNameEdit->text().trimmed().isEmpty()) {
        showWarning(AppI18n::text("无法保存协议模板"), AppI18n::text("模板名称为空"));
        return;
    }

    if (!m_protocolHeaderEdit->text().trimmed().isEmpty()) {
        const HexParseResult header = parseHexPayload(m_protocolHeaderEdit->text());
        if (!header.ok) {
            showWarning(AppI18n::text("帧头无效"),
                        header.errorOffset >= 0
                            ? AppI18n::text("%1，位置 %2").arg(header.errorMessage).arg(header.errorOffset + 1)
                            : header.errorMessage);
            return;
        }
    }

    AppProtocol::ProtocolTemplate item = currentProtocolTemplateFromUi();
    int index = -1;
    for (int i = 0; i < m_protocolTemplates.size(); ++i) {
        if (m_protocolTemplates.at(i).name == item.name) {
            index = i;
            break;
        }
    }
    if (index >= 0) {
        m_protocolTemplates[index] = item;
    } else {
        m_protocolTemplates.append(item);
        index = m_protocolTemplates.size() - 1;
    }

    updateProtocolTemplateCombo(index);
    saveProtocolTemplates();
    showSuccess(AppI18n::text("已保存协议模板"), item.name);
}

void WorkbenchPage::deleteCurrentProtocolTemplate()
{
    const int index = m_protocolTemplateCombo ? m_protocolTemplateCombo->currentIndex() : -1;
    if (index < 0 || index >= m_protocolTemplates.size()) {
        return;
    }
    if (m_protocolTemplates.size() <= 1) {
        showWarning(AppI18n::text("无法删除协议模板"), AppI18n::text("至少保留一个协议模板"));
        return;
    }

    const QString name = m_protocolTemplates.at(index).name;
    m_protocolTemplates.removeAt(index);
    updateProtocolTemplateCombo(qMin(index, m_protocolTemplates.size() - 1));
    saveProtocolTemplates();
    showInfo(AppI18n::text("已删除协议模板"), name);
}

void WorkbenchPage::insertProtocolTemplateExample()
{
    AppProtocol::ProtocolTemplate item = AppProtocol::defaultTemplate();
    item.name = uniqueProtocolTemplateName(m_protocolTemplates, item.name);
    m_protocolTemplates.append(item);
    updateProtocolTemplateCombo(m_protocolTemplates.size() - 1);
    saveProtocolTemplates();

    const QString detail =
        AppI18n::text("已添加示例模板：%1").arg(item.name) + QStringLiteral("\n\n") + protocolTemplateExampleDetails();
    MessageBox dialog(AppI18n::text("协议模板示例说明"), detail, window());
    dialog.hideCancelButton();
    dialog.setClosableOnMaskClicked(true);
    dialog.setDraggable(true);
    dialog.setContentCopyable(true);
    dialog.exec();
}

QString WorkbenchPage::protocolParseSourceLabel(const QByteArray &data)
{
    if (!m_protocolEnabledCheck || !m_protocolEnabledCheck->isChecked() || m_protocolTemplates.isEmpty()) {
        return {};
    }

    const AppProtocol::ProtocolTemplate item = currentProtocolTemplateFromUi();
    const AppProtocol::ParseResult result = AppProtocol::parseFrame(data, item);
    if (!result.ok) {
        if (m_protocolStatusLabel) {
            m_protocolStatusLabel->setText(AppI18n::text("协议解析失败：%1").arg(result.errorMessage));
        }
        return {};
    }

    if (m_protocolStatusLabel) {
        m_protocolStatusLabel->setText(result.summary);
    }

    const QString command = result.command.isEmpty() ? QStringLiteral("-") : bytesToHex(result.command);
    QString label = AppI18n::text("协议 CMD %1").arg(command);
    if (result.checksumChecked) {
        label += result.checksumValid ? QStringLiteral(" OK") : QStringLiteral(" ERR");
    }
    return label;
}
