#include "app/core/checksum_utils.h"

#include <QtCore/QtGlobal>

namespace {

const QList<AppChecksum::AlgorithmOption> &algorithmOptions()
{
    static const QList<AppChecksum::AlgorithmOption> options = {
        {QStringLiteral("crc16-modbus"), QStringLiteral("CRC16-Modbus"), 2},
        {QStringLiteral("crc16-ccitt"), QStringLiteral("CRC16-CCITT"), 2},
        {QStringLiteral("crc32"), QStringLiteral("CRC32"), 4},
        {QStringLiteral("lrc"), QStringLiteral("LRC"), 1},
        {QStringLiteral("xor"), QStringLiteral("XOR"), 1},
        {QStringLiteral("sum8"), QStringLiteral("SUM8"), 1},
    };
    return options;
}

const AppChecksum::AlgorithmOption &optionForKey(const QString &key)
{
    const QString normalized = AppChecksum::normalizedAlgorithmKey(key);
    for (const AppChecksum::AlgorithmOption &option : algorithmOptions()) {
        if (option.key == normalized) {
            return option;
        }
    }
    return algorithmOptions().first();
}

quint16 crc16Modbus(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if ((crc & 0x0001) != 0) {
                crc = static_cast<quint16>((crc >> 1) ^ 0xA001);
            } else {
                crc = static_cast<quint16>(crc >> 1);
            }
        }
    }
    return crc;
}

quint16 crc16CcittFalse(const QByteArray &data)
{
    quint16 crc = 0xFFFF;
    for (unsigned char byte : data) {
        crc ^= static_cast<quint16>(byte << 8);
        for (int i = 0; i < 8; ++i) {
            if ((crc & 0x8000) != 0) {
                crc = static_cast<quint16>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<quint16>(crc << 1);
            }
        }
    }
    return crc;
}

quint32 crc32Ieee(const QByteArray &data)
{
    quint32 crc = 0xFFFFFFFFU;
    for (unsigned char byte : data) {
        crc ^= byte;
        for (int i = 0; i < 8; ++i) {
            if ((crc & 0x00000001U) != 0) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

quint8 lrc(const QByteArray &data)
{
    quint8 sum = 0;
    for (unsigned char byte : data) {
        sum = static_cast<quint8>(sum + byte);
    }
    return static_cast<quint8>(0U - sum);
}

quint8 xor8(const QByteArray &data)
{
    quint8 result = 0;
    for (unsigned char byte : data) {
        result ^= byte;
    }
    return result;
}

quint8 sum8(const QByteArray &data)
{
    quint8 result = 0;
    for (unsigned char byte : data) {
        result = static_cast<quint8>(result + byte);
    }
    return result;
}

QByteArray valueBytes(quint32 value, int widthBytes, AppChecksum::ByteOrder byteOrder)
{
    QByteArray bytes;
    bytes.reserve(widthBytes);
    if (byteOrder == AppChecksum::ByteOrder::LittleEndian) {
        for (int i = 0; i < widthBytes; ++i) {
            bytes.append(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
        return bytes;
    }

    for (int i = widthBytes - 1; i >= 0; --i) {
        bytes.append(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
    return bytes;
}

} // namespace

namespace AppChecksum {

QList<AlgorithmOption> options() { return algorithmOptions(); }

QString defaultAlgorithmKey() { return QStringLiteral("crc16-modbus"); }

QString normalizedAlgorithmKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("modbus") || normalized == QStringLiteral("crc-modbus")) {
        return QStringLiteral("crc16-modbus");
    }
    if (normalized == QStringLiteral("ccitt") || normalized == QStringLiteral("crc16")) {
        return QStringLiteral("crc16-ccitt");
    }
    if (normalized == QStringLiteral("sum")) {
        return QStringLiteral("sum8");
    }

    for (const AlgorithmOption &option : algorithmOptions()) {
        if (option.key == normalized) {
            return option.key;
        }
    }
    return defaultAlgorithmKey();
}

QString labelForAlgorithm(const QString &key) { return optionForKey(key).label; }

int widthForAlgorithm(const QString &key) { return optionForKey(key).widthBytes; }

ByteOrder byteOrderFromKey(const QString &key)
{
    return key.trimmed().toLower() == QStringLiteral("big") ? ByteOrder::BigEndian : ByteOrder::LittleEndian;
}

QString byteOrderKey(ByteOrder byteOrder)
{
    return byteOrder == ByteOrder::BigEndian ? QStringLiteral("big") : QStringLiteral("little");
}

QString byteOrderLabel(ByteOrder byteOrder)
{
    return byteOrder == ByteOrder::BigEndian ? QStringLiteral("高字节在前") : QStringLiteral("低字节在前");
}

ChecksumResult calculate(const QByteArray &data, const QString &algorithmKey, ByteOrder byteOrder)
{
    ChecksumResult result;
    const QString key = normalizedAlgorithmKey(algorithmKey);
    const int widthBytes = widthForAlgorithm(key);

    if (key == QStringLiteral("crc16-modbus")) {
        result.value = crc16Modbus(data);
    } else if (key == QStringLiteral("crc16-ccitt")) {
        result.value = crc16CcittFalse(data);
    } else if (key == QStringLiteral("crc32")) {
        result.value = crc32Ieee(data);
    } else if (key == QStringLiteral("lrc")) {
        result.value = lrc(data);
    } else if (key == QStringLiteral("xor")) {
        result.value = xor8(data);
    } else if (key == QStringLiteral("sum8")) {
        result.value = sum8(data);
    } else {
        result.errorMessage = QStringLiteral("不支持的校验算法");
        return result;
    }

    result.bytes = valueBytes(result.value, widthBytes, byteOrder);
    result.ok = true;
    return result;
}

} // namespace AppChecksum
