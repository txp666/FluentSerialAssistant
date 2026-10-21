#include "app/core/checksum_utils.h"
#include "app/core/modbus_utils.h"
#include "app/core/protocol_template.h"

#include <QtTest/QTest>

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
