#pragma once

#include "app/core/checksum_utils.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

namespace AppProtocol {

enum class LengthMode
{
    PayloadLength,
    FrameLength
};

struct ProtocolTemplate
{
    QString name;
    QByteArray header;
    int lengthOffset = 2;
    int lengthSize = 1;
    LengthMode lengthMode = LengthMode::PayloadLength;
    AppChecksum::ByteOrder lengthByteOrder = AppChecksum::ByteOrder::BigEndian;
    int commandOffset = 3;
    int commandSize = 1;
    int payloadOffset = 4;
    int payloadLength = 0;
    QString checksumAlgorithm = QStringLiteral("none");
    AppChecksum::ByteOrder checksumByteOrder = AppChecksum::ByteOrder::LittleEndian;
};

struct ParseResult
{
    bool ok = false;
    QString errorMessage;
    QString summary;
    int frameLength = 0;
    quint32 lengthValue = 0;
    QByteArray command;
    QByteArray payload;
    QByteArray checksum;
    bool checksumChecked = false;
    bool checksumValid = false;
};

// Where a frame's fields lie, using its length field when the template has
// one. A length field outside the frame reads as 0.
struct FrameLayout
{
    quint32 lengthValue = 0;
    qint64 frameLength = 0;
    qint64 checksumOffset = 0;
    qint64 payloadLength = 0;
};

// Compares every field, so templates that are equal parse frames the same way.
bool operator==(const ProtocolTemplate &left, const ProtocolTemplate &right);

QString lengthModeKey(LengthMode mode);
LengthMode lengthModeFromKey(const QString &key);
QString lengthModeLabel(LengthMode mode);
QString checksumNoneKey();
bool hasChecksum(const ProtocolTemplate &protocolTemplate);
int checksumSize(const ProtocolTemplate &protocolTemplate);

ProtocolTemplate defaultTemplate();
QList<ProtocolTemplate> defaultTemplates();
ProtocolTemplate fromJson(const QJsonObject &object);
QJsonObject toJson(const ProtocolTemplate &protocolTemplate);
QList<ProtocolTemplate> listFromJson(const QJsonArray &array);
QJsonArray listToJson(const QList<ProtocolTemplate> &protocolTemplates);
FrameLayout frameLayout(const QByteArray &frame, const ProtocolTemplate &protocolTemplate);
ParseResult parseFrame(const QByteArray &frame, const ProtocolTemplate &protocolTemplate);
// A frame that the template parses, or empty when its fields cannot form one.
QByteArray exampleFrame(const ProtocolTemplate &protocolTemplate);

} // namespace AppProtocol
