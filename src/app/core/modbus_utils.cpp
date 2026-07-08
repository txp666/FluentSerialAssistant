#include "app/core/modbus_utils.h"

#include "app/core/checksum_utils.h"
#include "app/core/hex_utils.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>

namespace {

using AppModbus::FunctionOption;

const QList<FunctionOption> &modbusFunctionOptions()
{
    static const QList<FunctionOption> options = {
        {QStringLiteral("read-coils"), QStringLiteral("01 读线圈"), 0x01, false, false, true},
        {QStringLiteral("read-discrete-inputs"), QStringLiteral("02 读离散输入"), 0x02, false, false, true},
        {QStringLiteral("read-holding-registers"), QStringLiteral("03 读保持寄存器"), 0x03, false, false, false},
        {QStringLiteral("read-input-registers"), QStringLiteral("04 读输入寄存器"), 0x04, false, false, false},
        {QStringLiteral("write-single-coil"), QStringLiteral("05 写单线圈"), 0x05, true, false, true},
        {QStringLiteral("write-single-register"), QStringLiteral("06 写单寄存器"), 0x06, true, false, false},
        {QStringLiteral("write-multiple-coils"), QStringLiteral("0F 写多线圈"), 0x0F, true, true, true},
        {QStringLiteral("write-multiple-registers"), QStringLiteral("10 写多寄存器"), 0x10, true, true, false},
    };
    return options;
}

void appendU16(QByteArray &frame, quint16 value)
{
    frame.append(static_cast<char>((value >> 8) & 0xFF));
    frame.append(static_cast<char>(value & 0xFF));
}

quint16 readU16(const QByteArray &data, int offset)
{
    return static_cast<quint16>((static_cast<unsigned char>(data.at(offset)) << 8) |
                                static_cast<unsigned char>(data.at(offset + 1)));
}

AppModbus::BuildResult fail(const QString &message)
{
    AppModbus::BuildResult result;
    result.errorMessage = message;
    return result;
}

QStringList valueTokens(const QString &text)
{
    return text.split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
}

bool parseUInt16Token(const QString &token, quint16 *value)
{
    bool ok = false;
    const QString trimmed = token.trimmed();
    int base = 10;
    QString number = trimmed;
    if (number.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        base = 16;
        number = number.mid(2);
    }
    const uint parsed = number.toUInt(&ok, base);
    if (!ok || parsed > 0xFFFFU) {
        return false;
    }
    *value = static_cast<quint16>(parsed);
    return true;
}

bool parseCoilToken(const QString &token, bool *value)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("on") || normalized == QStringLiteral("ff00") ||
        normalized == QStringLiteral("0xff00")) {
        *value = true;
        return true;
    }
    if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("off") || normalized == QStringLiteral("0000") ||
        normalized == QStringLiteral("0x0000")) {
        *value = false;
        return true;
    }
    return false;
}

QByteArray packedCoils(const QList<bool> &values)
{
    QByteArray bytes((values.size() + 7) / 8, Qt::Uninitialized);
    bytes.fill(0);
    for (int i = 0; i < values.size(); ++i) {
        if (values.at(i)) {
            bytes[i / 8] = static_cast<char>(static_cast<unsigned char>(bytes.at(i / 8)) | (1U << (i % 8)));
        }
    }
    return bytes;
}

QList<quint16> parseRegisters(const QString &text, bool *ok)
{
    QList<quint16> values;
    *ok = false;
    const QStringList tokens = valueTokens(text);
    values.reserve(tokens.size());
    for (const QString &token : tokens) {
        quint16 value = 0;
        if (!parseUInt16Token(token, &value)) {
            return {};
        }
        values.append(value);
    }
    *ok = !values.isEmpty();
    return values;
}

QList<bool> parseCoils(const QString &text, bool *ok)
{
    QList<bool> values;
    *ok = false;
    const QStringList tokens = valueTokens(text);
    values.reserve(tokens.size());
    for (const QString &token : tokens) {
        bool value = false;
        if (!parseCoilToken(token, &value)) {
            return {};
        }
        values.append(value);
    }
    *ok = !values.isEmpty();
    return values;
}

bool quantityInRange(const FunctionOption &function, quint16 quantity)
{
    if (quantity == 0) {
        return false;
    }
    if (!function.write || function.multiple) {
        return function.coil ? quantity <= 2000 : quantity <= 125;
    }
    return quantity == 1;
}

QString exceptionText(quint8 code)
{
    switch (code) {
    case 0x01:
        return QStringLiteral("非法功能");
    case 0x02:
        return QStringLiteral("非法数据地址");
    case 0x03:
        return QStringLiteral("非法数据值");
    case 0x04:
        return QStringLiteral("从站设备故障");
    case 0x05:
        return QStringLiteral("确认");
    case 0x06:
        return QStringLiteral("从站设备忙");
    case 0x08:
        return QStringLiteral("存储奇偶性错误");
    case 0x0A:
        return QStringLiteral("网关路径不可用");
    case 0x0B:
        return QStringLiteral("目标设备响应失败");
    default:
        return QStringLiteral("未知异常");
    }
}

QByteArray appendModbusCrc(QByteArray frame)
{
    const AppChecksum::ChecksumResult crc =
        AppChecksum::calculate(frame, QStringLiteral("crc16-modbus"), AppChecksum::ByteOrder::LittleEndian);
    frame.append(crc.bytes);
    return frame;
}

QString crcText(bool valid) { return valid ? QStringLiteral("CRC 正确") : QStringLiteral("CRC 错误"); }

} // namespace

namespace AppModbus {

QList<FunctionOption> functionOptions() { return modbusFunctionOptions(); }

QString defaultFunctionKey() { return QStringLiteral("read-holding-registers"); }

QString normalizedFunctionKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("01") || normalized == QStringLiteral("1")) {
        return QStringLiteral("read-coils");
    }
    if (normalized == QStringLiteral("02") || normalized == QStringLiteral("2")) {
        return QStringLiteral("read-discrete-inputs");
    }
    if (normalized == QStringLiteral("03") || normalized == QStringLiteral("3")) {
        return QStringLiteral("read-holding-registers");
    }
    if (normalized == QStringLiteral("04") || normalized == QStringLiteral("4")) {
        return QStringLiteral("read-input-registers");
    }
    if (normalized == QStringLiteral("05") || normalized == QStringLiteral("5")) {
        return QStringLiteral("write-single-coil");
    }
    if (normalized == QStringLiteral("06") || normalized == QStringLiteral("6")) {
        return QStringLiteral("write-single-register");
    }
    if (normalized == QStringLiteral("0f") || normalized == QStringLiteral("15")) {
        return QStringLiteral("write-multiple-coils");
    }
    if (normalized == QStringLiteral("10") || normalized == QStringLiteral("16")) {
        return QStringLiteral("write-multiple-registers");
    }
    for (const FunctionOption &option : modbusFunctionOptions()) {
        if (option.key == normalized) {
            return option.key;
        }
    }
    return defaultFunctionKey();
}

FunctionOption functionForKey(const QString &key)
{
    const QString normalized = normalizedFunctionKey(key);
    for (const FunctionOption &option : modbusFunctionOptions()) {
        if (option.key == normalized) {
            return option;
        }
    }
    return modbusFunctionOptions().at(2);
}

BuildResult buildRequest(const RequestConfig &config)
{
    const FunctionOption function = functionForKey(config.functionKey);
    const quint16 quantity = function.write && !function.multiple ? 1 : config.quantity;
    if (config.slaveId == 0 || config.slaveId > 247) {
        return fail(QStringLiteral("站号范围应为 1-247"));
    }
    if (!quantityInRange(function, quantity)) {
        return fail(function.coil ? QStringLiteral("线圈数量范围应为 1-2000")
                                  : QStringLiteral("寄存器数量范围应为 1-125"));
    }

    QByteArray frame;
    frame.append(static_cast<char>(config.slaveId));
    frame.append(static_cast<char>(function.code));
    appendU16(frame, config.address);

    if (!function.write) {
        appendU16(frame, quantity);
        frame = appendModbusCrc(frame);
        BuildResult result;
        result.ok = true;
        result.frame = frame;
        result.summary = QStringLiteral("%1 · 站号 %2 · 地址 %3 · 数量 %4")
                             .arg(function.label)
                             .arg(config.slaveId)
                             .arg(config.address)
                             .arg(quantity);
        return result;
    }

    if (!function.multiple && function.coil) {
        bool value = false;
        if (!parseCoilToken(config.valuesText, &value)) {
            return fail(QStringLiteral("写单线圈值应为 0/1、true/false 或 on/off"));
        }
        appendU16(frame, value ? 0xFF00 : 0x0000);
    } else if (!function.multiple) {
        quint16 value = 0;
        if (!parseUInt16Token(config.valuesText, &value)) {
            return fail(QStringLiteral("写单寄存器值应为 0-65535 或 0x0000-0xFFFF"));
        }
        appendU16(frame, value);
    } else if (function.coil) {
        bool ok = false;
        const QList<bool> values = parseCoils(config.valuesText, &ok);
        if (!ok) {
            return fail(QStringLiteral("多线圈值应为 0/1 列表，例如：1 0 1 1"));
        }
        if (values.size() != quantity) {
            return fail(QStringLiteral("线圈值数量需要等于数量字段"));
        }
        const QByteArray coilBytes = packedCoils(values);
        appendU16(frame, quantity);
        frame.append(static_cast<char>(coilBytes.size()));
        frame.append(coilBytes);
    } else {
        bool ok = false;
        const QList<quint16> values = parseRegisters(config.valuesText, &ok);
        if (!ok) {
            return fail(QStringLiteral("多寄存器值应为 0-65535 列表，例如：1 2 0x1234"));
        }
        if (values.size() != quantity) {
            return fail(QStringLiteral("寄存器值数量需要等于数量字段"));
        }
        appendU16(frame, quantity);
        frame.append(static_cast<char>(values.size() * 2));
        for (quint16 value : values) {
            appendU16(frame, value);
        }
    }

    frame = appendModbusCrc(frame);
    BuildResult result;
    result.ok = true;
    result.frame = frame;
    result.summary =
        QStringLiteral("%1 · 站号 %2 · 地址 %3").arg(function.label).arg(config.slaveId).arg(config.address);
    return result;
}

ParseResult parseResponse(const QByteArray &frame)
{
    ParseResult result;
    if (frame.size() < 5) {
        return result;
    }

    const QByteArray payload = frame.left(frame.size() - 2);
    const QByteArray expectedCrc =
        AppChecksum::calculate(payload, QStringLiteral("crc16-modbus"), AppChecksum::ByteOrder::LittleEndian).bytes;
    result.crcValid = expectedCrc == frame.right(2);

    const quint8 slave = static_cast<unsigned char>(frame.at(0));
    const quint8 function = static_cast<unsigned char>(frame.at(1));
    const quint8 baseFunction = function & 0x7F;
    const bool knownFunction = baseFunction == 0x01 || baseFunction == 0x02 || baseFunction == 0x03 ||
                               baseFunction == 0x04 || baseFunction == 0x05 || baseFunction == 0x06 ||
                               baseFunction == 0x0F || baseFunction == 0x10;
    if (!knownFunction && !result.crcValid) {
        return result;
    }

    result.recognized = true;
    if ((function & 0x80) != 0) {
        const quint8 exceptionCode = frame.size() >= 5 ? static_cast<unsigned char>(frame.at(2)) : 0;
        result.summary = QStringLiteral("Modbus 异常 · 站号 %1 · 功能 0x%2 · %3 · %4")
                             .arg(slave)
                             .arg(function, 2, 16, QLatin1Char('0'))
                             .arg(exceptionText(exceptionCode), crcText(result.crcValid))
                             .toUpper();
        return result;
    }

    if (function >= 0x01 && function <= 0x04 && frame.size() >= 5) {
        const quint8 byteCount = static_cast<unsigned char>(frame.at(2));
        const QByteArray data = frame.mid(3, qMin<int>(byteCount, frame.size() - 5));
        QString detail = bytesToHex(data);
        if ((function == 0x03 || function == 0x04) && data.size() % 2 == 0) {
            QStringList registers;
            for (int i = 0; i + 1 < data.size(); i += 2) {
                registers.append(QString::number(readU16(data, i)));
            }
            detail = registers.join(QStringLiteral(", "));
        }
        result.summary = QStringLiteral("Modbus 响应 · 站号 %1 · 功能 0x%2 · 数据 %3 · %4")
                             .arg(slave)
                             .arg(function, 2, 16, QLatin1Char('0'))
                             .arg(detail.isEmpty() ? QStringLiteral("-") : detail, crcText(result.crcValid))
                             .toUpper();
        return result;
    }

    if ((function == 0x05 || function == 0x06 || function == 0x0F || function == 0x10) && frame.size() >= 8) {
        const quint16 address = readU16(frame, 2);
        const quint16 valueOrQuantity = readU16(frame, 4);
        result.summary = QStringLiteral("Modbus 响应 · 站号 %1 · 功能 0x%2 · 地址 %3 · 值/数量 %4 · %5")
                             .arg(slave)
                             .arg(function, 2, 16, QLatin1Char('0'))
                             .arg(address)
                             .arg(valueOrQuantity)
                             .arg(crcText(result.crcValid))
                             .toUpper();
        return result;
    }

    result.summary = QStringLiteral("Modbus 响应 · 站号 %1 · 功能 0x%2 · %3")
                         .arg(slave)
                         .arg(function, 2, 16, QLatin1Char('0'))
                         .arg(crcText(result.crcValid))
                         .toUpper();
    return result;
}

} // namespace AppModbus
