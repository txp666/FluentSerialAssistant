#include "app/core/plot_value_parser.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QRegularExpression>

#include <cmath>
#include <cstring>
#include <limits>

namespace {

const QRegularExpression &numberPattern()
{
    static const QRegularExpression pattern(QStringLiteral(R"([+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?)"));
    return pattern;
}

const QRegularExpression &wholeNumberPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(R"(^[+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?$)"));
    return pattern;
}

const QRegularExpression &keyValuePattern()
{
    static const QRegularExpression pattern(
        QStringLiteral(
            R"((?:^|[^\p{L}\p{N}_\.\-/])([\p{L}_][\p{L}\p{N}_\.\-/]*(?:[ \t]+[\p{L}_][\p{L}\p{N}_\.\-/]*)*)[ \t]*[:=][ \t]*([+-]?(?:(?:\d+(?:\.\d*)?)|(?:\.\d+))(?:[eE][+-]?\d+)?))"),
        QRegularExpression::UseUnicodePropertiesOption);
    return pattern;
}

QString normalizedFieldName(const QString &name) { return name.simplified().toCaseFolded(); }

QStringList normalizedFilters(const QStringList &fields)
{
    QStringList result;
    for (const QString &field : fields) {
        const QString normalized = normalizedFieldName(field);
        if (!normalized.isEmpty() && !result.contains(normalized)) {
            result.append(normalized);
        }
    }
    return result;
}

bool selectedField(const QStringList &filters, const QString &name)
{
    return filters.isEmpty() || filters.contains(normalizedFieldName(name));
}

AppPlot::PlotSample applyFieldFilter(AppPlot::PlotSample values, const QStringList &filters)
{
    AppPlot::PlotSample result;
    result.reserve(values.size());
    for (int index = 0; index < values.size(); ++index) {
        AppPlot::PlotValue value = values.at(index);
        if (value.name.trimmed().isEmpty()) {
            value.name = QStringLiteral("CH%1").arg(index + 1);
        } else {
            value.name = value.name.simplified();
        }
        if (selectedField(filters, value.name)) {
            result.append(value);
        }
    }
    return result;
}

QStringList textLines(const QString &text)
{
    QStringList lines = text.split(QRegularExpression(QStringLiteral(R"(\r\n|[\r\n])")), Qt::SkipEmptyParts);
    if (lines.isEmpty() && !text.trimmed().isEmpty()) {
        lines.append(text);
    }
    return lines;
}

AppPlot::PlotSample extractNumbers(const QString &text)
{
    AppPlot::PlotSample values;
    auto it = numberPattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const double value = match.captured(0).toDouble(&ok);
        if (ok && std::isfinite(value)) {
            values.append({QString(), value});
        }
    }
    return values;
}

AppPlot::PlotSample extractDelimited(const QString &text)
{
    AppPlot::PlotSample values;
    const QStringList tokens = text.split(QRegularExpression(QStringLiteral(R"([,;\s]+)")), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        const QString trimmed = token.trimmed();
        if (!wholeNumberPattern().match(trimmed).hasMatch()) {
            continue;
        }
        bool ok = false;
        const double value = trimmed.toDouble(&ok);
        if (ok && std::isfinite(value)) {
            values.append({QString(), value});
        }
    }
    return values;
}

AppPlot::PlotSample extractKeyValues(const QString &text)
{
    AppPlot::PlotSample values;
    auto it = keyValuePattern().globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const double value = match.captured(2).toDouble(&ok);
        if (ok && std::isfinite(value)) {
            values.append({match.captured(1).simplified(), value});
        }
    }
    return values;
}

void appendJsonValues(const QJsonValue &value, const QString &path, AppPlot::PlotSample *values)
{
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number)) {
            values->append({path, number});
        }
        return;
    }
    if (value.isObject()) {
        const QJsonObject object = value.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
            const QString childPath = path.isEmpty() ? it.key() : QStringLiteral("%1.%2").arg(path, it.key());
            appendJsonValues(it.value(), childPath, values);
        }
        return;
    }
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (int index = 0; index < array.size(); ++index) {
            const QString childPath = path.isEmpty() ? QString() : QStringLiteral("%1[%2]").arg(path).arg(index);
            appendJsonValues(array.at(index), childPath, values);
        }
    }
}

bool extractJsonDocument(const QString &text, AppPlot::PlotSample *values)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.trimmed().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        return false;
    }
    if (document.isObject()) {
        appendJsonValues(document.object(), QString(), values);
    } else if (document.isArray()) {
        appendJsonValues(document.array(), QString(), values);
    }
    return true;
}

int binaryTypeSize(AppPlot::BinaryType type)
{
    switch (type) {
    case AppPlot::BinaryType::UInt8:
    case AppPlot::BinaryType::Int8:
        return 1;
    case AppPlot::BinaryType::UInt16:
    case AppPlot::BinaryType::Int16:
        return 2;
    case AppPlot::BinaryType::UInt32:
    case AppPlot::BinaryType::Int32:
    case AppPlot::BinaryType::Float32:
        return 4;
    case AppPlot::BinaryType::UInt64:
    case AppPlot::BinaryType::Int64:
    case AppPlot::BinaryType::Float64:
        return 8;
    }
    return 0;
}

quint64 readUnsigned(const QByteArray &data, int offset, int size, AppPlot::ByteOrder byteOrder)
{
    quint64 result = 0;
    for (int index = 0; index < size; ++index) {
        const int sourceIndex =
            byteOrder == AppPlot::ByteOrder::LittleEndian ? offset + index : offset + size - index - 1;
        result |= static_cast<quint64>(static_cast<quint8>(data.at(sourceIndex))) << (index * 8);
    }
    return result;
}

qint64 signedValue(quint64 value, int bits)
{
    if (bits >= 64) {
        return static_cast<qint64>(value);
    }
    const quint64 signBit = quint64(1) << (bits - 1);
    if ((value & signBit) != 0) {
        value |= (~quint64(0)) << bits;
    }
    return static_cast<qint64>(value);
}

bool decodeBinaryValue(const QByteArray &data, const AppPlot::BinaryField &field, double *value)
{
    const int size = binaryTypeSize(field.type);
    if (field.byteOffset < 0 || size <= 0 || field.byteOffset > data.size() - size) {
        return false;
    }

    const quint64 raw = readUnsigned(data, field.byteOffset, size, field.byteOrder);
    double decoded = 0.0;
    switch (field.type) {
    case AppPlot::BinaryType::UInt8:
    case AppPlot::BinaryType::UInt16:
    case AppPlot::BinaryType::UInt32:
    case AppPlot::BinaryType::UInt64:
        decoded = static_cast<double>(raw);
        break;
    case AppPlot::BinaryType::Int8:
    case AppPlot::BinaryType::Int16:
    case AppPlot::BinaryType::Int32:
    case AppPlot::BinaryType::Int64:
        decoded = static_cast<double>(signedValue(raw, size * 8));
        break;
    case AppPlot::BinaryType::Float32: {
        const quint32 bits = static_cast<quint32>(raw);
        float number = 0.0F;
        std::memcpy(&number, &bits, sizeof(number));
        decoded = static_cast<double>(number);
        break;
    }
    case AppPlot::BinaryType::Float64: {
        double number = 0.0;
        std::memcpy(&number, &raw, sizeof(number));
        decoded = number;
        break;
    }
    }
    decoded = decoded * field.scale + field.add;
    if (!std::isfinite(decoded)) {
        return false;
    }
    *value = decoded;
    return true;
}

AppPlot::PlotSample extractBinary(const AppPlot::ParserConfig &config, const QByteArray &data)
{
    AppPlot::PlotSample values;
    if (config.binaryFields.isEmpty()) {
        values.reserve(data.size());
        for (int index = 0; index < data.size(); ++index) {
            values.append(
                {QStringLiteral("CH%1").arg(index + 1), static_cast<double>(static_cast<quint8>(data.at(index)))});
        }
        return values;
    }

    for (const AppPlot::BinaryField &field : config.binaryFields) {
        double value = 0.0;
        if (decodeBinaryValue(data, field, &value)) {
            values.append({field.name, value});
        }
    }
    return values;
}

bool jsonStringList(const QJsonValue &value, QStringList *result, QString *error)
{
    result->clear();
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    if (!value.isArray()) {
        *error = QStringLiteral("fields must be an array of strings");
        return false;
    }
    for (const QJsonValue &entry : value.toArray()) {
        if (!entry.isString() || entry.toString().trimmed().isEmpty()) {
            *error = QStringLiteral("fields must contain non-empty strings");
            return false;
        }
        const QString field = entry.toString().simplified();
        if (!result->contains(field, Qt::CaseInsensitive)) {
            result->append(field);
        }
    }
    return true;
}

} // namespace

namespace AppPlot {

QStringList supportedProtocolKeys()
{
    return {QStringLiteral("numbers"), QStringLiteral("delimited"), QStringLiteral("keyValue"), QStringLiteral("json"),
            QStringLiteral("binary")};
}

QString protocolKey(Protocol protocol)
{
    switch (protocol) {
    case Protocol::Delimited:
        return QStringLiteral("delimited");
    case Protocol::KeyValue:
        return QStringLiteral("keyValue");
    case Protocol::Json:
        return QStringLiteral("json");
    case Protocol::Binary:
        return QStringLiteral("binary");
    case Protocol::Numbers:
    default:
        return QStringLiteral("numbers");
    }
}

bool protocolFromKey(const QString &key, Protocol *protocol)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("numbers")) {
        *protocol = Protocol::Numbers;
    } else if (normalized == QStringLiteral("delimited")) {
        *protocol = Protocol::Delimited;
    } else if (normalized == QStringLiteral("keyvalue") || normalized == QStringLiteral("key-value")) {
        *protocol = Protocol::KeyValue;
    } else if (normalized == QStringLiteral("json")) {
        *protocol = Protocol::Json;
    } else if (normalized == QStringLiteral("binary")) {
        *protocol = Protocol::Binary;
    } else {
        return false;
    }
    return true;
}

QString binarySourceKey(BinarySource source)
{
    return source == BinarySource::Payload ? QStringLiteral("payload") : QStringLiteral("frame");
}

bool binarySourceFromKey(const QString &key, BinarySource *source)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("frame")) {
        *source = BinarySource::Frame;
    } else if (normalized == QStringLiteral("payload")) {
        *source = BinarySource::Payload;
    } else {
        return false;
    }
    return true;
}

QString binaryTypeKey(BinaryType type)
{
    switch (type) {
    case BinaryType::Int8:
        return QStringLiteral("i8");
    case BinaryType::UInt16:
        return QStringLiteral("u16");
    case BinaryType::Int16:
        return QStringLiteral("i16");
    case BinaryType::UInt32:
        return QStringLiteral("u32");
    case BinaryType::Int32:
        return QStringLiteral("i32");
    case BinaryType::UInt64:
        return QStringLiteral("u64");
    case BinaryType::Int64:
        return QStringLiteral("i64");
    case BinaryType::Float32:
        return QStringLiteral("f32");
    case BinaryType::Float64:
        return QStringLiteral("f64");
    case BinaryType::UInt8:
    default:
        return QStringLiteral("u8");
    }
}

bool binaryTypeFromKey(const QString &key, BinaryType *type)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("u8") || normalized == QStringLiteral("uint8")) {
        *type = BinaryType::UInt8;
    } else if (normalized == QStringLiteral("i8") || normalized == QStringLiteral("int8")) {
        *type = BinaryType::Int8;
    } else if (normalized == QStringLiteral("u16") || normalized == QStringLiteral("uint16")) {
        *type = BinaryType::UInt16;
    } else if (normalized == QStringLiteral("i16") || normalized == QStringLiteral("int16")) {
        *type = BinaryType::Int16;
    } else if (normalized == QStringLiteral("u32") || normalized == QStringLiteral("uint32")) {
        *type = BinaryType::UInt32;
    } else if (normalized == QStringLiteral("i32") || normalized == QStringLiteral("int32")) {
        *type = BinaryType::Int32;
    } else if (normalized == QStringLiteral("u64") || normalized == QStringLiteral("uint64")) {
        *type = BinaryType::UInt64;
    } else if (normalized == QStringLiteral("i64") || normalized == QStringLiteral("int64")) {
        *type = BinaryType::Int64;
    } else if (normalized == QStringLiteral("f32") || normalized == QStringLiteral("float") ||
               normalized == QStringLiteral("float32")) {
        *type = BinaryType::Float32;
    } else if (normalized == QStringLiteral("f64") || normalized == QStringLiteral("double") ||
               normalized == QStringLiteral("float64")) {
        *type = BinaryType::Float64;
    } else {
        return false;
    }
    return true;
}

QString byteOrderKey(ByteOrder byteOrder)
{
    return byteOrder == ByteOrder::BigEndian ? QStringLiteral("big") : QStringLiteral("little");
}

bool byteOrderFromKey(const QString &key, ByteOrder *byteOrder)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized.isEmpty() || normalized == QStringLiteral("little") || normalized == QStringLiteral("le")) {
        *byteOrder = ByteOrder::LittleEndian;
    } else if (normalized == QStringLiteral("big") || normalized == QStringLiteral("be")) {
        *byteOrder = ByteOrder::BigEndian;
    } else {
        return false;
    }
    return true;
}

bool parserConfigFromJson(const QJsonObject &object, ParserConfig *config, QString *error)
{
    if (!config || !error) {
        return false;
    }
    error->clear();
    ParserConfig parsed;
    const QString protocol = object.value(QStringLiteral("protocol")).toString(QStringLiteral("numbers"));
    if (!protocolFromKey(protocol, &parsed.protocol)) {
        *error = QStringLiteral("Unsupported plot protocol: %1").arg(protocol);
        return false;
    }
    if (!jsonStringList(object.value(QStringLiteral("fields")), &parsed.fields, error)) {
        return false;
    }
    const QString source = object.value(QStringLiteral("binarySource")).toString(QStringLiteral("frame"));
    if (!binarySourceFromKey(source, &parsed.binarySource)) {
        *error = QStringLiteral("Unsupported binary source: %1").arg(source);
        return false;
    }

    const QJsonValue binaryFieldsValue = object.value(QStringLiteral("binaryFields"));
    if (!binaryFieldsValue.isUndefined() && !binaryFieldsValue.isNull() && !binaryFieldsValue.isArray()) {
        *error = QStringLiteral("binaryFields must be an array of objects");
        return false;
    }
    for (const QJsonValue &entry : binaryFieldsValue.toArray()) {
        if (!entry.isObject()) {
            *error = QStringLiteral("binaryFields must contain objects");
            return false;
        }
        const QJsonObject fieldObject = entry.toObject();
        BinaryField field;
        field.name = fieldObject.value(QStringLiteral("name")).toString().simplified();
        if (field.name.isEmpty()) {
            *error = QStringLiteral("Each binary field requires a non-empty name");
            return false;
        }
        const QJsonValue byteOffset = fieldObject.value(QStringLiteral("byteOffset"));
        if (!byteOffset.isDouble() || byteOffset.toDouble() < 0 ||
            byteOffset.toDouble() != std::floor(byteOffset.toDouble()) ||
            byteOffset.toDouble() > std::numeric_limits<int>::max()) {
            *error = QStringLiteral("Binary field '%1' has an invalid byteOffset").arg(field.name);
            return false;
        }
        field.byteOffset = byteOffset.toInt();
        const QString type = fieldObject.value(QStringLiteral("type")).toString();
        if (!binaryTypeFromKey(type, &field.type)) {
            *error = QStringLiteral("Binary field '%1' has an unsupported type: %2").arg(field.name, type);
            return false;
        }
        const QString order = fieldObject.value(QStringLiteral("byteOrder")).toString(QStringLiteral("little"));
        if (!byteOrderFromKey(order, &field.byteOrder)) {
            *error = QStringLiteral("Binary field '%1' has an unsupported byteOrder: %2").arg(field.name, order);
            return false;
        }
        const QJsonValue scaleValue = fieldObject.value(QStringLiteral("scale"));
        const QJsonValue addValue = fieldObject.value(QStringLiteral("add"));
        if ((!scaleValue.isUndefined() && !scaleValue.isDouble()) ||
            (!addValue.isUndefined() && !addValue.isDouble())) {
            *error = QStringLiteral("Binary field '%1' has an invalid scale or add value").arg(field.name);
            return false;
        }
        field.scale = scaleValue.toDouble(1.0);
        field.add = addValue.toDouble(0.0);
        if (!std::isfinite(field.scale) || !std::isfinite(field.add)) {
            *error = QStringLiteral("Binary field '%1' has a non-finite scale or add value").arg(field.name);
            return false;
        }
        parsed.binaryFields.append(field);
    }

    *config = parsed;
    return true;
}

QJsonObject parserConfigToJson(const ParserConfig &config)
{
    QJsonObject object;
    object.insert(QStringLiteral("protocol"), protocolKey(config.protocol));
    object.insert(QStringLiteral("fields"), QJsonArray::fromStringList(config.fields));
    object.insert(QStringLiteral("binarySource"), binarySourceKey(config.binarySource));
    QJsonArray binaryFields;
    for (const BinaryField &field : config.binaryFields) {
        QJsonObject fieldObject;
        fieldObject.insert(QStringLiteral("name"), field.name);
        fieldObject.insert(QStringLiteral("byteOffset"), field.byteOffset);
        fieldObject.insert(QStringLiteral("type"), binaryTypeKey(field.type));
        fieldObject.insert(QStringLiteral("byteOrder"), byteOrderKey(field.byteOrder));
        fieldObject.insert(QStringLiteral("scale"), field.scale);
        fieldObject.insert(QStringLiteral("add"), field.add);
        binaryFields.append(fieldObject);
    }
    object.insert(QStringLiteral("binaryFields"), binaryFields);
    return object;
}

QVector<PlotSample> extractSamples(const ParserConfig &config, const QString &text, const QByteArray &frame,
                                   const QByteArray &payload)
{
    const QStringList filters = normalizedFilters(config.fields);
    QVector<PlotSample> samples;
    if (config.protocol == Protocol::Binary) {
        const QByteArray &data = config.binarySource == BinarySource::Payload ? payload : frame;
        PlotSample values = applyFieldFilter(extractBinary(config, data), filters);
        if (!values.isEmpty()) {
            samples.append(values);
        }
        return samples;
    }

    if (config.protocol == Protocol::Json) {
        PlotSample values;
        if (extractJsonDocument(text, &values)) {
            values = applyFieldFilter(values, filters);
            if (!values.isEmpty()) {
                samples.append(values);
            }
            return samples;
        }
        for (const QString &line : textLines(text)) {
            PlotSample lineValues;
            if (extractJsonDocument(line, &lineValues)) {
                lineValues = applyFieldFilter(lineValues, filters);
                if (!lineValues.isEmpty()) {
                    samples.append(lineValues);
                }
            }
        }
        return samples;
    }

    for (const QString &line : textLines(text)) {
        PlotSample values;
        switch (config.protocol) {
        case Protocol::Delimited:
            values = extractDelimited(line);
            break;
        case Protocol::KeyValue:
            values = extractKeyValues(line);
            break;
        case Protocol::Numbers:
        default:
            values = extractNumbers(line);
            break;
        }
        values = applyFieldFilter(values, filters);
        if (!values.isEmpty()) {
            samples.append(values);
        }
    }
    return samples;
}

} // namespace AppPlot
