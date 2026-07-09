#include "app/view/workbench/workbench_page_internal.h"
#include "app/core/app_i18n.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {
constexpr int SendPacketSchemaVersion = 1;
const QString PacketSchemaName = QStringLiteral("fluent-serial-assistant.send-packets");
} // namespace

void WorkbenchPage::updatePacketTable(int selectedRow)
{
    if (!m_packetList) {
        return;
    }

    const QSignalBlocker blocker(m_packetList);
    m_packetList->clear();
    for (int row = 0; row < m_sendPackets.size(); ++row) {
        const SendPacket &packet = m_sendPackets.at(row);
        QString payload = packet.payload.simplified();
        if (payload.size() > 38) {
            payload = payload.left(35) + QStringLiteral("...");
        }
        QString note = packet.note.simplified();
        if (note.size() > 22) {
            note = note.left(19) + QStringLiteral("...");
        }
        const QString group = packet.group.isEmpty() ? AppI18n::text("默认") : packet.group;
        const QString status = packet.enabled ? QString() : AppI18n::text(" [停用]");
        const QString suffix = note.isEmpty() ? QString() : QStringLiteral(" · %1").arg(note);

        auto *item = new QListWidgetItem(icon(packet.enabled ? FluentIcon::CommandPrompt : FluentIcon::Cancel),
                                         QStringLiteral("%1 / %2%3\n%4 · %5 · %6%7")
                                             .arg(group, packet.name, status, modeLabel(packet.mode),
                                                  lineEndingLabel(packet.lineEnding), payload, suffix));
        item->setData(Qt::UserRole, row);
        item->setToolTip(AppI18n::text("分组：%1\n名称：%2\n备注：%3\n内容：%4")
                             .arg(group, packet.name, packet.note, packet.payload));
        item->setSizeHint(QSize(0, 58));
        m_packetList->addItem(item);
    }

    if (!m_sendPackets.isEmpty()) {
        const int row =
            qBound(0, selectedRow >= 0 ? selectedRow : m_packetList->currentRow(), m_sendPackets.size() - 1);
        m_packetList->setCurrentRow(row);
    }
    updatePacketActionState();
}

QList<int> WorkbenchPage::selectedPacketRows() const
{
    QList<int> rows;
    if (!m_packetList) {
        return rows;
    }

    const auto items = m_packetList->selectedItems();
    rows.reserve(items.size());
    for (QListWidgetItem *item : items) {
        const int row = item->data(Qt::UserRole).toInt();
        if (row >= 0 && row < m_sendPackets.size()) {
            rows.append(row);
        }
    }

    if (rows.isEmpty()) {
        const int row = m_packetList->currentRow();
        if (row >= 0 && row < m_sendPackets.size()) {
            rows.append(row);
        }
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void WorkbenchPage::updatePacketActionState()
{
    if (!m_packetList) {
        return;
    }

    const bool hasPacket = !m_sendPackets.isEmpty();
    const int row = m_packetList->currentRow();
    const bool hasCurrent = row >= 0 && row < m_sendPackets.size();
    const QList<int> rows = selectedPacketRows();
    bool hasEnabledSelection = false;
    for (const int selectedRow : rows) {
        if (m_sendPackets.at(selectedRow).enabled) {
            hasEnabledSelection = true;
            break;
        }
    }
    if (m_packetLoadButton) {
        m_packetLoadButton->setEnabled(hasCurrent);
    }
    if (m_packetDeleteButton) {
        m_packetDeleteButton->setEnabled(!rows.isEmpty());
    }
    if (m_packetSendButton) {
        m_packetSendButton->setEnabled(hasCurrent && m_sendPackets.at(row).enabled && m_serial.isOpen());
    }
    if (m_packetBatchSendButton) {
        m_packetBatchSendButton->setEnabled(hasEnabledSelection && m_serial.isOpen());
    }
    if (m_packetUpButton) {
        m_packetUpButton->setEnabled(rows.size() == 1 && rows.first() > 0);
    }
    if (m_packetDownButton) {
        m_packetDownButton->setEnabled(rows.size() == 1 && rows.first() < m_sendPackets.size() - 1);
    }
    if (m_packetExportButton) {
        m_packetExportButton->setEnabled(hasPacket);
    }
}

void WorkbenchPage::applyPacket(int row)
{
    if (row < 0 || row >= m_sendPackets.size()) {
        return;
    }

    const SendPacket packet = m_sendPackets.at(row);
    if (m_packetGroupEdit) {
        m_packetGroupEdit->setText(packet.group);
    }
    m_packetNameEdit->setText(packet.name);
    if (m_packetNoteEdit) {
        m_packetNoteEdit->setText(packet.note);
    }
    if (m_packetEnabledCheck) {
        m_packetEnabledCheck->setChecked(packet.enabled);
    }
    m_packetPayloadEdit->setPlainText(packet.payload);
    const int packetModeIndex = m_packetModeCombo->findData(packet.mode);
    if (packetModeIndex >= 0) {
        m_packetModeCombo->setCurrentIndex(packetModeIndex);
    }
    const int packetLineEndingIndex = m_packetLineEndingCombo->findData(packet.lineEnding);
    if (packetLineEndingIndex >= 0) {
        m_packetLineEndingCombo->setCurrentIndex(packetLineEndingIndex);
    }

    m_hexSendCheck->setChecked(packet.mode == QStringLiteral("hex"));
    const int lineEndingIndex = m_lineEndingCombo->findData(packet.lineEnding);
    if (lineEndingIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(lineEndingIndex);
    }
    m_sendEdit->setPlainText(packet.payload);
}

void WorkbenchPage::saveCurrentPacket()
{
    const QString payload = m_packetPayloadEdit->toPlainText();
    if (payload.trimmed().isEmpty()) {
        showWarning(AppI18n::text("无法保存"), AppI18n::text("发送内容为空"));
        return;
    }

    QString name = m_packetNameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = payload.simplified();
        if (name.size() > 18) {
            name = name.left(18) + QStringLiteral("...");
        }
        if (name.isEmpty()) {
            name = AppI18n::text("未命名包");
        }
        m_packetNameEdit->setText(name);
    }

    SendPacket packet;
    packet.group = m_packetGroupEdit ? m_packetGroupEdit->text().trimmed() : QString();
    packet.name = name;
    packet.note = m_packetNoteEdit ? m_packetNoteEdit->text().trimmed() : QString();
    packet.mode = m_packetModeCombo->currentData().toString();
    packet.payload = payload;
    packet.lineEnding = m_packetLineEndingCombo->currentData().toString();
    packet.enabled = !m_packetEnabledCheck || m_packetEnabledCheck->isChecked();

    int targetRow = -1;
    for (int i = 0; i < m_sendPackets.size(); ++i) {
        const SendPacket &existing = m_sendPackets.at(i);
        if (existing.group == packet.group && existing.name == packet.name) {
            targetRow = i;
            break;
        }
    }
    if (targetRow >= 0) {
        m_sendPackets[targetRow] = packet;
    } else {
        if (m_sendPackets.size() >= MaxSendPacketItems) {
            m_sendPackets.removeLast();
        }
        m_sendPackets.append(packet);
        targetRow = m_sendPackets.size() - 1;
    }

    updatePacketTable(targetRow);
    applyPacket(targetRow);
    saveSendPackets();
    showSuccess(AppI18n::text("已保存常用包"), packet.name);
}

void WorkbenchPage::removeSelectedPacket()
{
    QList<int> rows = selectedPacketRows();
    if (rows.isEmpty()) {
        return;
    }

    const QString name = rows.size() == 1 ? m_sendPackets.at(rows.first()).name : QString();
    const int nextRow = rows.first();
    std::sort(rows.begin(), rows.end(), [](int left, int right) { return left > right; });
    for (const int row : rows) {
        if (row >= 0 && row < m_sendPackets.size()) {
            m_sendPackets.removeAt(row);
        }
    }

    updatePacketTable(qMin(nextRow, m_sendPackets.size() - 1));
    saveSendPackets();
    showInfo(AppI18n::text("已删除常用包"), name.isEmpty() ? AppI18n::text("%1 条").arg(rows.size()) : name);
}

void WorkbenchPage::moveSelectedPacket(int direction)
{
    const QList<int> rows = selectedPacketRows();
    if (rows.size() != 1) {
        return;
    }
    const int row = rows.first();
    const int target = row + direction;
    if (row < 0 || row >= m_sendPackets.size() || target < 0 || target >= m_sendPackets.size()) {
        return;
    }

    m_sendPackets.swapItemsAt(row, target);
    updatePacketTable(target);
    saveSendPackets();
}

void WorkbenchPage::sendSelectedPacket() { sendPacket(m_packetList ? m_packetList->currentRow() : -1); }

void WorkbenchPage::sendSelectedPackets()
{
    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法批量发送"), AppI18n::text("请先连接串口"));
        return;
    }

    const QList<int> rows = selectedPacketRows();
    if (rows.isEmpty()) {
        showWarning(AppI18n::text("无法批量发送"), AppI18n::text("请先选择常用包"));
        return;
    }

    int sent = 0;
    int skipped = 0;
    for (const int row : rows) {
        if (row < 0 || row >= m_sendPackets.size()) {
            continue;
        }
        const SendPacket packet = m_sendPackets.at(row);
        if (!packet.enabled) {
            ++skipped;
            continue;
        }
        if (!sendPacketPayload(packet)) {
            showWarning(AppI18n::text("批量发送已中止"),
                        sent > 0 ? AppI18n::text("已发送 %1 条，失败项：%2").arg(sent).arg(packet.name)
                                 : AppI18n::text("失败项：%1").arg(packet.name));
            return;
        }
        ++sent;
    }

    if (sent == 0) {
        showWarning(AppI18n::text("无法批量发送"), AppI18n::text("选中的常用包都已停用"));
        return;
    }
    const QString skippedText = skipped > 0 ? AppI18n::text("，跳过停用 %1 条").arg(skipped) : QString();
    showSuccess(AppI18n::text("批量发送完成"), AppI18n::text("已发送 %1 条%2").arg(sent).arg(skippedText));
}

void WorkbenchPage::sendPacket(int row)
{
    if (row < 0 || row >= m_sendPackets.size()) {
        return;
    }
    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法发送"), AppI18n::text("请先连接串口"));
        return;
    }

    const SendPacket packet = m_sendPackets.at(row);
    if (!packet.enabled) {
        showWarning(AppI18n::text("无法发送"), AppI18n::text("该常用包已停用"));
        return;
    }
    sendPacketPayload(packet);
}

bool WorkbenchPage::sendPacketPayload(const SendPacket &packet)
{
    bool ok = false;
    const QByteArray payload = payloadFromText(packet.payload, packet.mode, packet.lineEnding, false, &ok);
    if (!ok) {
        return false;
    }
    if (payload.isEmpty()) {
        showWarning(AppI18n::text("无法发送"), AppI18n::text("常用包内容为空"));
        return false;
    }
    bool checksumOk = false;
    const QByteArray output = payloadWithOptionalChecksum(payload, &checksumOk);
    if (!checksumOk) {
        return false;
    }

    QString error;
    if (!m_serial.writeData(output, &error)) {
        showError(AppI18n::text("发送失败"), error);
        return false;
    }

    SendHistoryItem historyItem;
    historyItem.mode = packet.mode;
    historyItem.payload = packet.payload;
    historyItem.lineEnding = packet.lineEnding;
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, output);
    return true;
}

void WorkbenchPage::sendCurrentPayload()
{
    if (!m_serial.isOpen()) {
        if (m_loopCheck->isChecked()) {
            m_loopCheck->setChecked(false);
        }
        showWarning(AppI18n::text("无法发送"), AppI18n::text("请先连接串口"));
        return;
    }

    bool ok = false;
    const QByteArray payload = currentPayload(&ok);
    if (!ok) {
        return;
    }
    if (payload.isEmpty()) {
        showWarning(AppI18n::text("无法发送"), AppI18n::text("发送内容为空"));
        return;
    }
    bool checksumOk = false;
    const QByteArray output = payloadWithOptionalChecksum(payload, &checksumOk);
    if (!checksumOk) {
        return;
    }

    QString error;
    if (!m_serial.writeData(output, &error)) {
        showError(AppI18n::text("发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text");
    historyItem.payload = m_sendEdit->toPlainText();
    historyItem.lineEnding = selectedLineEndingKey();
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, output);
}

void WorkbenchPage::addSendHistory(const SendHistoryItem &item)
{
    if (item.payload.trimmed().isEmpty()) {
        return;
    }

    for (int i = m_sendHistory.size() - 1; i >= 0; --i) {
        const SendHistoryItem existing = m_sendHistory.at(i);
        if (existing.mode == item.mode && existing.payload == item.payload && existing.lineEnding == item.lineEnding) {
            m_sendHistory.removeAt(i);
        }
    }
    m_sendHistory.prepend(item);
    while (m_sendHistory.size() > MaxSendHistoryItems) {
        m_sendHistory.removeLast();
    }
    updateHistoryCombo();
    saveSendHistory();
}

void WorkbenchPage::loadSendHistory()
{
    m_sendHistory.clear();
    AppSettings settings;
    const QByteArray json = settings.value(QStringLiteral("send/history")).toString().toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray()) {
        return;
    }

    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        SendHistoryItem item;
        item.mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("text"));
        item.payload = object.value(QStringLiteral("payload")).toString();
        item.lineEnding = object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none"));
        if (!item.payload.isEmpty()) {
            m_sendHistory.append(item);
        }
        if (m_sendHistory.size() >= MaxSendHistoryItems) {
            break;
        }
    }
}

void WorkbenchPage::saveSendHistory() const
{
    QJsonArray array;
    for (const SendHistoryItem &item : m_sendHistory) {
        QJsonObject object;
        object.insert(QStringLiteral("mode"), item.mode);
        object.insert(QStringLiteral("payload"), item.payload);
        object.insert(QStringLiteral("lineEnding"), item.lineEnding);
        array.append(object);
    }

    AppSettings settings;
    settings.setValue(QStringLiteral("send/history"),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WorkbenchPage::loadSendPackets()
{
    m_sendPackets.clear();
    AppSettings settings;
    const QByteArray json = settings.value(QStringLiteral("send/packets")).toString().toUtf8();
    if (json.isEmpty()) {
        updatePacketTable();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        updatePacketTable();
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != SendPacketSchemaVersion ||
        !root.value(QStringLiteral("packets")).isArray()) {
        updatePacketTable();
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("packets")).toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        SendPacket packet;
        packet.group = object.value(QStringLiteral("group")).toString().trimmed();
        packet.name = object.value(QStringLiteral("name")).toString().trimmed();
        packet.note = object.value(QStringLiteral("note")).toString().trimmed();
        packet.mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("text"));
        packet.payload = object.value(QStringLiteral("payload")).toString();
        packet.lineEnding = object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none"));
        packet.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        if (packet.name.isEmpty() || packet.payload.isEmpty()) {
            continue;
        }
        if (packet.mode != QStringLiteral("hex")) {
            packet.mode = QStringLiteral("text");
        }
        if (packet.lineEnding != QStringLiteral("cr") && packet.lineEnding != QStringLiteral("lf") &&
            packet.lineEnding != QStringLiteral("crlf")) {
            packet.lineEnding = QStringLiteral("none");
        }
        m_sendPackets.append(packet);
        if (m_sendPackets.size() >= MaxSendPacketItems) {
            break;
        }
    }
    updatePacketTable();
}

void WorkbenchPage::saveSendPackets() const
{
    QJsonArray array;
    for (const SendPacket &packet : m_sendPackets) {
        QJsonObject object;
        object.insert(QStringLiteral("group"), packet.group);
        object.insert(QStringLiteral("name"), packet.name);
        object.insert(QStringLiteral("note"), packet.note);
        object.insert(QStringLiteral("mode"), packet.mode);
        object.insert(QStringLiteral("payload"), packet.payload);
        object.insert(QStringLiteral("lineEnding"), packet.lineEnding);
        object.insert(QStringLiteral("enabled"), packet.enabled);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), PacketSchemaName);
    root.insert(QStringLiteral("version"), SendPacketSchemaVersion);
    root.insert(QStringLiteral("packets"), array);

    AppSettings settings;
    settings.setValue(QStringLiteral("send/packets"),
                      QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void WorkbenchPage::importSendPackets()
{
    AppSettings settings;
    const QString initialFolder = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    const QString path = QFileDialog::getOpenFileName(window(), AppI18n::text("导入常用包"), initialFolder,
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showError(AppI18n::text("导入失败"), file.errorString());
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        showError(AppI18n::text("导入失败"), AppI18n::text("JSON 格式无效"));
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != SendPacketSchemaVersion ||
        !root.value(QStringLiteral("packets")).isArray()) {
        showError(AppI18n::text("导入失败"), AppI18n::text("不是常用包 JSON v1 格式"));
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("packets")).toArray();
    int added = 0;
    int replaced = 0;
    int skipped = 0;
    int selectedRow = -1;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            ++skipped;
            continue;
        }

        const QJsonObject object = value.toObject();
        SendPacket packet;
        packet.group = object.value(QStringLiteral("group")).toString().trimmed();
        packet.name = object.value(QStringLiteral("name")).toString().trimmed();
        packet.note = object.value(QStringLiteral("note")).toString().trimmed();
        packet.mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("text"));
        packet.payload = object.value(QStringLiteral("payload")).toString();
        packet.lineEnding = object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none"));
        packet.enabled = object.value(QStringLiteral("enabled")).toBool(true);

        if (packet.name.isEmpty() || packet.payload.isEmpty()) {
            ++skipped;
            continue;
        }
        if (packet.mode != QStringLiteral("hex")) {
            packet.mode = QStringLiteral("text");
        }
        if (packet.lineEnding != QStringLiteral("cr") && packet.lineEnding != QStringLiteral("lf") &&
            packet.lineEnding != QStringLiteral("crlf")) {
            packet.lineEnding = QStringLiteral("none");
        }

        int targetRow = -1;
        for (int i = 0; i < m_sendPackets.size(); ++i) {
            const SendPacket &existing = m_sendPackets.at(i);
            if (existing.group == packet.group && existing.name == packet.name) {
                targetRow = i;
                break;
            }
        }

        if (targetRow >= 0) {
            m_sendPackets[targetRow] = packet;
            ++replaced;
        } else {
            if (m_sendPackets.size() >= MaxSendPacketItems) {
                ++skipped;
                continue;
            }
            m_sendPackets.append(packet);
            targetRow = m_sendPackets.size() - 1;
            ++added;
        }
        if (selectedRow < 0) {
            selectedRow = targetRow;
        }
    }

    if (added == 0 && replaced == 0) {
        showWarning(AppI18n::text("没有导入常用包"), AppI18n::text("文件中没有有效条目"));
        return;
    }

    settings.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    updatePacketTable(selectedRow);
    applyPacket(selectedRow);
    saveSendPackets();
    showSuccess(AppI18n::text("导入完成"),
                AppI18n::text("新增 %1 条，更新 %2 条，跳过 %3 条").arg(added).arg(replaced).arg(skipped));
}

void WorkbenchPage::exportSendPackets()
{
    if (m_sendPackets.isEmpty()) {
        showWarning(AppI18n::text("没有可导出的常用包"), AppI18n::text("请先保存常用包"));
        return;
    }

    AppSettings settings;
    const QString initialFolder = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    const QString initialName =
        QDir(initialFolder)
            .filePath(QStringLiteral("send_packets_%1.json")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    QString path = QFileDialog::getSaveFileName(window(), AppI18n::text("导出常用包"), initialName,
                                                QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path.append(QStringLiteral(".json"));
    }

    QJsonArray array;
    for (const SendPacket &packet : m_sendPackets) {
        QJsonObject object;
        object.insert(QStringLiteral("group"), packet.group);
        object.insert(QStringLiteral("name"), packet.name);
        object.insert(QStringLiteral("note"), packet.note);
        object.insert(QStringLiteral("mode"), packet.mode);
        object.insert(QStringLiteral("payload"), packet.payload);
        object.insert(QStringLiteral("lineEnding"), packet.lineEnding);
        object.insert(QStringLiteral("enabled"), packet.enabled);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), PacketSchemaName);
    root.insert(QStringLiteral("version"), SendPacketSchemaVersion);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    root.insert(QStringLiteral("packets"), array);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(AppI18n::text("导出失败"), file.errorString());
        return;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        showError(AppI18n::text("导出失败"), file.errorString());
        return;
    }

    settings.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    showSuccess(AppI18n::text("导出完成"), path);
}
