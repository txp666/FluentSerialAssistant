#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QString WorkbenchPage::checksumAlgorithmKey() const
{
    return AppChecksum::normalizedAlgorithmKey(m_checksumAlgorithmCombo
                                                   ? m_checksumAlgorithmCombo->currentData().toString()
                                                   : AppChecksum::defaultAlgorithmKey());
}

AppChecksum::ByteOrder WorkbenchPage::checksumByteOrder() const
{
    return AppChecksum::byteOrderFromKey(m_checksumByteOrderCombo
                                             ? m_checksumByteOrderCombo->currentData().toString()
                                             : AppChecksum::byteOrderKey(AppChecksum::ByteOrder::LittleEndian));
}

QByteArray WorkbenchPage::payloadWithOptionalChecksum(const QByteArray &payload, bool *ok)
{
    if (ok) {
        *ok = false;
    }

    if (!m_checksumAppendCheck || !m_checksumAppendCheck->isChecked()) {
        if (ok) {
            *ok = true;
        }
        return payload;
    }

    const AppChecksum::ChecksumResult result =
        AppChecksum::calculate(payload, checksumAlgorithmKey(), checksumByteOrder());
    if (!result.ok) {
        showWarning(QStringLiteral("校验计算失败"), result.errorMessage);
        return payload;
    }

    QByteArray output = payload;
    output.append(result.bytes);
    if (ok) {
        *ok = true;
    }
    setChecksumResultText(QStringLiteral("%1 0x%2 · %3")
                              .arg(AppChecksum::labelForAlgorithm(checksumAlgorithmKey()),
                                   QStringLiteral("%1")
                                       .arg(result.value, AppChecksum::widthForAlgorithm(checksumAlgorithmKey()) * 2,
                                            16, QLatin1Char('0'))
                                       .toUpper(),
                                   bytesToHex(result.bytes)));
    return output;
}

void WorkbenchPage::calculateChecksumForCurrentPayload()
{
    bool ok = false;
    const QByteArray payload = currentPayload(&ok);
    if (!ok) {
        return;
    }
    if (payload.isEmpty()) {
        showWarning(QStringLiteral("无法计算校验"), QStringLiteral("发送内容为空"));
        return;
    }

    const AppChecksum::ChecksumResult result =
        AppChecksum::calculate(payload, checksumAlgorithmKey(), checksumByteOrder());
    if (!result.ok) {
        showWarning(QStringLiteral("校验计算失败"), result.errorMessage);
        return;
    }

    setChecksumResultText(QStringLiteral("%1 0x%2 · %3")
                              .arg(AppChecksum::labelForAlgorithm(checksumAlgorithmKey()),
                                   QStringLiteral("%1")
                                       .arg(result.value, AppChecksum::widthForAlgorithm(checksumAlgorithmKey()) * 2,
                                            16, QLatin1Char('0'))
                                       .toUpper(),
                                   bytesToHex(result.bytes)));
}

void WorkbenchPage::setChecksumResultText(const QString &text)
{
    if (m_checksumResultLabel) {
        m_checksumResultLabel->setText(text);
    }
}
