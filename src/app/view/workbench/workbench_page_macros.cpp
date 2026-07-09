#include "app/view/workbench/workbench_page_internal.h"
#include "app/core/app_i18n.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

namespace {
constexpr int MacroStepSchemaVersion = 1;
const QString MacroStepSchemaName = QStringLiteral("fluent-serial-assistant.macro-steps");

QString responseModeLabel(const QString &mode)
{
    return mode == QStringLiteral("hex") ? QStringLiteral("HEX") : AppI18n::text("文本");
}

QString normalizedLineEnding(QString value)
{
    if (value == QStringLiteral("cr") || value == QStringLiteral("lf") || value == QStringLiteral("crlf")) {
        return value;
    }
    return QStringLiteral("none");
}

QString normalizedMode(QString value)
{
    return value == QStringLiteral("hex") ? QStringLiteral("hex") : QStringLiteral("text");
}

QString macroStepName(const QString &name, int row)
{
    if (!name.trimmed().isEmpty()) {
        return name.trimmed();
    }
    return AppI18n::text("步骤 %1").arg(row + 1);
}
} // namespace

void WorkbenchPage::updateMacroTable(int selectedRow)
{
    if (!m_macroList) {
        return;
    }

    const QSignalBlocker blocker(m_macroList);
    m_macroList->clear();
    for (int row = 0; row < m_macroSteps.size(); ++row) {
        const MacroStep &step = m_macroSteps.at(row);
        QString payload = step.payload.simplified();
        if (payload.size() > 30) {
            payload = payload.left(27) + QStringLiteral("...");
        }
        QString expected = step.expectedResponse.simplified();
        if (expected.size() > 18) {
            expected = expected.left(15) + QStringLiteral("...");
        }
        const QString waitText = expected.isEmpty() ? AppI18n::text("不等待")
                                                    : AppI18n::text("等待 %1 · %2 ms")
                                                          .arg(responseModeLabel(step.responseMode))
                                                          .arg(qBound(1, step.timeoutMs, 600000));

        auto *item =
            new QListWidgetItem(icon(FluentIcon::Play), AppI18n::text("%1. %2\n%3 · %4 · %5 · 延时 %6 ms")
                                                            .arg(row + 1)
                                                            .arg(macroStepName(step.name, row), modeLabel(step.mode),
                                                                 lineEndingLabel(step.lineEnding), waitText)
                                                            .arg(qBound(0, step.delayMs, 600000)));
        item->setData(Qt::UserRole, row);
        item->setToolTip(AppI18n::text("发送：%1\n期望响应：%2").arg(step.payload, step.expectedResponse));
        item->setSizeHint(QSize(0, 58));
        m_macroList->addItem(item);
    }

    if (!m_macroSteps.isEmpty()) {
        const int row = qBound(0, selectedRow >= 0 ? selectedRow : m_macroList->currentRow(), m_macroSteps.size() - 1);
        m_macroList->setCurrentRow(row);
    }
    updateMacroActionState();
}

void WorkbenchPage::updateMacroActionState()
{
    const bool hasSteps = !m_macroSteps.isEmpty();
    const int row = m_macroList ? m_macroList->currentRow() : -1;
    const bool hasCurrent = row >= 0 && row < m_macroSteps.size();
    const bool canEdit = !m_macroRunning;

    if (m_macroNameEdit) {
        m_macroNameEdit->setEnabled(canEdit);
    }
    if (m_macroModeCombo) {
        m_macroModeCombo->setEnabled(canEdit);
    }
    if (m_macroLineEndingCombo) {
        m_macroLineEndingCombo->setEnabled(canEdit);
    }
    if (m_macroPayloadEdit) {
        m_macroPayloadEdit->setEnabled(canEdit);
    }
    if (m_macroExpectedEdit) {
        m_macroExpectedEdit->setEnabled(canEdit);
    }
    if (m_macroResponseModeCombo) {
        m_macroResponseModeCombo->setEnabled(canEdit);
    }
    if (m_macroTimeoutEdit) {
        m_macroTimeoutEdit->setEnabled(canEdit);
    }
    if (m_macroDelayEdit) {
        m_macroDelayEdit->setEnabled(canEdit);
    }
    if (m_macroList) {
        m_macroList->setEnabled(canEdit);
    }
    if (m_macroSaveButton) {
        m_macroSaveButton->setEnabled(canEdit);
    }
    if (m_macroLoadButton) {
        m_macroLoadButton->setEnabled(canEdit && hasCurrent);
    }
    if (m_macroDeleteButton) {
        m_macroDeleteButton->setEnabled(canEdit && hasCurrent);
    }
    if (m_macroUpButton) {
        m_macroUpButton->setEnabled(canEdit && hasCurrent && row > 0);
    }
    if (m_macroDownButton) {
        m_macroDownButton->setEnabled(canEdit && hasCurrent && row < m_macroSteps.size() - 1);
    }
    if (m_macroLoopCountEdit) {
        m_macroLoopCountEdit->setEnabled(canEdit);
    }
    if (m_macroAbortOnFailureCheck) {
        m_macroAbortOnFailureCheck->setEnabled(canEdit);
    }
    if (m_macroRunButton) {
        m_macroRunButton->setEnabled(canEdit && hasSteps && m_serial.isOpen());
    }
    if (m_macroStopButton) {
        m_macroStopButton->setEnabled(m_macroRunning);
    }
    if (m_macroExportButton) {
        m_macroExportButton->setEnabled(!m_macroResults.isEmpty());
    }
}

void WorkbenchPage::applyMacroStep(int row)
{
    if (row < 0 || row >= m_macroSteps.size()) {
        return;
    }

    const MacroStep step = m_macroSteps.at(row);
    m_macroNameEdit->setText(step.name);
    m_macroPayloadEdit->setPlainText(step.payload);
    const int modeIndex = m_macroModeCombo->findData(step.mode);
    if (modeIndex >= 0) {
        m_macroModeCombo->setCurrentIndex(modeIndex);
    }
    const int lineEndingIndex = m_macroLineEndingCombo->findData(step.lineEnding);
    if (lineEndingIndex >= 0) {
        m_macroLineEndingCombo->setCurrentIndex(lineEndingIndex);
    }
    m_macroExpectedEdit->setText(step.expectedResponse);
    const int responseModeIndex = m_macroResponseModeCombo->findData(step.responseMode);
    if (responseModeIndex >= 0) {
        m_macroResponseModeCombo->setCurrentIndex(responseModeIndex);
    }
    setNumberEditValue(m_macroTimeoutEdit, step.timeoutMs, 1, 600000);
    setNumberEditValue(m_macroDelayEdit, step.delayMs, 0, 600000);
}

void WorkbenchPage::saveCurrentMacroStep()
{
    const QString payload = m_macroPayloadEdit->toPlainText();
    if (payload.trimmed().isEmpty()) {
        showWarning(AppI18n::text("无法保存宏步骤"), AppI18n::text("发送内容为空"));
        return;
    }

    MacroStep step;
    step.name = m_macroNameEdit->text().trimmed();
    if (step.name.isEmpty()) {
        step.name = payload.simplified();
        if (step.name.size() > 18) {
            step.name = step.name.left(18) + QStringLiteral("...");
        }
        if (step.name.isEmpty()) {
            step.name = AppI18n::text("未命名步骤");
        }
        m_macroNameEdit->setText(step.name);
    }
    step.mode = normalizedMode(m_macroModeCombo->currentData().toString());
    step.payload = payload;
    step.lineEnding = normalizedLineEnding(m_macroLineEndingCombo->currentData().toString());
    step.responseMode = normalizedMode(m_macroResponseModeCombo->currentData().toString());
    step.expectedResponse = m_macroExpectedEdit->text();
    step.timeoutMs = numberEditValue(m_macroTimeoutEdit, 1000, 1, 600000);
    step.delayMs = numberEditValue(m_macroDelayEdit, 0, 0, 600000);

    int row = m_macroList ? m_macroList->currentRow() : -1;
    if (row >= 0 && row < m_macroSteps.size()) {
        m_macroSteps[row] = step;
    } else {
        m_macroSteps.append(step);
        row = m_macroSteps.size() - 1;
    }

    updateMacroTable(row);
    applyMacroStep(row);
    saveMacroSteps();
    showSuccess(AppI18n::text("已保存宏步骤"), macroStepName(step.name, row));
}

void WorkbenchPage::removeSelectedMacroStep()
{
    const int row = m_macroList ? m_macroList->currentRow() : -1;
    if (row < 0 || row >= m_macroSteps.size() || m_macroRunning) {
        return;
    }

    const QString name = macroStepName(m_macroSteps.at(row).name, row);
    m_macroSteps.removeAt(row);
    updateMacroTable(qMin(row, m_macroSteps.size() - 1));
    saveMacroSteps();
    showInfo(AppI18n::text("已删除宏步骤"), name);
}

void WorkbenchPage::moveSelectedMacroStep(int direction)
{
    const int row = m_macroList ? m_macroList->currentRow() : -1;
    const int target = row + direction;
    if (row < 0 || row >= m_macroSteps.size() || target < 0 || target >= m_macroSteps.size() || m_macroRunning) {
        return;
    }

    m_macroSteps.swapItemsAt(row, target);
    updateMacroTable(target);
    saveMacroSteps();
}

void WorkbenchPage::startMacroSequence()
{
    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法运行宏命令"), AppI18n::text("请先连接串口"));
        return;
    }
    if (m_macroSteps.isEmpty()) {
        showWarning(AppI18n::text("无法运行宏命令"), AppI18n::text("请先保存至少一个宏步骤"));
        return;
    }

    m_macroResults.clear();
    m_macroWaitBuffer.clear();
    m_macroLoopTotal = numberEditValue(m_macroLoopCountEdit, 1, 1, 100000);
    m_macroCurrentLoop = 1;
    m_macroCurrentStep = 0;
    m_macroActiveResult = -1;
    m_macroRunning = true;
    m_macroWaitingForResponse = false;
    updateMacroActionState();
    if (m_macroStatusLabel) {
        m_macroStatusLabel->setText(
            AppI18n::text("准备运行 %1 步 · %2 轮").arg(m_macroSteps.size()).arg(m_macroLoopTotal));
    }
    runMacroCurrentStep();
}

void WorkbenchPage::stopMacroSequence(const QString &message, bool userRequested)
{
    if (!m_macroRunning) {
        return;
    }

    if (m_macroActiveResult >= 0 && m_macroActiveResult < m_macroResults.size()) {
        MacroRunResult &result = m_macroResults[m_macroActiveResult];
        if (result.status == AppI18n::text("等待响应")) {
            result.status = AppI18n::text("已取消");
            result.message = message.isEmpty() ? AppI18n::text("已停止") : message;
            result.rxBytes = m_macroWaitBuffer;
            result.elapsedMs = m_macroStepClock.isValid() ? static_cast<int>(m_macroStepClock.elapsed()) : 0;
        }
    }

    Q_UNUSED(userRequested)
    finishMacroSequence(message.isEmpty() ? AppI18n::text("已停止") : message, false);
}

void WorkbenchPage::runMacroCurrentStep()
{
    if (!m_macroRunning) {
        return;
    }
    if (!m_serial.isOpen()) {
        finishMacroSequence(AppI18n::text("串口已断开"), false);
        return;
    }

    if (m_macroCurrentStep >= m_macroSteps.size()) {
        ++m_macroCurrentLoop;
        m_macroCurrentStep = 0;
    }
    if (m_macroCurrentLoop > m_macroLoopTotal) {
        finishMacroSequence(AppI18n::text("全部步骤已完成"), true);
        return;
    }

    const MacroStep step = m_macroSteps.at(m_macroCurrentStep);
    const QString stepName = macroStepName(step.name, m_macroCurrentStep);
    m_macroStepClock.restart();
    if (m_macroStatusLabel) {
        m_macroStatusLabel->setText(
            AppI18n::text("第 %1/%2 轮 · %3").arg(m_macroCurrentLoop).arg(m_macroLoopTotal).arg(stepName));
    }

    bool expectedOk = true;
    QString expectedError;
    if (!step.expectedResponse.trimmed().isEmpty()) {
        expectedMacroResponseBytes(step, &expectedOk, &expectedError);
        if (!expectedOk) {
            MacroRunResult result;
            result.timestamp = QDateTime::currentDateTime();
            result.loop = m_macroCurrentLoop;
            result.step = m_macroCurrentStep + 1;
            result.stepName = stepName;
            result.status = AppI18n::text("失败");
            result.message = expectedError;
            m_macroResults.append(result);
            m_macroActiveResult = m_macroResults.size() - 1;
            completeMacroStep(false, expectedError);
            return;
        }
    }

    MacroRunResult result;
    result.timestamp = QDateTime::currentDateTime();
    result.loop = m_macroCurrentLoop;
    result.step = m_macroCurrentStep + 1;
    result.stepName = stepName;
    result.status = step.expectedResponse.trimmed().isEmpty() ? AppI18n::text("通过") : AppI18n::text("等待响应");
    result.message = step.expectedResponse.trimmed().isEmpty() ? AppI18n::text("已发送") : AppI18n::text("等待响应");

    bool ok = false;
    const QByteArray payload = payloadFromText(step.payload, step.mode, step.lineEnding, false, &ok);
    if (!ok || payload.isEmpty()) {
        m_macroResults.append(result);
        m_macroActiveResult = m_macroResults.size() - 1;
        completeMacroStep(false, payload.isEmpty() ? AppI18n::text("发送内容为空") : AppI18n::text("发送内容无效"));
        return;
    }

    bool checksumOk = false;
    const QByteArray output = payloadWithOptionalChecksum(payload, &checksumOk);
    if (!checksumOk) {
        m_macroResults.append(result);
        m_macroActiveResult = m_macroResults.size() - 1;
        completeMacroStep(false, AppI18n::text("校验码追加失败"));
        return;
    }

    QString error;
    if (!m_serial.writeData(output, &error)) {
        m_macroResults.append(result);
        m_macroActiveResult = m_macroResults.size() - 1;
        completeMacroStep(false, error);
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = step.mode;
    historyItem.payload = step.payload;
    historyItem.lineEnding = step.lineEnding;
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, output);

    result.txBytes = output;
    m_macroResults.append(result);
    m_macroActiveResult = m_macroResults.size() - 1;

    if (step.expectedResponse.trimmed().isEmpty()) {
        completeMacroStep(true, AppI18n::text("已发送"));
        return;
    }

    m_macroWaitBuffer.clear();
    m_macroWaitingForResponse = true;
    m_macroTimer.start(qBound(1, step.timeoutMs, 600000));
}

void WorkbenchPage::handleMacroTimer()
{
    if (!m_macroRunning) {
        return;
    }
    if (m_macroWaitingForResponse) {
        completeMacroStep(false, AppI18n::text("等待响应超时"), m_macroWaitBuffer);
        return;
    }
    runMacroCurrentStep();
}

void WorkbenchPage::handleMacroReceivedData(const QByteArray &data)
{
    if (!m_macroRunning || !m_macroWaitingForResponse || data.isEmpty() || m_macroCurrentStep >= m_macroSteps.size()) {
        return;
    }

    m_macroWaitBuffer.append(data);
    if (m_macroWaitBuffer.size() > MaxFrameBufferBytes) {
        m_macroWaitBuffer.remove(0, m_macroWaitBuffer.size() - MaxFrameBufferBytes);
    }

    if (macroResponseMatches(m_macroSteps.at(m_macroCurrentStep), m_macroWaitBuffer)) {
        completeMacroStep(true, AppI18n::text("响应匹配"), m_macroWaitBuffer);
    }
}

void WorkbenchPage::completeMacroStep(bool passed, const QString &message, const QByteArray &rxBytes)
{
    if (!m_macroRunning) {
        return;
    }

    const int stepIndex = m_macroCurrentStep;
    const MacroStep step = stepIndex >= 0 && stepIndex < m_macroSteps.size() ? m_macroSteps.at(stepIndex) : MacroStep();
    if (m_macroActiveResult >= 0 && m_macroActiveResult < m_macroResults.size()) {
        MacroRunResult &result = m_macroResults[m_macroActiveResult];
        result.status = passed ? AppI18n::text("通过") : AppI18n::text("失败");
        result.message = message;
        result.rxBytes = rxBytes;
        result.elapsedMs = m_macroStepClock.isValid() ? static_cast<int>(m_macroStepClock.elapsed()) : 0;
    }

    m_macroWaitingForResponse = false;
    m_macroTimer.stop();

    if (!passed && m_macroAbortOnFailureCheck && m_macroAbortOnFailureCheck->isChecked()) {
        finishMacroSequence(AppI18n::text("失败中止：%1").arg(message), false);
        return;
    }

    const bool hasNext = stepIndex + 1 < m_macroSteps.size() || m_macroCurrentLoop < m_macroLoopTotal;
    ++m_macroCurrentStep;
    const int delayMs = qBound(0, step.delayMs, 600000);
    if (hasNext && delayMs > 0) {
        if (m_macroStatusLabel) {
            m_macroStatusLabel->setText(AppI18n::text("步骤间延时 %1 ms").arg(delayMs));
        }
        m_macroTimer.start(delayMs);
        return;
    }
    runMacroCurrentStep();
}

void WorkbenchPage::finishMacroSequence(const QString &message, bool passed)
{
    m_macroTimer.stop();
    m_macroRunning = false;
    m_macroWaitingForResponse = false;
    m_macroActiveResult = -1;
    m_macroWaitBuffer.clear();

    int passedCount = 0;
    int failedCount = 0;
    for (const MacroRunResult &result : m_macroResults) {
        if (result.status == AppI18n::text("通过")) {
            ++passedCount;
        } else if (result.status == AppI18n::text("失败")) {
            ++failedCount;
        }
    }

    const QString summary = AppI18n::text("%1 · 通过 %2，失败 %3").arg(message).arg(passedCount).arg(failedCount);
    if (m_macroStatusLabel) {
        m_macroStatusLabel->setText(summary);
    }
    updateMacroActionState();

    if (passed) {
        showSuccess(AppI18n::text("宏命令完成"), summary);
    } else {
        showWarning(AppI18n::text("宏命令结束"), summary);
    }
}

QByteArray WorkbenchPage::expectedMacroResponseBytes(const MacroStep &step, bool *ok, QString *error) const
{
    if (ok) {
        *ok = true;
    }
    if (error) {
        error->clear();
    }

    const QString expected = step.expectedResponse.trimmed();
    if (expected.isEmpty()) {
        return {};
    }
    if (step.responseMode != QStringLiteral("hex")) {
        return expected.toUtf8();
    }

    const HexParseResult result = parseHexPayload(expected);
    if (result.ok && !result.bytes.isEmpty()) {
        return result.bytes;
    }
    if (ok) {
        *ok = false;
    }
    if (error) {
        *error = result.errorMessage.isEmpty() ? AppI18n::text("期望 HEX 响应无效") : result.errorMessage;
    }
    return {};
}

bool WorkbenchPage::macroResponseMatches(const MacroStep &step, const QByteArray &buffer) const
{
    if (step.expectedResponse.trimmed().isEmpty()) {
        return true;
    }
    if (step.responseMode == QStringLiteral("hex")) {
        bool ok = false;
        const QByteArray expected = expectedMacroResponseBytes(step, &ok);
        return ok && !expected.isEmpty() && buffer.contains(expected);
    }

    const QString decoded = AppTextEncoding::decode(buffer, receiveEncodingKey());
    return decoded.contains(step.expectedResponse);
}

void WorkbenchPage::loadMacroSteps()
{
    m_macroSteps.clear();
    AppSettings settings;
    const QByteArray json = settings.value(QStringLiteral("macro/steps")).toString().toUtf8();
    if (json.isEmpty()) {
        updateMacroTable();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        updateMacroTable();
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("version")).toInt() != MacroStepSchemaVersion ||
        !root.value(QStringLiteral("steps")).isArray()) {
        updateMacroTable();
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("steps")).toArray();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        MacroStep step;
        step.name = object.value(QStringLiteral("name")).toString().trimmed();
        step.mode = normalizedMode(object.value(QStringLiteral("mode")).toString(QStringLiteral("text")));
        step.payload = object.value(QStringLiteral("payload")).toString();
        step.lineEnding =
            normalizedLineEnding(object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none")));
        step.responseMode =
            normalizedMode(object.value(QStringLiteral("responseMode")).toString(QStringLiteral("text")));
        step.expectedResponse = object.value(QStringLiteral("expectedResponse")).toString();
        step.timeoutMs = qBound(1, object.value(QStringLiteral("timeoutMs")).toInt(1000), 600000);
        step.delayMs = qBound(0, object.value(QStringLiteral("delayMs")).toInt(0), 600000);
        if (step.payload.trimmed().isEmpty()) {
            continue;
        }
        m_macroSteps.append(step);
    }
    updateMacroTable();
}

void WorkbenchPage::saveMacroSteps() const
{
    QJsonArray array;
    for (const MacroStep &step : m_macroSteps) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), step.name);
        object.insert(QStringLiteral("mode"), step.mode);
        object.insert(QStringLiteral("payload"), step.payload);
        object.insert(QStringLiteral("lineEnding"), step.lineEnding);
        object.insert(QStringLiteral("responseMode"), step.responseMode);
        object.insert(QStringLiteral("expectedResponse"), step.expectedResponse);
        object.insert(QStringLiteral("timeoutMs"), step.timeoutMs);
        object.insert(QStringLiteral("delayMs"), step.delayMs);
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), MacroStepSchemaName);
    root.insert(QStringLiteral("version"), MacroStepSchemaVersion);
    root.insert(QStringLiteral("steps"), array);

    AppSettings settings;
    settings.setValue(QStringLiteral("macro/steps"),
                      QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

void WorkbenchPage::exportMacroResults()
{
    if (m_macroResults.isEmpty()) {
        showWarning(AppI18n::text("没有可导出的结果"), AppI18n::text("请先运行宏命令"));
        return;
    }

    AppSettings settings;
    const QString initialFolder = settings.value(QStringLiteral("export/folder"), defaultExportFolder()).toString();
    const QString initialName =
        QDir(initialFolder)
            .filePath(QStringLiteral("macro_results_%1.csv")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))));
    QString path = QFileDialog::getSaveFileName(window(), AppI18n::text("导出宏命令结果"), initialName,
                                                QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) {
        return;
    }
    if (QFileInfo(path).suffix().isEmpty()) {
        path.append(QStringLiteral(".csv"));
    }

    QByteArray output("\xEF\xBB\xBF", 3);
    output.append("timestamp,loop,step,name,status,elapsed_ms,tx_hex,rx_hex,message\n");
    for (const MacroRunResult &result : m_macroResults) {
        const QString line = QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9\n")
                                 .arg(csvEscape(result.timestamp.toString(Qt::ISODateWithMs)))
                                 .arg(result.loop)
                                 .arg(result.step)
                                 .arg(csvEscape(result.stepName), csvEscape(result.status))
                                 .arg(result.elapsedMs)
                                 .arg(csvEscape(bytesToHex(result.txBytes)), csvEscape(bytesToHex(result.rxBytes)),
                                      csvEscape(result.message));
        output.append(line.toUtf8());
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(AppI18n::text("导出失败"), file.errorString());
        return;
    }
    if (file.write(output) < 0) {
        showError(AppI18n::text("导出失败"), file.errorString());
        return;
    }

    settings.setValue(QStringLiteral("export/folder"), QFileInfo(path).absolutePath());
    showSuccess(AppI18n::text("导出完成"), path);
}
