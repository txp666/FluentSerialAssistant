#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {
constexpr int AutoReplyRuleSchemaVersion = 1;
const QString AutoReplyRuleSchemaName = QStringLiteral("fluent-serial-assistant.auto-reply-rules");

QString normalizedMode(QString value)
{
    return value == QStringLiteral("hex") ? QStringLiteral("hex") : QStringLiteral("text");
}

QString normalizedMatchMode(QString value)
{
    if (value == QStringLiteral("hex") || value == QStringLiteral("regex")) {
        return value;
    }
    return QStringLiteral("text");
}

QString normalizedLineEnding(QString value)
{
    if (value == QStringLiteral("cr") || value == QStringLiteral("lf") || value == QStringLiteral("crlf")) {
        return value;
    }
    return QStringLiteral("none");
}

QString matchModeLabel(const QString &mode)
{
    if (mode == QStringLiteral("hex")) {
        return QStringLiteral("HEX");
    }
    if (mode == QStringLiteral("regex")) {
        return QStringLiteral("正则");
    }
    return QStringLiteral("文本");
}

QString autoReplyRuleName(const QString &name, int row)
{
    if (!name.trimmed().isEmpty()) {
        return name.trimmed();
    }
    return QStringLiteral("规则 %1").arg(row + 1);
}
} // namespace

void WorkbenchPage::updateAutoReplyTable(int selectedRow)
{
    if (!m_autoReplyList) {
        return;
    }

    const QSignalBlocker blocker(m_autoReplyList);
    m_autoReplyList->clear();
    for (int row = 0; row < m_autoReplyRules.size(); ++row) {
        const AutoReplyRule &rule = m_autoReplyRules.at(row);
        QString pattern = rule.pattern.simplified();
        if (pattern.size() > 22) {
            pattern = pattern.left(19) + QStringLiteral("...");
        }
        QString payload = rule.responsePayload.simplified();
        if (payload.size() > 22) {
            payload = payload.left(19) + QStringLiteral("...");
        }
        const QString status = rule.enabled ? QStringLiteral("启用") : QStringLiteral("停用");

        auto *item = new QListWidgetItem(
            icon(rule.enabled ? FluentIcon::Feedback : FluentIcon::Cancel),
            QStringLiteral("%1. %2 · %3\n%4：%5 → %6 · 延时 %7 ms")
                .arg(row + 1)
                .arg(autoReplyRuleName(rule.name, row), status, matchModeLabel(rule.matchMode), pattern, payload)
                .arg(qBound(0, rule.delayMs, 600000)));
        item->setData(Qt::UserRole, row);
        item->setToolTip(QStringLiteral("匹配：%1\n应答：%2").arg(rule.pattern, rule.responsePayload));
        item->setSizeHint(QSize(0, 58));
        m_autoReplyList->addItem(item);
    }

    if (!m_autoReplyRules.isEmpty()) {
        const int row =
            qBound(0, selectedRow >= 0 ? selectedRow : m_autoReplyList->currentRow(), m_autoReplyRules.size() - 1);
        m_autoReplyList->setCurrentRow(row);
    }
    updateAutoReplyActionState();
}

void WorkbenchPage::updateAutoReplyActionState()
{
    const int row = m_autoReplyList ? m_autoReplyList->currentRow() : -1;
    const bool hasCurrent = row >= 0 && row < m_autoReplyRules.size();
    if (m_autoReplyLoadButton) {
        m_autoReplyLoadButton->setEnabled(hasCurrent);
    }
    if (m_autoReplyDeleteButton) {
        m_autoReplyDeleteButton->setEnabled(hasCurrent);
    }
    if (m_autoReplyUpButton) {
        m_autoReplyUpButton->setEnabled(hasCurrent && row > 0);
    }
    if (m_autoReplyDownButton) {
        m_autoReplyDownButton->setEnabled(hasCurrent && row < m_autoReplyRules.size() - 1);
    }
}

void WorkbenchPage::applyAutoReplyRule(int row)
{
    if (row < 0 || row >= m_autoReplyRules.size()) {
        return;
    }

    const AutoReplyRule rule = m_autoReplyRules.at(row);
    m_autoReplyNameEdit->setText(rule.name);
    m_autoReplyEnabledCheck->setChecked(rule.enabled);
    m_autoReplyPatternEdit->setText(rule.pattern);
    const int matchModeIndex = m_autoReplyMatchModeCombo->findData(rule.matchMode);
    if (matchModeIndex >= 0) {
        m_autoReplyMatchModeCombo->setCurrentIndex(matchModeIndex);
    }
    const int responseModeIndex = m_autoReplyResponseModeCombo->findData(rule.responseMode);
    if (responseModeIndex >= 0) {
        m_autoReplyResponseModeCombo->setCurrentIndex(responseModeIndex);
    }
    const int lineEndingIndex = m_autoReplyLineEndingCombo->findData(rule.lineEnding);
    if (lineEndingIndex >= 0) {
        m_autoReplyLineEndingCombo->setCurrentIndex(lineEndingIndex);
    }
    m_autoReplyPayloadEdit->setPlainText(rule.responsePayload);
    m_autoReplyDelaySpin->setValue(qBound(0, rule.delayMs, 600000));
}

void WorkbenchPage::saveCurrentAutoReplyRule()
{
    AutoReplyRule rule;
    rule.name = m_autoReplyNameEdit->text().trimmed();
    rule.enabled = m_autoReplyEnabledCheck->isChecked();
    rule.matchMode = normalizedMatchMode(m_autoReplyMatchModeCombo->currentData().toString());
    rule.pattern = m_autoReplyPatternEdit->text();
    rule.responseMode = normalizedMode(m_autoReplyResponseModeCombo->currentData().toString());
    rule.responsePayload = m_autoReplyPayloadEdit->toPlainText();
    rule.lineEnding = normalizedLineEnding(m_autoReplyLineEndingCombo->currentData().toString());
    rule.delayMs = qBound(0, m_autoReplyDelaySpin->value(), 600000);

    if (rule.pattern.isEmpty()) {
        showWarning(QStringLiteral("无法保存自动应答"), QStringLiteral("匹配内容为空"));
        return;
    }
    if (rule.responsePayload.trimmed().isEmpty()) {
        showWarning(QStringLiteral("无法保存自动应答"), QStringLiteral("应答内容为空"));
        return;
    }
    if (rule.matchMode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(rule.pattern);
        if (!result.ok || result.bytes.isEmpty()) {
            showWarning(QStringLiteral("HEX 匹配无效"), result.errorMessage);
            return;
        }
    } else if (rule.matchMode == QStringLiteral("regex")) {
        const QRegularExpression regex(rule.pattern);
        if (!regex.isValid()) {
            showWarning(QStringLiteral("正则匹配无效"), regex.errorString());
            return;
        }
    }
    if (rule.responseMode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(rule.responsePayload);
        if (!result.ok || result.bytes.isEmpty()) {
            showWarning(QStringLiteral("HEX 应答无效"), result.errorMessage);
            return;
        }
    }

    if (rule.name.isEmpty()) {
        rule.name = rule.pattern.simplified();
        if (rule.name.size() > 18) {
            rule.name = rule.name.left(18) + QStringLiteral("...");
        }
        m_autoReplyNameEdit->setText(rule.name);
    }

    int row = m_autoReplyList ? m_autoReplyList->currentRow() : -1;
    if (row >= 0 && row < m_autoReplyRules.size()) {
        m_autoReplyRules[row] = rule;
    } else {
        m_autoReplyRules.append(rule);
        row = m_autoReplyRules.size() - 1;
    }

    updateAutoReplyTable(row);
    applyAutoReplyRule(row);
    saveAutoReplyRules();
    showSuccess(QStringLiteral("已保存自动应答"), autoReplyRuleName(rule.name, row));
}

void WorkbenchPage::removeSelectedAutoReplyRule()
{
    const int row = m_autoReplyList ? m_autoReplyList->currentRow() : -1;
    if (row < 0 || row >= m_autoReplyRules.size()) {
        return;
    }

    const QString name = autoReplyRuleName(m_autoReplyRules.at(row).name, row);
    m_autoReplyRules.removeAt(row);
    updateAutoReplyTable(qMin(row, m_autoReplyRules.size() - 1));
    saveAutoReplyRules();
    showInfo(QStringLiteral("已删除自动应答"), name);
}

void WorkbenchPage::moveSelectedAutoReplyRule(int direction)
{
    const int row = m_autoReplyList ? m_autoReplyList->currentRow() : -1;
    const int target = row + direction;
    if (row < 0 || row >= m_autoReplyRules.size() || target < 0 || target >= m_autoReplyRules.size()) {
        return;
    }

    m_autoReplyRules.swapItemsAt(row, target);
    updateAutoReplyTable(target);
    saveAutoReplyRules();
}

void WorkbenchPage::handleAutoReplyReceivedData(const QByteArray &data)
{
    if (data.isEmpty() || m_autoReplyRules.isEmpty() || !m_serial.isOpen()) {
        return;
    }

    m_autoReplyBuffer.append(data);
    if (m_autoReplyBuffer.size() > MaxFrameBufferBytes) {
        m_autoReplyBuffer.remove(0, m_autoReplyBuffer.size() - MaxFrameBufferBytes);
    }

    QList<AutoReplyRule> matchedRules;
    for (const AutoReplyRule &rule : m_autoReplyRules) {
        if (rule.enabled && autoReplyRuleMatches(rule, m_autoReplyBuffer)) {
            matchedRules.append(rule);
        }
    }
    if (matchedRules.isEmpty()) {
        return;
    }

    m_autoReplyBuffer.clear();
    QStringList names;
    names.reserve(matchedRules.size());
    for (const AutoReplyRule &rule : matchedRules) {
        names.append(rule.name.isEmpty() ? rule.pattern.simplified() : rule.name);
        const int delayMs = qBound(0, rule.delayMs, 600000);
        if (delayMs <= 0) {
            sendAutoReplyRule(rule);
        } else {
            QTimer::singleShot(delayMs, this, [this, rule]() { sendAutoReplyRule(rule); });
        }
    }

    if (m_autoReplyStatusLabel) {
        m_autoReplyStatusLabel->setText(QStringLiteral("已匹配：%1").arg(names.join(QStringLiteral("、"))));
    }
}

bool WorkbenchPage::autoReplyRuleMatches(const AutoReplyRule &rule, const QByteArray &buffer) const
{
    if (rule.pattern.isEmpty() || buffer.isEmpty()) {
        return false;
    }
    if (rule.matchMode == QStringLiteral("hex")) {
        const HexParseResult result = parseHexPayload(rule.pattern);
        return result.ok && !result.bytes.isEmpty() && buffer.contains(result.bytes);
    }

    const QString decoded = AppTextEncoding::decode(buffer, receiveEncodingKey());
    if (rule.matchMode == QStringLiteral("regex")) {
        const QRegularExpression regex(rule.pattern);
        return regex.isValid() && regex.match(decoded).hasMatch();
    }
    return decoded.contains(rule.pattern);
}

void WorkbenchPage::sendAutoReplyRule(const AutoReplyRule &rule)
{
    if (!m_serial.isOpen()) {
        if (m_autoReplyStatusLabel) {
            m_autoReplyStatusLabel->setText(QStringLiteral("串口已断开，自动应答未发送"));
        }
        return;
    }

    bool ok = false;
    const QByteArray payload = payloadFromText(rule.responsePayload, rule.responseMode, rule.lineEnding, false, &ok);
    if (!ok || payload.isEmpty()) {
        if (m_autoReplyStatusLabel) {
            m_autoReplyStatusLabel->setText(QStringLiteral("自动应答内容无效：%1").arg(rule.name));
        }
        return;
    }
    bool checksumOk = false;
    const QByteArray output = payloadWithOptionalChecksum(payload, &checksumOk);
    if (!checksumOk) {
        if (m_autoReplyStatusLabel) {
            m_autoReplyStatusLabel->setText(QStringLiteral("自动应答校验失败：%1").arg(rule.name));
        }
        return;
    }

    QString error;
    if (!m_serial.writeData(output, &error)) {
        if (m_autoReplyStatusLabel) {
            m_autoReplyStatusLabel->setText(QStringLiteral("自动应答发送失败：%1").arg(error));
        }
        return;
    }

    appendRecord(RecordDirection::Tx, output, true, QStringLiteral("自动应答"));
    if (m_autoReplyStatusLabel) {
        const QString name = rule.name.isEmpty() ? rule.pattern.simplified() : rule.name;
        m_autoReplyStatusLabel->setText(QStringLiteral("已应答：%1").arg(name));
    }
}

void WorkbenchPage::loadAutoReplyRules()
{
    m_autoReplyRules.clear();
    QSettings settings;
    const QByteArray json = settings.value(QStringLiteral("autoReply/rules")).toString().toUtf8();
    if (json.isEmpty()) {
        updateAutoReplyTable();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        updateAutoReplyTable();
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != AutoReplyRuleSchemaVersion ||
        !root.value(QStringLiteral("rules")).isArray()) {
        updateAutoReplyTable();
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("rules")).toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        AutoReplyRule rule;
        rule.name = object.value(QStringLiteral("name")).toString().trimmed();
        rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        rule.matchMode =
            normalizedMatchMode(object.value(QStringLiteral("matchMode")).toString(QStringLiteral("text")));
        rule.pattern = object.value(QStringLiteral("pattern")).toString();
        rule.responseMode =
            normalizedMode(object.value(QStringLiteral("responseMode")).toString(QStringLiteral("text")));
        rule.responsePayload = object.value(QStringLiteral("responsePayload")).toString();
        rule.lineEnding =
            normalizedLineEnding(object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none")));
        rule.delayMs = qBound(0, object.value(QStringLiteral("delayMs")).toInt(0), 600000);
        if (rule.pattern.isEmpty() || rule.responsePayload.trimmed().isEmpty()) {
            continue;
        }
        m_autoReplyRules.append(rule);
    }
    updateAutoReplyTable();
}

void WorkbenchPage::saveAutoReplyRules() const
{
    QJsonArray array;
    for (const AutoReplyRule &rule : m_autoReplyRules) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), rule.name);
        object.insert(QStringLiteral("enabled"), rule.enabled);
        object.insert(QStringLiteral("matchMode"), rule.matchMode);
        object.insert(QStringLiteral("pattern"), rule.pattern);
        object.insert(QStringLiteral("responseMode"), rule.responseMode);
        object.insert(QStringLiteral("responsePayload"), rule.responsePayload);
        object.insert(QStringLiteral("lineEnding"), rule.lineEnding);
        object.insert(QStringLiteral("delayMs"), rule.delayMs);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), AutoReplyRuleSchemaName);
    root.insert(QStringLiteral("version"), AutoReplyRuleSchemaVersion);
    root.insert(QStringLiteral("rules"), array);

    QSettings settings;
    settings.setValue(QStringLiteral("autoReply/rules"),
                      QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}
