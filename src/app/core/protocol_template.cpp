#include "app/core/protocol_template.h"

#include "app/core/app_i18n.h"
#include "app/core/hex_utils.h"

#include <QtCore/QtGlobal>
#include <QtCore/QJsonValue>

namespace {

constexpr int MaxProtocolFrameBytes = 1024 * 1024;

quint32 readUnsigned(const QByteArray &data, int offset, int size, AppChecksum::ByteOrder byteOrder)
{
    quint32 value = 0;
    if (byteOrder == AppChecksum::ByteOrder::LittleEndian) {
        for (int i = size - 1; i >= 0; --i) {
            value = (value << 8) | static_cast<unsigned char>(data.at(offset + i));
        }
        return value;
    }

    for (int i = 0; i < size; ++i) {
        value = (value << 8) | static_cast<unsigned char>(data.at(offset + i));
    }
    return value;
}

bool validLengthSize(int size)
{
    return size == 0 || size == 1 || size == 2 || size == 4;
}

int boundedOffset(int value)
{
    return qBound(0, value, MaxProtocolFrameBytes);
}

int boundedSize(int value)
{
    return qBound(0, value, MaxProtocolFrameBytes);
}

QString checksumStatusText(const AppProtocol::ParseResult &result)
{
    if (!result.checksumChecked) {
        return AppI18n::text("无校验");
    }
    return result.checksumValid ? AppI18n::text("校验正确") : AppI18n::text("校验错误");
}

} // namespace

namespace AppProtocol {

QString lengthModeKey(LengthMode mode)
{
    return mode == LengthMode::FrameLength ? QStringLiteral("frame") : QStringLiteral("payload");
}

LengthMode lengthModeFromKey(const QString &key)
{
    return key.trimmed().toLower() == QStringLiteral("frame") ? LengthMode::FrameLength : LengthMode::PayloadLength;
}

QString lengthModeLabel(LengthMode mode)
{
    return mode == LengthMode::FrameLength ? AppI18n::text("整帧长度") : AppI18n::text("载荷长度");
}

QString checksumNoneKey()
{
    return QStringLiteral("none");
}

bool hasChecksum(const ProtocolTemplate &protocolTemplate)
{
    return protocolTemplate.checksumAlgorithm.trimmed().toLower() != checksumNoneKey();
}

int checksumSize(const ProtocolTemplate &protocolTemplate)
{
    return hasChecksum(protocolTemplate) ? AppChecksum::widthForAlgorithm(protocolTemplate.checksumAlgorithm) : 0;
}

ProtocolTemplate defaultTemplate()
{
    ProtocolTemplate item;
    item.name = AppI18n::text("示例模板");
    item.header = QByteArray::fromHex("AA55");
    item.lengthOffset = 2;
    item.lengthSize = 1;
    item.lengthMode = LengthMode::PayloadLength;
    item.lengthByteOrder = AppChecksum::ByteOrder::BigEndian;
    item.commandOffset = 3;
    item.commandSize = 1;
    item.payloadOffset = 4;
    item.payloadLength = 0;
    item.checksumAlgorithm = QStringLiteral("crc16-modbus");
    item.checksumByteOrder = AppChecksum::ByteOrder::LittleEndian;
    return item;
}

QList<ProtocolTemplate> defaultTemplates()
{
    return {defaultTemplate()};
}

ProtocolTemplate fromJson(const QJsonObject &object)
{
    ProtocolTemplate item = defaultTemplate();
    item.name = object.value(QStringLiteral("name")).toString(item.name).trimmed();
    item.header = QByteArray::fromHex(object.value(QStringLiteral("header")).toString(bytesToHex(item.header)).toLatin1());
    item.lengthOffset = boundedOffset(object.value(QStringLiteral("lengthOffset")).toInt(item.lengthOffset));
    item.lengthSize = object.value(QStringLiteral("lengthSize")).toInt(item.lengthSize);
    if (!validLengthSize(item.lengthSize)) {
        item.lengthSize = defaultTemplate().lengthSize;
    }
    item.lengthMode = lengthModeFromKey(object.value(QStringLiteral("lengthMode")).toString(lengthModeKey(item.lengthMode)));
    item.lengthByteOrder =
        AppChecksum::byteOrderFromKey(object.value(QStringLiteral("lengthByteOrder"))
                                          .toString(AppChecksum::byteOrderKey(item.lengthByteOrder)));
    item.commandOffset = boundedOffset(object.value(QStringLiteral("commandOffset")).toInt(item.commandOffset));
    item.commandSize = boundedSize(object.value(QStringLiteral("commandSize")).toInt(item.commandSize));
    item.payloadOffset = boundedOffset(object.value(QStringLiteral("payloadOffset")).toInt(item.payloadOffset));
    item.payloadLength = boundedSize(object.value(QStringLiteral("payloadLength")).toInt(item.payloadLength));
    item.checksumAlgorithm = object.value(QStringLiteral("checksumAlgorithm")).toString(item.checksumAlgorithm);
    item.checksumByteOrder =
        AppChecksum::byteOrderFromKey(object.value(QStringLiteral("checksumByteOrder"))
                                          .toString(AppChecksum::byteOrderKey(item.checksumByteOrder)));
    if (item.name.isEmpty()) {
        item.name = defaultTemplate().name;
    }
    return item;
}

QJsonObject toJson(const ProtocolTemplate &protocolTemplate)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), protocolTemplate.name);
    object.insert(QStringLiteral("header"), bytesToHex(protocolTemplate.header));
    object.insert(QStringLiteral("lengthOffset"), protocolTemplate.lengthOffset);
    object.insert(QStringLiteral("lengthSize"), protocolTemplate.lengthSize);
    object.insert(QStringLiteral("lengthMode"), lengthModeKey(protocolTemplate.lengthMode));
    object.insert(QStringLiteral("lengthByteOrder"), AppChecksum::byteOrderKey(protocolTemplate.lengthByteOrder));
    object.insert(QStringLiteral("commandOffset"), protocolTemplate.commandOffset);
    object.insert(QStringLiteral("commandSize"), protocolTemplate.commandSize);
    object.insert(QStringLiteral("payloadOffset"), protocolTemplate.payloadOffset);
    object.insert(QStringLiteral("payloadLength"), protocolTemplate.payloadLength);
    object.insert(QStringLiteral("checksumAlgorithm"), protocolTemplate.checksumAlgorithm);
    object.insert(QStringLiteral("checksumByteOrder"), AppChecksum::byteOrderKey(protocolTemplate.checksumByteOrder));
    return object;
}

QList<ProtocolTemplate> listFromJson(const QJsonArray &array)
{
    QList<ProtocolTemplate> items;
    items.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            items.append(fromJson(value.toObject()));
        }
    }
    return items.isEmpty() ? defaultTemplates() : items;
}

QJsonArray listToJson(const QList<ProtocolTemplate> &protocolTemplates)
{
    QJsonArray array;
    for (const ProtocolTemplate &item : protocolTemplates) {
        array.append(toJson(item));
    }
    return array;
}

ParseResult parseFrame(const QByteArray &frame, const ProtocolTemplate &protocolTemplate)
{
    ParseResult result;
    if (frame.isEmpty()) {
        result.errorMessage = AppI18n::text("帧为空");
        return result;
    }
    if (!protocolTemplate.header.isEmpty() && !frame.startsWith(protocolTemplate.header)) {
        result.errorMessage = AppI18n::text("帧头不匹配");
        return result;
    }
    if (!validLengthSize(protocolTemplate.lengthSize)) {
        result.errorMessage = AppI18n::text("长度字段字节数应为 0、1、2 或 4");
        return result;
    }

    const int checksumBytes = checksumSize(protocolTemplate);
    if (protocolTemplate.lengthSize > 0) {
        if (protocolTemplate.lengthOffset < 0 ||
            protocolTemplate.lengthOffset + protocolTemplate.lengthSize > frame.size()) {
            result.errorMessage = AppI18n::text("长度字段超出帧范围");
            return result;
        }
        result.lengthValue =
            readUnsigned(frame, protocolTemplate.lengthOffset, protocolTemplate.lengthSize,
                         protocolTemplate.lengthByteOrder);
    }

    int frameLength = frame.size();
    if (protocolTemplate.lengthSize > 0 && protocolTemplate.lengthMode == LengthMode::FrameLength) {
        frameLength = static_cast<int>(result.lengthValue);
    } else if (protocolTemplate.lengthSize > 0 && protocolTemplate.payloadLength <= 0) {
        frameLength = protocolTemplate.payloadOffset + static_cast<int>(result.lengthValue) + checksumBytes;
    } else if (protocolTemplate.payloadLength > 0) {
        frameLength = protocolTemplate.payloadOffset + protocolTemplate.payloadLength + checksumBytes;
    }

    if (frameLength <= 0 || frameLength > MaxProtocolFrameBytes) {
        result.errorMessage = AppI18n::text("解析出的帧长度无效");
        return result;
    }
    if (frame.size() < frameLength) {
        result.errorMessage = AppI18n::text("帧长度不足：需要 %1 B，当前 %2 B").arg(frameLength).arg(frame.size());
        return result;
    }

    const QByteArray currentFrame = frame.left(frameLength);
    result.frameLength = frameLength;

    if (protocolTemplate.commandSize > 0) {
        if (protocolTemplate.commandOffset < 0 ||
            protocolTemplate.commandOffset + protocolTemplate.commandSize > currentFrame.size()) {
            result.errorMessage = AppI18n::text("命令字字段超出帧范围");
            return result;
        }
        result.command = currentFrame.mid(protocolTemplate.commandOffset, protocolTemplate.commandSize);
    }

    int payloadLength = protocolTemplate.payloadLength;
    if (payloadLength <= 0) {
        if (protocolTemplate.lengthSize > 0 && protocolTemplate.lengthMode == LengthMode::PayloadLength) {
            payloadLength = static_cast<int>(result.lengthValue);
        } else {
            payloadLength = currentFrame.size() - checksumBytes - protocolTemplate.payloadOffset;
        }
    }
    if (payloadLength < 0 || protocolTemplate.payloadOffset < 0 ||
        protocolTemplate.payloadOffset + payloadLength > currentFrame.size() - checksumBytes) {
        result.errorMessage = AppI18n::text("载荷字段超出帧范围");
        return result;
    }
    result.payload = currentFrame.mid(protocolTemplate.payloadOffset, payloadLength);

    if (checksumBytes > 0) {
        if (checksumBytes >= currentFrame.size()) {
            result.errorMessage = AppI18n::text("校验字段超出帧范围");
            return result;
        }
        const QByteArray checksumPayload = currentFrame.left(currentFrame.size() - checksumBytes);
        result.checksum = currentFrame.right(checksumBytes);
        const AppChecksum::ChecksumResult checksum =
            AppChecksum::calculate(checksumPayload, protocolTemplate.checksumAlgorithm,
                                   protocolTemplate.checksumByteOrder);
        if (!checksum.ok) {
            result.errorMessage = checksum.errorMessage;
            return result;
        }
        result.checksumChecked = true;
        result.checksumValid = checksum.bytes == result.checksum;
    }

    const QString commandText = result.command.isEmpty() ? QStringLiteral("-") : bytesToHex(result.command);
    result.summary = AppI18n::text("%1 · CMD %2 · LEN %3 · PAYLOAD %4 B · %5")
                         .arg(protocolTemplate.name, commandText)
                         .arg(result.frameLength)
                         .arg(result.payload.size())
                         .arg(checksumStatusText(result));
    if (frame.size() > frameLength) {
        result.summary += AppI18n::text(" · 余 %1 B").arg(frame.size() - frameLength);
    }
    result.ok = true;
    return result;
}

} // namespace AppProtocol
