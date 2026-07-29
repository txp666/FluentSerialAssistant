#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVector>

namespace AppPlot {

enum class Protocol
{
    Numbers,
    Delimited,
    KeyValue,
    Json,
    Binary
};

enum class BinarySource
{
    Frame,
    Payload
};

enum class BinaryType
{
    UInt8,
    Int8,
    UInt16,
    Int16,
    UInt32,
    Int32,
    UInt64,
    Int64,
    Float32,
    Float64
};

enum class ByteOrder
{
    LittleEndian,
    BigEndian
};

struct BinaryField
{
    QString name;
    int byteOffset = 0;
    BinaryType type = BinaryType::UInt8;
    ByteOrder byteOrder = ByteOrder::LittleEndian;
    double scale = 1.0;
    double add = 0.0;
};

struct ParserConfig
{
    Protocol protocol = Protocol::Numbers;
    QStringList fields;
    BinarySource binarySource = BinarySource::Frame;
    QVector<BinaryField> binaryFields;
};

struct PlotValue
{
    QString name;
    double value = 0.0;
};

using PlotSample = QVector<PlotValue>;

QStringList supportedProtocolKeys();
QString protocolKey(Protocol protocol);
bool protocolFromKey(const QString &key, Protocol *protocol);
QString binarySourceKey(BinarySource source);
bool binarySourceFromKey(const QString &key, BinarySource *source);
QString binaryTypeKey(BinaryType type);
bool binaryTypeFromKey(const QString &key, BinaryType *type);
QString byteOrderKey(ByteOrder byteOrder);
bool byteOrderFromKey(const QString &key, ByteOrder *byteOrder);

bool parserConfigFromJson(const QJsonObject &object, ParserConfig *config, QString *error);
QJsonObject parserConfigToJson(const ParserConfig &config);
QVector<PlotSample> extractSamples(const ParserConfig &config, const QString &text, const QByteArray &frame = {},
                                   const QByteArray &payload = {});

} // namespace AppPlot
