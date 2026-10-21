#include "app/core/plot_value_parser.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtTest/QTest>

#include <limits>

namespace {

using AppPlot::BinaryType;
using AppPlot::ByteOrder;
using AppPlot::Protocol;

AppPlot::ParserConfig parser(Protocol protocol, const QStringList &fields = {})
{
    AppPlot::ParserConfig config;
    config.protocol = protocol;
    config.fields = fields;
    return config;
}

AppPlot::BinaryField binaryField(const QString &name, int byteOffset, BinaryType type,
                                 ByteOrder byteOrder = ByteOrder::LittleEndian, double scale = 1.0, double add = 0.0)
{
    AppPlot::BinaryField field;
    field.name = name;
    field.byteOffset = byteOffset;
    field.type = type;
    field.byteOrder = byteOrder;
    field.scale = scale;
    field.add = add;
    return field;
}

QStringList names(const AppPlot::PlotSample &sample)
{
    QStringList result;
    for (const AppPlot::PlotValue &value : sample) {
        result.append(value.name);
    }
    return result;
}

QVector<double> values(const AppPlot::PlotSample &sample)
{
    QVector<double> result;
    for (const AppPlot::PlotValue &value : sample) {
        result.append(value.value);
    }
    return result;
}

} // namespace

class PlotValueParserTest : public QObject
{
    Q_OBJECT

  private slots:
    void numbersTakeEveryNumberOnEachLine()
    {
        const auto samples = AppPlot::extractSamples(parser(Protocol::Numbers),
                                                     QStringLiteral("t=1.5, -2e3 and .5\r\nfoo 7\n\n1e999 2"));
        QCOMPARE(samples.size(), 3);
        QCOMPARE(names(samples.at(0)), QStringList({QStringLiteral("CH1"), QStringLiteral("CH2"), QStringLiteral("CH3")}));
        QCOMPARE(values(samples.at(0)), QVector<double>({1.5, -2000.0, 0.5}));
        QCOMPARE(values(samples.at(1)), QVector<double>({7.0}));
        // A number too large for a double is dropped.
        QCOMPARE(values(samples.at(2)), QVector<double>({2.0}));
        QVERIFY(AppPlot::extractSamples(parser(Protocol::Numbers), QStringLiteral("no numbers\n  ")).isEmpty());
    }

    void delimitedTakesOnlyWholeNumberTokens()
    {
        const auto samples =
            AppPlot::extractSamples(parser(Protocol::Delimited), QStringLiteral("1, 2;3\tx 4abc -5\n6,,7"));
        QCOMPARE(samples.size(), 2);
        QCOMPARE(values(samples.at(0)), QVector<double>({1.0, 2.0, 3.0, -5.0}));
        QCOMPARE(values(samples.at(1)), QVector<double>({6.0, 7.0}));
    }

    void keyValuesNameTheirFields()
    {
        const auto samples = AppPlot::extractSamples(
            parser(Protocol::KeyValue),
            QStringLiteral("temperature=20.5 humidity: 60, 水温=18 motor speed = 1200 v1.2/x=3"));
        QCOMPARE(samples.size(), 1);
        QCOMPARE(names(samples.first()),
                 QStringList({QStringLiteral("temperature"), QStringLiteral("humidity"), QStringLiteral("水温"),
                              QStringLiteral("motor speed"), QStringLiteral("v1.2/x")}));
        QCOMPARE(values(samples.first()), QVector<double>({20.5, 60.0, 18.0, 1200.0, 3.0}));
    }

    void fieldFiltersIgnoreCaseAndSpacing()
    {
        const auto named =
            AppPlot::extractSamples(parser(Protocol::KeyValue, {QStringLiteral("Motor  Speed"), QStringLiteral("TEMPERATURE")}),
                                    QStringLiteral("temperature=1 humidity=2 motor speed=3\nhumidity=4"));
        // The second line has no selected field, so it is not a sample.
        QCOMPARE(named.size(), 1);
        QCOMPARE(names(named.first()), QStringList({QStringLiteral("temperature"), QStringLiteral("motor speed")}));

        // Unnamed values are selected by their channel name.
        const auto unnamed =
            AppPlot::extractSamples(parser(Protocol::Numbers, {QStringLiteral("ch2")}), QStringLiteral("10 20 30"));
        QCOMPARE(unnamed.size(), 1);
        QCOMPARE(names(unnamed.first()), QStringList({QStringLiteral("CH2")}));
        QCOMPARE(values(unnamed.first()), QVector<double>({20.0}));
    }

    void jsonReadsNumbersByPath()
    {
        const QString document =
            QStringLiteral("{\n  \"a\": 1,\n  \"b\": {\"c\": [2, [3]]},\n  \"s\": \"4\",\n  \"t\": true\n}");
        const auto whole = AppPlot::extractSamples(parser(Protocol::Json), document);
        QCOMPARE(whole.size(), 1);
        QCOMPARE(names(whole.first()),
                 QStringList({QStringLiteral("a"), QStringLiteral("b.c[0]"), QStringLiteral("b.c[1][0]")}));
        QCOMPARE(values(whole.first()), QVector<double>({1.0, 2.0, 3.0}));

        // When the text is not one document, each line is read on its own.
        const auto lines =
            AppPlot::extractSamples(parser(Protocol::Json), QStringLiteral("{\"a\": 1}\nnot json\n[5, 6]"));
        QCOMPARE(lines.size(), 2);
        QCOMPARE(names(lines.at(0)), QStringList({QStringLiteral("a")}));
        QCOMPARE(names(lines.at(1)), QStringList({QStringLiteral("CH1"), QStringLiteral("CH2")}));
        QCOMPARE(values(lines.at(1)), QVector<double>({5.0, 6.0}));

        const auto filtered =
            AppPlot::extractSamples(parser(Protocol::Json, {QStringLiteral("B.C[0]")}), document);
        QCOMPARE(names(filtered.first()), QStringList({QStringLiteral("b.c[0]")}));
    }

    void binaryFieldsDecodeTypeOrderAndScale()
    {
        AppPlot::ParserConfig config = parser(Protocol::Binary);
        config.binaryFields = {
            binaryField(QStringLiteral("u16"), 0, BinaryType::UInt16),
            binaryField(QStringLiteral("i16"), 2, BinaryType::Int16, ByteOrder::BigEndian, 0.5, 1.0),
            binaryField(QStringLiteral("f32"), 4, BinaryType::Float32),
            binaryField(QStringLiteral("i8"), 8, BinaryType::Int8),
            binaryField(QStringLiteral("u64"), 9, BinaryType::UInt64, ByteOrder::BigEndian),
            binaryField(QStringLiteral("past end"), 16, BinaryType::UInt16),
        };
        const QByteArray frame = QByteArray::fromHex("3412"
                                                     "FFFE"
                                                     "0000C03F"
                                                     "80"
                                                     "0000000000000102");
        const auto samples = AppPlot::extractSamples(config, QString(), frame);
        QCOMPARE(samples.size(), 1);
        // A field that does not fit in the frame is left out.
        QCOMPARE(names(samples.first()), QStringList({QStringLiteral("u16"), QStringLiteral("i16"),
                                                      QStringLiteral("f32"), QStringLiteral("i8"),
                                                      QStringLiteral("u64")}));
        QCOMPARE(values(samples.first()), QVector<double>({4660.0, 0.0, 1.5, -128.0, 258.0}));
    }

    void binaryPayloadSourceReadsThePayload()
    {
        AppPlot::ParserConfig config = parser(Protocol::Binary);
        config.binaryFields = {binaryField(QStringLiteral("value"), 0, BinaryType::UInt8)};
        const QByteArray frame = QByteArray::fromHex("AA5507");
        const QByteArray payload = QByteArray::fromHex("07");
        QCOMPARE(values(AppPlot::extractSamples(config, QString(), frame, payload).first()),
                 QVector<double>({170.0}));
        config.binarySource = AppPlot::BinarySource::Payload;
        QCOMPARE(values(AppPlot::extractSamples(config, QString(), frame, payload).first()), QVector<double>({7.0}));
    }

    void binaryWithoutFieldsPlotsEachByteUpToTheLimit()
    {
        QByteArray frame(AppPlot::MaximumChannels + 8, '\x02');
        frame[0] = '\x01';
        const auto samples = AppPlot::extractSamples(parser(Protocol::Binary), QString(), frame);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples.first().size(), AppPlot::MaximumChannels);
        QCOMPARE(samples.first().first().name, QStringLiteral("CH1"));
        QCOMPARE(samples.first().first().value, 1.0);
        QCOMPARE(samples.first().last().name, QStringLiteral("CH%1").arg(AppPlot::MaximumChannels));
        QVERIFY(AppPlot::extractSamples(parser(Protocol::Binary), QString(), QByteArray()).isEmpty());
    }

    void nonFiniteBinaryValuesAreDropped()
    {
        AppPlot::ParserConfig config = parser(Protocol::Binary);
        config.binaryFields = {
            binaryField(QStringLiteral("nan"), 0, BinaryType::Float32),
            binaryField(QStringLiteral("infinity"), 4, BinaryType::Float64),
            binaryField(QStringLiteral("overflow"), 12, BinaryType::Float64, ByteOrder::LittleEndian, 1e10),
        };
        QByteArray frame = QByteArray::fromHex("0000C07F"
                                               "000000000000F07F");
        const double large = std::numeric_limits<double>::max();
        frame.append(reinterpret_cast<const char *>(&large), sizeof(large));
        QVERIFY(AppPlot::extractSamples(config, QString(), frame).isEmpty());
    }

    void keysRoundTrip()
    {
        for (const QString &key : AppPlot::supportedProtocolKeys()) {
            Protocol protocol = Protocol::Numbers;
            QVERIFY2(AppPlot::protocolFromKey(key, &protocol), qPrintable(key));
            QCOMPARE(AppPlot::protocolKey(protocol), key);
        }
        for (BinaryType type : {BinaryType::UInt8, BinaryType::Int8, BinaryType::UInt16, BinaryType::Int16,
                                BinaryType::UInt32, BinaryType::Int32, BinaryType::UInt64, BinaryType::Int64,
                                BinaryType::Float32, BinaryType::Float64}) {
            BinaryType parsed = BinaryType::UInt8;
            QVERIFY(AppPlot::binaryTypeFromKey(AppPlot::binaryTypeKey(type), &parsed));
            QCOMPARE(parsed, type);
        }
        Protocol protocol = Protocol::Numbers;
        QVERIFY(!AppPlot::protocolFromKey(QStringLiteral("xml"), &protocol));
    }

    void parserConfigRoundTripsThroughJson()
    {
        AppPlot::ParserConfig config = parser(Protocol::Binary, {QStringLiteral("a"), QStringLiteral("motor speed")});
        config.binarySource = AppPlot::BinarySource::Payload;
        config.binaryFields = {binaryField(QStringLiteral("a"), 1, BinaryType::Float64, ByteOrder::BigEndian, 0.25, -3.0),
                               binaryField(QStringLiteral("motor speed"), 0, BinaryType::Int8)};
        AppPlot::ParserConfig restored;
        QString error;
        QVERIFY2(AppPlot::parserConfigFromJson(AppPlot::parserConfigToJson(config), &restored, &error),
                 qPrintable(error));
        QCOMPARE(restored.protocol, config.protocol);
        QCOMPARE(restored.fields, config.fields);
        QCOMPARE(restored.binarySource, config.binarySource);
        QCOMPARE(restored.binaryFields.size(), 2);
        const AppPlot::BinaryField &field = restored.binaryFields.first();
        QCOMPARE(field.name, QStringLiteral("a"));
        QCOMPARE(field.byteOffset, 1);
        QCOMPARE(field.type, BinaryType::Float64);
        QCOMPARE(field.byteOrder, ByteOrder::BigEndian);
        QCOMPARE(field.scale, 0.25);
        QCOMPARE(field.add, -3.0);
        QCOMPARE(AppPlot::parserConfigToJson(restored), AppPlot::parserConfigToJson(config));
    }

    void parserConfigJsonDefaultsAndAliases()
    {
        AppPlot::ParserConfig config = parser(Protocol::Json);
        QString error;
        QVERIFY(AppPlot::parserConfigFromJson(QJsonObject(), &config, &error));
        QCOMPARE(config.protocol, Protocol::Numbers);
        QVERIFY(config.fields.isEmpty());
        QCOMPARE(config.binarySource, AppPlot::BinarySource::Frame);
        QVERIFY(config.binaryFields.isEmpty());

        const QJsonObject aliases{
            {QStringLiteral("protocol"), QStringLiteral(" Key-Value ")},
            {QStringLiteral("fields"),
             QJsonArray{QStringLiteral("Temp"), QStringLiteral("temp"), QStringLiteral(" motor   speed ")}},
            {QStringLiteral("binaryFields"),
             QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral(" a  b ")},
                                    {QStringLiteral("byteOffset"), 2},
                                    {QStringLiteral("type"), QStringLiteral("UINT16")},
                                    {QStringLiteral("byteOrder"), QStringLiteral("BE")}},
                        QJsonObject{{QStringLiteral("name"), QStringLiteral("f")},
                                    {QStringLiteral("byteOffset"), 0},
                                    {QStringLiteral("type"), QStringLiteral("double")}}}}};
        QVERIFY2(AppPlot::parserConfigFromJson(aliases, &config, &error), qPrintable(error));
        QCOMPARE(config.protocol, Protocol::KeyValue);
        // Field names are trimmed and duplicates differing only in case are dropped.
        QCOMPARE(config.fields, QStringList({QStringLiteral("Temp"), QStringLiteral("motor speed")}));
        QCOMPARE(config.binaryFields.size(), 2);
        QCOMPARE(config.binaryFields.at(0).name, QStringLiteral("a b"));
        QCOMPARE(config.binaryFields.at(0).type, BinaryType::UInt16);
        QCOMPARE(config.binaryFields.at(0).byteOrder, ByteOrder::BigEndian);
        QCOMPARE(config.binaryFields.at(0).scale, 1.0);
        QCOMPARE(config.binaryFields.at(0).add, 0.0);
        QCOMPARE(config.binaryFields.at(1).type, BinaryType::Float64);
        QCOMPARE(config.binaryFields.at(1).byteOrder, ByteOrder::LittleEndian);
    }

    void parserConfigRejectsInvalidJson_data()
    {
        QTest::addColumn<QJsonObject>("object");
        QTest::addColumn<QString>("mention");
        const auto withField = [](const QString &key, const QJsonValue &value) {
            QJsonObject field{{QStringLiteral("name"), QStringLiteral("x")},
                              {QStringLiteral("byteOffset"), 0},
                              {QStringLiteral("type"), QStringLiteral("u8")}};
            field.insert(key, value);
            return QJsonObject{{QStringLiteral("binaryFields"), QJsonArray{field}}};
        };
        QTest::newRow("protocol") << QJsonObject{{QStringLiteral("protocol"), QStringLiteral("xml")}}
                                  << QStringLiteral("xml");
        QTest::newRow("fields not array") << QJsonObject{{QStringLiteral("fields"), QStringLiteral("temp")}}
                                          << QStringLiteral("fields");
        QTest::newRow("blank field") << QJsonObject{{QStringLiteral("fields"), QJsonArray{QStringLiteral(" ")}}}
                                     << QStringLiteral("fields");
        QTest::newRow("numeric field") << QJsonObject{{QStringLiteral("fields"), QJsonArray{1}}}
                                       << QStringLiteral("fields");
        QTest::newRow("source") << QJsonObject{{QStringLiteral("binarySource"), QStringLiteral("body")}}
                                << QStringLiteral("body");
        QTest::newRow("binary fields not array") << QJsonObject{{QStringLiteral("binaryFields"), QJsonObject()}}
                                                 << QStringLiteral("binaryFields");
        QTest::newRow("binary field not object") << QJsonObject{{QStringLiteral("binaryFields"), QJsonArray{1}}}
                                                 << QStringLiteral("binaryFields");
        QTest::newRow("unnamed") << withField(QStringLiteral("name"), QStringLiteral(" ")) << QStringLiteral("name");
        QTest::newRow("negative offset") << withField(QStringLiteral("byteOffset"), -1) << QStringLiteral("'x'");
        QTest::newRow("fractional offset") << withField(QStringLiteral("byteOffset"), 1.5) << QStringLiteral("'x'");
        QTest::newRow("text offset") << withField(QStringLiteral("byteOffset"), QStringLiteral("1"))
                                     << QStringLiteral("'x'");
        QTest::newRow("huge offset") << withField(QStringLiteral("byteOffset"), 3e9) << QStringLiteral("'x'");
        QTest::newRow("type") << withField(QStringLiteral("type"), QStringLiteral("u24")) << QStringLiteral("u24");
        QTest::newRow("byte order") << withField(QStringLiteral("byteOrder"), QStringLiteral("middle"))
                                    << QStringLiteral("middle");
        QTest::newRow("text scale") << withField(QStringLiteral("scale"), QStringLiteral("2")) << QStringLiteral("'x'");
        QTest::newRow("infinite add") << withField(QStringLiteral("add"), std::numeric_limits<double>::infinity())
                                      << QStringLiteral("'x'");
    }

    void parserConfigRejectsInvalidJson()
    {
        QFETCH(QJsonObject, object);
        QFETCH(QString, mention);
        AppPlot::ParserConfig config = parser(Protocol::Json, {QStringLiteral("kept")});
        QString error;
        QVERIFY(!AppPlot::parserConfigFromJson(object, &config, &error));
        QVERIFY2(error.contains(mention), qPrintable(error));
        // A rejected configuration leaves the previous one in place.
        QCOMPARE(config.protocol, Protocol::Json);
        QCOMPARE(config.fields, QStringList({QStringLiteral("kept")}));
    }
};

QTEST_GUILESS_MAIN(PlotValueParserTest)
#include "tst_plot_value_parser.moc"
