#include "app/core/checksum_utils.h"
#include "app/core/modbus_utils.h"
#include "app/core/protocol_template.h"

#include <QtCore/QTranslator>
#include <QtTest/QTest>

#include <functional>

class FrameCodecsTest : public QObject
{
    Q_OBJECT

  private slots:
    void checksumsMatchStandardCheckValues_data()
    {
        QTest::addColumn<QString>("algorithm");
        QTest::addColumn<quint32>("value");
        // Check values of each algorithm for the ASCII input "123456789".
        QTest::newRow("crc16-modbus") << QStringLiteral("crc16-modbus") << quint32(0x4B37);
        QTest::newRow("crc16-ccitt-false") << QStringLiteral("crc16-ccitt") << quint32(0x29B1);
        QTest::newRow("crc32") << QStringLiteral("crc32") << quint32(0xCBF43926);
        QTest::newRow("modbus-alias") << QStringLiteral(" Modbus ") << quint32(0x4B37);
    }

    void checksumsMatchStandardCheckValues()
    {
        QFETCH(QString, algorithm);
        QFETCH(quint32, value);
        const auto result =
            AppChecksum::calculate(QByteArrayLiteral("123456789"), algorithm, AppChecksum::ByteOrder::BigEndian);
        QVERIFY(result.ok);
        QCOMPARE(result.value, value);
    }

    void unsupportedChecksumAlgorithmIsReportedInsteadOfReplaced()
    {
        QVERIFY(AppChecksum::isSupportedAlgorithm(QStringLiteral("CRC32")));
        QVERIFY(!AppChecksum::isSupportedAlgorithm(QStringLiteral("crc8")));
        const auto unknown = AppChecksum::calculate(QByteArrayLiteral("123456789"), QStringLiteral("crc8"),
                                                    AppChecksum::ByteOrder::LittleEndian);
        QVERIFY(!unknown.ok);
        QVERIFY(!unknown.errorMessage.isEmpty());

        AppProtocol::ProtocolTemplate protocol = AppProtocol::defaultTemplate();
        QByteArray checked = QByteArray::fromHex("AA550310010203");
        checked +=AppChecksum::calculate(checked, protocol.checksumAlgorithm, protocol.checksumByteOrder).bytes;
        QVERIFY(AppProtocol::parseFrame(checked, protocol).checksumValid);
        // A template naming an unsupported algorithm must not be checked as CRC16-Modbus.
        protocol.checksumAlgorithm = QStringLiteral("crc8");
        const auto parsed = AppProtocol::parseFrame(checked, protocol);
        QVERIFY(!parsed.ok);
        QVERIFY(!parsed.checksumChecked);
        QCOMPARE(parsed.errorMessage, unknown.errorMessage);
    }

    void exampleFramesParseWithTheirTemplate()
    {
        AppProtocol::ProtocolTemplate frameLength = AppProtocol::defaultTemplate();
        frameLength.lengthMode = AppProtocol::LengthMode::FrameLength;
        frameLength.lengthSize = 2;
        frameLength.lengthByteOrder = AppChecksum::ByteOrder::LittleEndian;
        frameLength.commandOffset = 4;
        frameLength.payloadOffset = 5;
        frameLength.checksumAlgorithm = QStringLiteral("crc32");
        for (const AppProtocol::ProtocolTemplate &protocol : {AppProtocol::defaultTemplate(), frameLength}) {
            const auto parsed = AppProtocol::parseFrame(AppProtocol::exampleFrame(protocol), protocol);
            QVERIFY2(parsed.ok && parsed.checksumValid, qPrintable(parsed.errorMessage));
        }
        // A header longer than the frame its fields describe cannot form a frame.
        AppProtocol::ProtocolTemplate impossible = AppProtocol::defaultTemplate();
        impossible.header = QByteArray(16, '\xAA');
        QVERIFY(AppProtocol::exampleFrame(impossible).isEmpty());
    }

    void frameLayoutMatchesTheParser()
    {
        AppProtocol::ProtocolTemplate frameLength = AppProtocol::defaultTemplate();
        frameLength.lengthMode = AppProtocol::LengthMode::FrameLength;
        AppProtocol::ProtocolTemplate fixedPayload = AppProtocol::defaultTemplate();
        fixedPayload.lengthSize = 0;
        fixedPayload.payloadLength = 2;
        AppProtocol::ProtocolTemplate noLength = AppProtocol::defaultTemplate();
        noLength.lengthSize = 0;
        for (const AppProtocol::ProtocolTemplate &protocol :
             {AppProtocol::defaultTemplate(), frameLength, fixedPayload, noLength}) {
            const QByteArray frame = AppProtocol::exampleFrame(protocol);
            const auto parsed = AppProtocol::parseFrame(frame, protocol);
            QVERIFY2(parsed.ok && parsed.checksumValid, qPrintable(parsed.errorMessage));
            const auto layout = AppProtocol::frameLayout(frame, protocol);
            QCOMPARE(layout.lengthValue, parsed.lengthValue);
            QCOMPARE(layout.frameLength, qint64(parsed.frameLength));
            QCOMPARE(layout.payloadLength, qint64(parsed.payload.size()));
            QCOMPARE(layout.checksumOffset, qint64(parsed.frameLength - parsed.checksum.size()));
        }
        // A length field past the end of the frame reads as 0.
        const auto truncated = AppProtocol::frameLayout(QByteArray::fromHex("AA55"), AppProtocol::defaultTemplate());
        QCOMPARE(truncated.lengthValue, 0u);
        QCOMPARE(truncated.frameLength, qint64(6));
    }

    void templatesCompareEveryField()
    {
        const AppProtocol::ProtocolTemplate base = AppProtocol::defaultTemplate();
        QVERIFY(base == AppProtocol::defaultTemplate());
        const QList<std::function<void(AppProtocol::ProtocolTemplate &)>> changes = {
            [](auto &item) { item.name = QStringLiteral("other"); },
            [](auto &item) { item.header = QByteArray::fromHex("BB66"); },
            [](auto &item) { item.lengthOffset = 1; },
            [](auto &item) { item.lengthSize = 2; },
            [](auto &item) { item.lengthMode = AppProtocol::LengthMode::FrameLength; },
            [](auto &item) { item.lengthByteOrder = AppChecksum::ByteOrder::LittleEndian; },
            [](auto &item) { item.commandOffset = 4; },
            [](auto &item) { item.commandSize = 2; },
            [](auto &item) { item.payloadOffset = 5; },
            [](auto &item) { item.payloadLength = 3; },
            [](auto &item) { item.checksumAlgorithm = QStringLiteral("crc32"); },
            [](auto &item) { item.checksumByteOrder = AppChecksum::ByteOrder::BigEndian; },
        };
        for (const auto &change : changes) {
            AppProtocol::ProtocolTemplate changed = base;
            change(changed);
            QVERIFY(!(changed == base));
        }
    }

    void englishModbusSummariesKeepTheirCase()
    {
        Q_INIT_RESOURCE(app);
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/app/i18n/fluentserialassistant.en_US.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto withCrc = [](QByteArray frame) {
            return frame + AppChecksum::calculate(frame, QStringLiteral("crc16-modbus"),
                                                  AppChecksum::ByteOrder::LittleEndian)
                               .bytes;
        };
        const auto response = AppModbus::parseResponse(withCrc(QByteArray::fromHex("010F0000000A")));
        const auto exception = AppModbus::parseResponse(withCrc(QByteArray::fromHex("018F02")));
        QCoreApplication::removeTranslator(&translator);
        QVERIFY(response.recognized && exception.recognized);
        // Only the function code is upper case.
        QVERIFY2(response.summary.startsWith(QStringLiteral("Modbus response · Slave ID 1 · Function 0x0F")),
                 qPrintable(response.summary));
        QVERIFY2(exception.summary.startsWith(QStringLiteral("Modbus exception · Slave ID 1 · Function 0x8F")),
                 qPrintable(exception.summary));
    }

    void modbusQuantitiesFitOneFrame_data()
    {
        QTest::addColumn<QString>("function");
        QTest::addColumn<int>("maximum");
        QTest::newRow("read-coils") << QStringLiteral("read-coils") << 2000;
        QTest::newRow("read-registers") << QStringLiteral("read-holding-registers") << 125;
        QTest::newRow("write-coils") << QStringLiteral("write-multiple-coils") << 1968;
        QTest::newRow("write-registers") << QStringLiteral("write-multiple-registers") << 123;
        QTest::newRow("write-single-register") << QStringLiteral("write-single-register") << 1;
    }

    void modbusQuantitiesFitOneFrame()
    {
        QFETCH(QString, function);
        QFETCH(int, maximum);
        const AppModbus::FunctionOption option = AppModbus::functionForKey(function);
        QCOMPARE(int(AppModbus::maximumQuantity(option)), maximum);
        const auto values = [&option](int count) {
            return QStringList(count, option.coil ? QStringLiteral("1") : QStringLiteral("7")).join(QLatin1Char(' '));
        };

        AppModbus::RequestConfig config;
        config.functionKey = function;
        config.quantity = static_cast<quint16>(maximum);
        config.valuesText = option.write ? values(maximum) : QString();
        const auto accepted = AppModbus::buildRequest(config);
        QVERIFY2(accepted.ok, qPrintable(accepted.errorMessage));
        // An RTU frame (address, PDU and CRC) holds at most 256 bytes.
        QVERIFY(accepted.frame.size() <= 256);

        if (option.write && !option.multiple) {
            return;
        }
        config.quantity = static_cast<quint16>(maximum + 1);
        config.valuesText = option.write ? values(maximum + 1) : QString();
        const auto rejected = AppModbus::buildRequest(config);
        QVERIFY(!rejected.ok);
        QVERIFY(rejected.errorMessage.contains(QString::number(maximum)));
    }
};

QTEST_GUILESS_MAIN(FrameCodecsTest)
#include "tst_frame_codecs.moc"
