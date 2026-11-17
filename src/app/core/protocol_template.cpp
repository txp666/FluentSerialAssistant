#include "app/core/protocol_template.h"

#include "app/core/app_i18n.h"
#include "app/core/hex_utils.h"

#include <QtCore/QJsonValue>
#include <QtCore/QSet>
#include <QtCore/QtGlobal>

#include <cmath>
#include <cstring>

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

bool validLengthSize(int size) { return size == 0 || size == 1 || size == 2 || size == 4; }

int boundedOffset(int value) { return qBound(0, value, MaxProtocolFrameBytes); }

int boundedSize(int value) { return qBound(0, value, MaxProtocolFrameBytes); }

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

QString checksumNoneKey() { return QStringLiteral("none"); }

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

QList<ProtocolTemplate> defaultTemplates() { return {defaultTemplate()}; }

bool operator==(const ProtocolTemplate &left, const ProtocolTemplate &right)
{
    return left.name == right.name && left.header == right.header && left.lengthOffset == right.lengthOffset &&
           left.lengthSize == right.lengthSize && left.lengthMode == right.lengthMode &&
           left.lengthByteOrder == right.lengthByteOrder && left.commandOffset == right.commandOffset &&
           left.commandSize == right.commandSize && left.payloadOffset == right.payloadOffset &&
           left.payloadLength == right.payloadLength && left.checksumAlgorithm == right.checksumAlgorithm &&
           left.checksumByteOrder == right.checksumByteOrder && left.checksumStart == right.checksumStart &&
           left.trailer == right.trailer && left.payloadFields == right.payloadFields &&
           left.addressOffset == right.addressOffset && left.addressSize == right.addressSize;
}

ProtocolTemplate fromJson(const QJsonObject &object)
{
    ProtocolTemplate item = defaultTemplate();
    item.name = object.value(QStringLiteral("name")).toString(item.name).trimmed();
    item.header =
        QByteArray::fromHex(object.value(QStringLiteral("header")).toString(bytesToHex(item.header)).toLatin1());
    item.lengthOffset = boundedOffset(object.value(QStringLiteral("lengthOffset")).toInt(item.lengthOffset));
    item.lengthSize = object.value(QStringLiteral("lengthSize")).toInt(item.lengthSize);
    if (!validLengthSize(item.lengthSize)) {
        item.lengthSize = defaultTemplate().lengthSize;
    }
    item.lengthMode =
        lengthModeFromKey(object.value(QStringLiteral("lengthMode")).toString(lengthModeKey(item.lengthMode)));
    item.lengthByteOrder = AppChecksum::byteOrderFromKey(
        object.value(QStringLiteral("lengthByteOrder")).toString(AppChecksum::byteOrderKey(item.lengthByteOrder)));
    item.commandOffset = boundedOffset(object.value(QStringLiteral("commandOffset")).toInt(item.commandOffset));
    item.commandSize = boundedSize(object.value(QStringLiteral("commandSize")).toInt(item.commandSize));
    item.payloadOffset = boundedOffset(object.value(QStringLiteral("payloadOffset")).toInt(item.payloadOffset));
    item.payloadLength = boundedSize(object.value(QStringLiteral("payloadLength")).toInt(item.payloadLength));
    item.checksumAlgorithm = object.value(QStringLiteral("checksumAlgorithm")).toString(item.checksumAlgorithm);
    item.checksumByteOrder = AppChecksum::byteOrderFromKey(
        object.value(QStringLiteral("checksumByteOrder")).toString(AppChecksum::byteOrderKey(item.checksumByteOrder)));
    item.checksumStart = boundedOffset(object.value(QStringLiteral("checksumStart")).toInt(item.checksumStart));
    item.trailer =
        QByteArray::fromHex(object.value(QStringLiteral("trailer")).toString(bytesToHex(item.trailer)).toLatin1());
    item.addressOffset = boundedOffset(object.value(QStringLiteral("addressOffset")).toInt());
    item.addressSize = object.value(QStringLiteral("addressSize")).toInt();
    if (!validLengthSize(item.addressSize)) {
        item.addressSize = 0;
    }
    AppPlot::ParserConfig fields;
    QString fieldsError;
    if (AppPlot::parserConfigFromJson({{QStringLiteral("protocol"), QStringLiteral("binary")},
                                       {QStringLiteral("binaryFields"), object.value(QStringLiteral("payloadFields"))}},
                                      &fields, &fieldsError)) {
        item.payloadFields = fields.binaryFields.mid(0, AppPlot::MaximumChannels);
    }
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
    object.insert(QStringLiteral("checksumStart"), protocolTemplate.checksumStart);
    object.insert(QStringLiteral("trailer"), bytesToHex(protocolTemplate.trailer));
    object.insert(
        QStringLiteral("payloadFields"),
        AppPlot::parserConfigToJson(payloadParserConfig(protocolTemplate)).value(QStringLiteral("binaryFields")));
    object.insert(QStringLiteral("addressOffset"), protocolTemplate.addressOffset);
    object.insert(QStringLiteral("addressSize"), protocolTemplate.addressSize);
    return object;
}

AppPlot::ParserConfig payloadParserConfig(const ProtocolTemplate &protocolTemplate)
{
    AppPlot::ParserConfig config;
    config.protocol = AppPlot::Protocol::Binary;
    config.binarySource = AppPlot::BinarySource::Payload;
    config.binaryFields = protocolTemplate.payloadFields;
    return config;
}

qint64 requiredPayloadBytes(const ProtocolTemplate &protocolTemplate)
{
    qint64 required = 0;
    for (const auto &field : protocolTemplate.payloadFields) {
        required = qMax(required, qint64(field.byteOffset) + AppPlot::binaryTypeSize(field.type));
    }
    return required;
}

QString payloadConfigurationError(const ProtocolTemplate &protocolTemplate)
{
    if (protocolTemplate.payloadFields.size() > AppPlot::MaximumChannels) {
        return AppI18n::text("最多定义 %1 个载荷字段").arg(AppPlot::MaximumChannels);
    }
    if (!validLengthSize(protocolTemplate.addressSize) || protocolTemplate.addressOffset < 0 ||
        protocolTemplate.addressOffset > MaxProtocolFrameBytes - protocolTemplate.addressSize) {
        return AppI18n::text("设备地址字段范围无效");
    }
    QSet<QString> names;
    for (const auto &field : protocolTemplate.payloadFields) {
        const QString name = field.name.simplified();
        if (name.isEmpty() || names.contains(name)) {
            return AppI18n::text("载荷字段名称不能为空或重复");
        }
        names.insert(name);
        const int size = AppPlot::binaryTypeSize(field.type);
        const int limit = protocolTemplate.payloadLength > 0 ? protocolTemplate.payloadLength : MaxProtocolFrameBytes;
        if (size <= 0 || field.byteOffset < 0 || field.byteOffset > limit - size || !std::isfinite(field.scale) ||
            !std::isfinite(field.add)) {
            return AppI18n::text("载荷字段 %1 的范围或换算参数无效").arg(name);
        }
    }
    return {};
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

FrameLayout frameLayout(const QByteArray &frame, const ProtocolTemplate &protocolTemplate)
{
    FrameLayout layout;
    const int checksumBytes = checksumSize(protocolTemplate);
    const int trailerBytes = int(protocolTemplate.trailer.size());
    if (protocolTemplate.lengthSize > 0 && protocolTemplate.lengthOffset >= 0 &&
        protocolTemplate.lengthOffset + protocolTemplate.lengthSize <= frame.size()) {
        layout.lengthValue = readUnsigned(frame, protocolTemplate.lengthOffset, protocolTemplate.lengthSize,
                                          protocolTemplate.lengthByteOrder);
    }

    layout.frameLength = frame.size();
    if (protocolTemplate.lengthSize > 0 && protocolTemplate.lengthMode == LengthMode::FrameLength) {
        layout.frameLength = layout.lengthValue;
    } else if (protocolTemplate.payloadLength > 0) {
        layout.frameLength =
            qint64(protocolTemplate.payloadOffset) + protocolTemplate.payloadLength + checksumBytes + trailerBytes;
    } else if (protocolTemplate.lengthSize > 0) {
        layout.frameLength = qint64(protocolTemplate.payloadOffset) + layout.lengthValue + checksumBytes + trailerBytes;
    }
    layout.trailerOffset = layout.frameLength - trailerBytes;
    layout.checksumOffset = layout.trailerOffset - checksumBytes;
    layout.payloadLength = protocolTemplate.payloadLength > 0 ? protocolTemplate.payloadLength
                                                              : layout.checksumOffset - protocolTemplate.payloadOffset;
    return layout;
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
    if (hasChecksum(protocolTemplate) && !AppChecksum::isSupportedAlgorithm(protocolTemplate.checksumAlgorithm)) {
        result.errorMessage = AppI18n::text("不支持的校验算法");
        return result;
    }

    const int checksumBytes = checksumSize(protocolTemplate);
    if (protocolTemplate.lengthSize > 0 &&
        (protocolTemplate.lengthOffset < 0 ||
         protocolTemplate.lengthOffset + protocolTemplate.lengthSize > frame.size())) {
        result.errorMessage = AppI18n::text("长度字段超出帧范围");
        return result;
    }
    const FrameLayout layout = frameLayout(frame, protocolTemplate);
    result.lengthValue = layout.lengthValue;

    if (layout.frameLength <= 0 || layout.frameLength > MaxProtocolFrameBytes) {
        result.errorMessage = AppI18n::text("解析出的帧长度无效");
        return result;
    }
    const int frameLength = int(layout.frameLength);
    if (frame.size() < frameLength) {
        result.errorMessage = AppI18n::text("帧长度不足：需要 %1 B，当前 %2 B").arg(frameLength).arg(frame.size());
        return result;
    }

    const QByteArray currentFrame = frame.left(frameLength);
    result.frameLength = frameLength;
    if (!protocolTemplate.trailer.isEmpty() && !currentFrame.endsWith(protocolTemplate.trailer)) {
        result.errorMessage = AppI18n::text("帧尾不匹配");
        return result;
    }

    if (protocolTemplate.commandSize > 0) {
        if (protocolTemplate.commandOffset < 0 ||
            protocolTemplate.commandOffset + protocolTemplate.commandSize > currentFrame.size()) {
            result.errorMessage = AppI18n::text("命令字字段超出帧范围");
            return result;
        }
        result.command = currentFrame.mid(protocolTemplate.commandOffset, protocolTemplate.commandSize);
    }

    if (layout.payloadLength < 0 || protocolTemplate.payloadOffset < 0 ||
        protocolTemplate.payloadOffset + layout.payloadLength > layout.checksumOffset) {
        result.errorMessage = AppI18n::text("载荷字段超出帧范围");
        return result;
    }
    result.payload = currentFrame.mid(protocolTemplate.payloadOffset, int(layout.payloadLength));

    if (checksumBytes > 0) {
        if (layout.checksumOffset <= 0) {
            result.errorMessage = AppI18n::text("校验字段超出帧范围");
            return result;
        }
        const int checksumOffset = int(layout.checksumOffset);
        if (protocolTemplate.checksumStart >= checksumOffset) {
            result.errorMessage = AppI18n::text("校验起始偏移应小于校验位置 %1").arg(checksumOffset);
            return result;
        }
        result.checksum = currentFrame.mid(checksumOffset, checksumBytes);
        const AppChecksum::ChecksumResult checksum = AppChecksum::calculate(
            currentFrame.mid(protocolTemplate.checksumStart, checksumOffset - protocolTemplate.checksumStart),
            protocolTemplate.checksumAlgorithm, protocolTemplate.checksumByteOrder);
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

QList<DecodedFrame> takeFrames(QByteArray &buffer, const QByteArray &bytes, const ProtocolTemplate &item, bool stream)
{
    QList<DecodedFrame> frames;
    const bool delimited = item.lengthSize > 0 || item.payloadLength > 0;
    if (!stream || !delimited)
        buffer.clear();
    buffer.append(bytes);
    if (buffer.size() > MaxProtocolFrameBytes * 2)
        buffer = buffer.right(MaxProtocolFrameBytes);
    while (!buffer.isEmpty()) {
        if (!item.header.isEmpty()) {
            const auto header = buffer.indexOf(item.header);
            if (header < 0) {
                buffer = buffer.right(qMax(0, int(item.header.size()) - 1));
                break;
            }
            buffer.remove(0, header);
        }
        const qint64 prefix = qMax<qint64>(item.header.size(), qint64(item.lengthOffset) + item.lengthSize);
        if (item.lengthSize > 0 && buffer.size() < prefix)
            break;
        const auto layout = frameLayout(buffer, item);
        if (layout.frameLength <= 0 || layout.frameLength > MaxProtocolFrameBytes) {
            buffer.remove(0, 1);
            continue;
        }
        if (buffer.size() < layout.frameLength) {
            if (stream)
                break;
            // This record has ended, so this candidate cannot finish within it.
            // A raw stream has no such boundary: keep waiting for its payload.
            buffer.remove(0, 1);
            continue;
        }
        const auto parsed = parseFrame(buffer, item);
        if (!parsed.ok || (parsed.checksumChecked && !parsed.checksumValid)) {
            buffer.remove(0, 1);
            continue;
        }
        frames.append({buffer.left(parsed.frameLength), parsed});
        buffer.remove(0, parsed.frameLength);
    }
    if (!stream || !delimited)
        buffer.clear();
    return frames;
}

QByteArray exampleFrame(const ProtocolTemplate &protocolTemplate)
{
    const qint64 requestedPayloadSize = protocolTemplate.payloadLength > 0
                                            ? protocolTemplate.payloadLength
                                            : qMax<qint64>(4, requiredPayloadBytes(protocolTemplate));
    if (requestedPayloadSize > 4096 || !payloadConfigurationError(protocolTemplate).isEmpty()) {
        return {};
    }
    const int payloadSize = int(requestedPayloadSize);
    const qint64 checksumOffset = static_cast<qint64>(protocolTemplate.payloadOffset) + payloadSize;
    const int checksumBytes = checksumSize(protocolTemplate);
    if (protocolTemplate.payloadOffset < 0 || checksumOffset > 4096 || checksumOffset <= 0 ||
        protocolTemplate.header.size() > checksumOffset || protocolTemplate.lengthOffset < 0 ||
        protocolTemplate.lengthOffset + protocolTemplate.lengthSize > checksumOffset ||
        protocolTemplate.commandOffset < 0 ||
        protocolTemplate.commandOffset + protocolTemplate.commandSize > checksumOffset ||
        (checksumBytes > 0 && protocolTemplate.checksumStart >= checksumOffset)) {
        return {};
    }
    QByteArray frame(static_cast<int>(checksumOffset), '\0');
    if (protocolTemplate.addressSize > 0 && protocolTemplate.addressOffset >= 0 &&
        protocolTemplate.addressOffset + protocolTemplate.addressSize <= checksumOffset) {
        frame[protocolTemplate.addressOffset] = '\x01';
    }
    frame.replace(0, protocolTemplate.header.size(), protocolTemplate.header);
    if (protocolTemplate.commandSize > 0) {
        frame[protocolTemplate.commandOffset] = '\x01';
    }
    const QByteArray payload = QByteArray::fromHex("6400C800");
    for (int index = 0; index < payloadSize; ++index) {
        frame[protocolTemplate.payloadOffset + index] = payload.at(index % payload.size());
    }
    for (int index = 0; index < protocolTemplate.payloadFields.size(); ++index) {
        const auto &field = protocolTemplate.payloadFields.at(index);
        const int size = AppPlot::binaryTypeSize(field.type);
        quint64 raw = field.type == AppPlot::BinaryType::UInt8 ? 1 : 100 + 100 * index;
        if (field.type == AppPlot::BinaryType::Int16) {
            raw = quint16(1234 + 100 * index);
        } else if (field.type == AppPlot::BinaryType::Int8 || field.type == AppPlot::BinaryType::Int32 ||
                   field.type == AppPlot::BinaryType::Int64) {
            raw = quint64(qint64(-100));
        } else if (field.type == AppPlot::BinaryType::Float32) {
            const float value = 12.5f + index;
            quint32 bits = 0;
            std::memcpy(&bits, &value, sizeof(value));
            raw = bits;
        } else if (field.type == AppPlot::BinaryType::Float64) {
            const double value = 12.5 + index;
            std::memcpy(&raw, &value, sizeof(value));
        }
        for (int byte = 0; byte < size; ++byte) {
            const int target = field.byteOrder == AppPlot::ByteOrder::LittleEndian ? byte : size - byte - 1;
            frame[protocolTemplate.payloadOffset + field.byteOffset + target] = char((raw >> (byte * 8)) & 0xff);
        }
    }
    quint32 length = protocolTemplate.lengthMode == LengthMode::FrameLength
                         ? frame.size() + checksumBytes + protocolTemplate.trailer.size()
                         : payloadSize;
    if (protocolTemplate.lengthSize > 0 && protocolTemplate.lengthSize < 4 &&
        length >= (quint32(1) << (protocolTemplate.lengthSize * 8)))
        return {};
    for (int index = 0; index < protocolTemplate.lengthSize; ++index) {
        const int byteIndex = protocolTemplate.lengthByteOrder == AppChecksum::ByteOrder::LittleEndian
                                  ? index
                                  : protocolTemplate.lengthSize - index - 1;
        frame[protocolTemplate.lengthOffset + byteIndex] = static_cast<char>(length & 0xff);
        length >>= 8;
    }
    if (checksumBytes > 0) {
        const AppChecksum::ChecksumResult checksum =
            AppChecksum::calculate(frame.mid(protocolTemplate.checksumStart), protocolTemplate.checksumAlgorithm,
                                   protocolTemplate.checksumByteOrder);
        if (!checksum.ok) {
            return {};
        }
        frame.append(checksum.bytes);
    }
    frame.append(protocolTemplate.trailer);
    return frame;
}

} // namespace AppProtocol
