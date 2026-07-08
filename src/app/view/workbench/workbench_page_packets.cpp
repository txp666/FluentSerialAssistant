#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

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

        auto *item =
            new QListWidgetItem(icon(FluentIcon::CommandPrompt), QStringLiteral("%1\n%2 · %3 · %4")
                                                                     .arg(packet.name, modeLabel(packet.mode),
                                                                          lineEndingLabel(packet.lineEnding), payload));
        item->setData(Qt::UserRole, row);
        item->setToolTip(packet.payload);
        item->setSizeHint(QSize(0, 52));
        m_packetList->addItem(item);
    }

    if (!m_sendPackets.isEmpty()) {
        const int row =
            qBound(0, selectedRow >= 0 ? selectedRow : m_packetList->currentRow(), m_sendPackets.size() - 1);
        m_packetList->setCurrentRow(row);
    }
    const bool hasPacket = !m_sendPackets.isEmpty();
    if (m_packetLoadButton) {
        m_packetLoadButton->setEnabled(hasPacket);
    }
    if (m_packetDeleteButton) {
        m_packetDeleteButton->setEnabled(hasPacket);
    }
    if (m_packetSendButton) {
        m_packetSendButton->setEnabled(hasPacket && m_serial.isOpen());
    }
    if (m_packetUpButton) {
        m_packetUpButton->setEnabled(hasPacket);
    }
    if (m_packetDownButton) {
        m_packetDownButton->setEnabled(hasPacket);
    }
}

void WorkbenchPage::applyPacket(int row)
{
    if (row < 0 || row >= m_sendPackets.size()) {
        return;
    }

    const SendPacket packet = m_sendPackets.at(row);
    m_packetNameEdit->setText(packet.name);
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
        showWarning(QStringLiteral("无法保存"), QStringLiteral("发送内容为空"));
        return;
    }

    QString name = m_packetNameEdit->text().trimmed();
    if (name.isEmpty()) {
        name = payload.simplified();
        if (name.size() > 18) {
            name = name.left(18) + QStringLiteral("...");
        }
        if (name.isEmpty()) {
            name = QStringLiteral("未命名包");
        }
        m_packetNameEdit->setText(name);
    }

    SendPacket packet;
    packet.name = name;
    packet.mode = m_packetModeCombo->currentData().toString();
    packet.payload = payload;
    packet.lineEnding = m_packetLineEndingCombo->currentData().toString();
    packet.enabled = true;

    int targetRow = -1;
    for (int i = 0; i < m_sendPackets.size(); ++i) {
        if (m_sendPackets.at(i).name == packet.name) {
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
    showSuccess(QStringLiteral("已保存常用包"), packet.name);
}

void WorkbenchPage::removeSelectedPacket()
{
    const int row = m_packetList ? m_packetList->currentRow() : -1;
    if (row < 0 || row >= m_sendPackets.size()) {
        return;
    }
    const QString name = m_sendPackets.at(row).name;
    m_sendPackets.removeAt(row);
    updatePacketTable(qMin(row, m_sendPackets.size() - 1));
    saveSendPackets();
    showInfo(QStringLiteral("已删除常用包"), name);
}

void WorkbenchPage::moveSelectedPacket(int direction)
{
    const int row = m_packetList ? m_packetList->currentRow() : -1;
    const int target = row + direction;
    if (row < 0 || row >= m_sendPackets.size() || target < 0 || target >= m_sendPackets.size()) {
        return;
    }

    m_sendPackets.swapItemsAt(row, target);
    updatePacketTable(target);
    saveSendPackets();
}

void WorkbenchPage::sendSelectedPacket() { sendPacket(m_packetList ? m_packetList->currentRow() : -1); }

void WorkbenchPage::sendPacket(int row)
{
    if (row < 0 || row >= m_sendPackets.size()) {
        return;
    }
    if (!m_serial.isOpen()) {
        showWarning(QStringLiteral("无法发送"), QStringLiteral("请先连接串口"));
        return;
    }

    const SendPacket packet = m_sendPackets.at(row);
    bool ok = false;
    const QByteArray payload = payloadFromText(packet.payload, packet.mode, packet.lineEnding, false, &ok);
    if (!ok) {
        return;
    }
    if (payload.isEmpty()) {
        showWarning(QStringLiteral("无法发送"), QStringLiteral("常用包内容为空"));
        return;
    }

    QString error;
    if (!m_serial.writeData(payload, &error)) {
        showError(QStringLiteral("发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = packet.mode;
    historyItem.payload = packet.payload;
    historyItem.lineEnding = packet.lineEnding;
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, payload);
}

void WorkbenchPage::sendCurrentPayload()
{
    if (!m_serial.isOpen()) {
        if (m_loopCheck->isChecked()) {
            m_loopCheck->setChecked(false);
        }
        showWarning(QStringLiteral("无法发送"), QStringLiteral("请先连接串口"));
        return;
    }

    bool ok = false;
    const QByteArray payload = currentPayload(&ok);
    if (!ok) {
        return;
    }
    if (payload.isEmpty()) {
        showWarning(QStringLiteral("无法发送"), QStringLiteral("发送内容为空"));
        return;
    }

    QString error;
    if (!m_serial.writeData(payload, &error)) {
        showError(QStringLiteral("发送失败"), error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = m_hexSendCheck->isChecked() ? QStringLiteral("hex") : QStringLiteral("text");
    historyItem.payload = m_sendEdit->toPlainText();
    historyItem.lineEnding = selectedLineEndingKey();
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, payload);
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
    QSettings settings;
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

    QSettings settings;
    settings.setValue(QStringLiteral("send/history"),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void WorkbenchPage::loadSendPackets()
{
    m_sendPackets.clear();
    QSettings settings;
    const QByteArray json = settings.value(QStringLiteral("send/packets")).toString().toUtf8();
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isArray()) {
        updatePacketTable();
        return;
    }

    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        SendPacket packet;
        packet.name = object.value(QStringLiteral("name")).toString().trimmed();
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
        if (packet.lineEnding.isEmpty()) {
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
        object.insert(QStringLiteral("name"), packet.name);
        object.insert(QStringLiteral("mode"), packet.mode);
        object.insert(QStringLiteral("payload"), packet.payload);
        object.insert(QStringLiteral("lineEnding"), packet.lineEnding);
        object.insert(QStringLiteral("enabled"), packet.enabled);
        array.append(object);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("send/packets"),
                      QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}
