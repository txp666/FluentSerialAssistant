#include "app/view/workbench/workbench_page_internal.h"
#include "app/view/protocol_template_window.h"
#include "app/view/quick_plot_window.h"

#include <QtCore/QStringList>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {

constexpr const char *ProtocolTemplateSettingsKey = "protocolTemplate/templates";
constexpr const char *ProtocolTemplateExamplesSettingsKey = "protocolTemplate/exampleFrames";

QString protocolInputError(const LineEdit *nameEdit, const LineEdit *headerEdit,
                           const QList<QPair<QString, const LineEdit *>> &numberEdits)
{
    if (!nameEdit || nameEdit->text().trimmed().isEmpty()) {
        return AppI18n::text("模板名称为空");
    }
    if (headerEdit) {
        const HexParseResult header = parseHexPayload(headerEdit->text());
        if (!header.ok) {
            return AppI18n::text("帧头无效：%1，位置 %2").arg(header.errorMessage).arg(header.errorOffset + 1);
        }
    }
    for (const auto &number : numberEdits) {
        bool ok = false;
        if (number.second) {
            number.second->text().trimmed().toInt(&ok);
        }
        if (!ok || !number.second->hasAcceptableInput()) {
            return AppI18n::text("%1需要输入有效的非负整数").arg(number.first);
        }
    }
    return {};
}

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
    settings.setValue(QLatin1String(ProtocolTemplateSettingsKey),
                      QString::fromUtf8(document.toJson(QJsonDocument::Compact)));
    if (m_protocolTemplateCombo && m_protocolTemplateCombo->currentIndex() >= 0) {
        settings.setValue(QStringLiteral("protocolTemplate/current"), m_protocolTemplateCombo->currentText());
    }
    if (m_protocolEnabledCheck) {
        settings.setValue(QStringLiteral("protocolTemplate/enabled"), m_protocolEnabledCheck->isChecked());
    }
    for (QuickPlotWindow *plotWindow : m_quickPlotWindows) {
        if (plotWindow) {
            plotWindow->setProtocolTemplates(m_protocolTemplates);
        }
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
    const int targetIndex =
        qBound(0, selectedIndex >= 0 ? selectedIndex : previousIndex, m_protocolTemplates.size() - 1);

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
        const QString error = protocolInputError(m_protocolNameEdit, m_protocolHeaderEdit,
            {{AppI18n::text("长度偏移"), m_protocolLengthOffsetEdit},
             {AppI18n::text("命令偏移"), m_protocolCommandOffsetEdit},
             {AppI18n::text("命令长度"), m_protocolCommandSizeEdit},
             {AppI18n::text("载荷偏移"), m_protocolPayloadOffsetEdit},
             {AppI18n::text("载荷长度"), m_protocolPayloadLengthEdit}});
        m_protocolSaveButton->setEnabled(hasName && error.isEmpty());
    }
    if (m_protocolDeleteButton) {
        const int selectedIndex = m_protocolTemplateCombo ? m_protocolTemplateCombo->currentIndex() : -1;
        const bool selectedTemplate = selectedIndex >= 0 && selectedIndex < m_protocolTemplates.size() && hasName &&
            m_protocolTemplates.at(selectedIndex).name == m_protocolNameEdit->text().trimmed();
        m_protocolDeleteButton->setEnabled(m_protocolTemplates.size() > 1 && selectedTemplate);
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
    if (m_protocolStatusLabel && !m_updatingProtocolTemplateUi) {
        const AppProtocol::ProtocolTemplate item = currentProtocolTemplateFromUi();
        if (!enabled) {
            m_protocolStatusLabel->setText(AppI18n::text("协议模板未启用") + QStringLiteral(" · ") + item.name);
        } else {
            m_protocolStatusLabel->setText(AppI18n::text("%1 · 帧头 %2 B · 命令 %3 B · %4")
                .arg(item.name).arg(item.header.size()).arg(item.commandSize)
                .arg(checksumEnabled ? AppChecksum::labelForAlgorithm(item.checksumAlgorithm) : AppI18n::text("无校验")));
        }
    }
    if (!m_updatingProtocolTemplateUi) {
        updateProtocolTemplatePreview();
    }
}

void WorkbenchPage::showProtocolTemplateWindow()
{
    if (!m_protocolTemplateWindow) {
        return;
    }
    updateProtocolTemplatePreview();
    m_protocolTemplateWindow->showNormal();
    m_protocolTemplateWindow->raise();
    m_protocolTemplateWindow->activateWindow();
}

void WorkbenchPage::updateProtocolTemplatePreview()
{
    if (!m_protocolTemplateWindow || m_updatingProtocolTemplateUi) {
        return;
    }
    if (auto *selector =
            m_protocolTemplateWindow->findChild<ComboBox *>(QStringLiteral("protocolEditorTemplateCombo"))) {
        const QSignalBlocker blocker(selector);
        QStringList names;
        for (const AppProtocol::ProtocolTemplate &item : m_protocolTemplates) {
            names.append(item.name);
        }
        bool changed = selector->count() != names.size();
        for (int i = 0; !changed && i < names.size(); ++i) {
            changed = selector->itemText(i) != names.at(i);
        }
        if (changed) {
            selector->clear();
            selector->addItems(names);
        }
        selector->setCurrentIndex(names.indexOf(m_protocolNameEdit->text().trimmed()));
    }
    const QString error = protocolInputError(m_protocolNameEdit, m_protocolHeaderEdit,
        {{AppI18n::text("长度偏移"), m_protocolLengthOffsetEdit},
         {AppI18n::text("命令偏移"), m_protocolCommandOffsetEdit},
         {AppI18n::text("命令长度"), m_protocolCommandSizeEdit},
         {AppI18n::text("载荷偏移"), m_protocolPayloadOffsetEdit},
         {AppI18n::text("载荷长度"), m_protocolPayloadLengthEdit}});
    m_protocolTemplateWindow->setConfigurationError(error);
    m_protocolTemplateWindow->setProtocolTemplate(currentProtocolTemplateFromUi());
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
    item.lengthMode = AppProtocol::lengthModeFromKey(
        m_protocolLengthModeCombo ? m_protocolLengthModeCombo->currentData().toString()
                                  : AppProtocol::lengthModeKey(AppProtocol::LengthMode::PayloadLength));
    item.lengthByteOrder = AppChecksum::byteOrderFromKey(
        m_protocolLengthByteOrderCombo ? m_protocolLengthByteOrderCombo->currentData().toString()
                                       : AppChecksum::byteOrderKey(AppChecksum::ByteOrder::BigEndian));
    item.commandOffset = intFromEdit(m_protocolCommandOffsetEdit);
    item.commandSize = intFromEdit(m_protocolCommandSizeEdit);
    item.payloadOffset = intFromEdit(m_protocolPayloadOffsetEdit);
    item.payloadLength = intFromEdit(m_protocolPayloadLengthEdit);
    item.checksumAlgorithm = m_protocolChecksumAlgorithmCombo
                                 ? m_protocolChecksumAlgorithmCombo->currentData().toString()
                                 : AppProtocol::checksumNoneKey();
    item.checksumByteOrder = AppChecksum::byteOrderFromKey(
        m_protocolChecksumByteOrderCombo ? m_protocolChecksumByteOrderCombo->currentData().toString()
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
    if (m_protocolTemplateWindow) {
        AppSettings settings;
        const QJsonDocument examples = QJsonDocument::fromJson(
            settings.value(QLatin1String(ProtocolTemplateExamplesSettingsKey)).toString().toUtf8());
        const QJsonValue example = examples.object().value(item.name);
        if (example.isString()) {
            m_protocolTemplateWindow->setSampleHex(example.toString());
        }
    }
    updateProtocolTemplateActionState();
}

void WorkbenchPage::saveCurrentProtocolTemplate()
{
    if (!m_protocolNameEdit || !m_protocolHeaderEdit) {
        return;
    }
    const QString inputError = protocolInputError(m_protocolNameEdit, m_protocolHeaderEdit,
        {{AppI18n::text("长度偏移"), m_protocolLengthOffsetEdit},
         {AppI18n::text("命令偏移"), m_protocolCommandOffsetEdit},
         {AppI18n::text("命令长度"), m_protocolCommandSizeEdit},
         {AppI18n::text("载荷偏移"), m_protocolPayloadOffsetEdit},
         {AppI18n::text("载荷长度"), m_protocolPayloadLengthEdit}});
    if (!inputError.isEmpty()) {
        showWarning(AppI18n::text("无法保存协议模板"), inputError);
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

    if (m_protocolTemplateWindow) {
        const HexParseResult example = parseHexPayload(m_protocolTemplateWindow->sampleHex());
        if (example.ok) {
            AppSettings settings;
            QJsonObject examples = QJsonDocument::fromJson(
                settings.value(QLatin1String(ProtocolTemplateExamplesSettingsKey)).toString().toUtf8()).object();
            examples.insert(item.name, bytesToHex(example.bytes));
            settings.setValue(QLatin1String(ProtocolTemplateExamplesSettingsKey),
                              QString::fromUtf8(QJsonDocument(examples).toJson(QJsonDocument::Compact)));
        }
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
    if (!m_protocolNameEdit || m_protocolNameEdit->text().trimmed() != m_protocolTemplates.at(index).name) {
        return;
    }
    if (m_protocolTemplates.size() <= 1) {
        showWarning(AppI18n::text("无法删除协议模板"), AppI18n::text("至少保留一个协议模板"));
        return;
    }

    const QString name = m_protocolTemplates.at(index).name;
    AppSettings settings;
    QJsonObject examples = QJsonDocument::fromJson(
        settings.value(QLatin1String(ProtocolTemplateExamplesSettingsKey)).toString().toUtf8()).object();
    examples.remove(name);
    settings.setValue(QLatin1String(ProtocolTemplateExamplesSettingsKey),
                      QString::fromUtf8(QJsonDocument(examples).toJson(QJsonDocument::Compact)));
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
    if (m_protocolTemplateWindow) {
        m_protocolTemplateWindow->setSampleHex(QStringLiteral("AA 55 03 10 01 02 03 4D 6E"));
    }
    saveProtocolTemplates();
    showProtocolTemplateWindow();
    showInfo(AppI18n::text("已添加示例模板"), item.name);
}

QString WorkbenchPage::protocolParseSourceLabel(const QByteArray &data)
{
    if (m_protocolTemplateWindow && m_protocolTemplateWindow->isVisible() && !m_protocolTemplateWindow->isMinimized()) {
        m_protocolTemplateWindow->setLastFrame(data);
    }
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
