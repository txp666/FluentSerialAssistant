#include "app/core/app_settings.h"
#include "app/serial/serial_controller.h"
#include "app/serial/virtual_serial_pair.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

#include <memory>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

class VirtualSerialPairTest : public QObject
{
    Q_OBJECT

  private:
    static SerialPortConfig config(const QString &name)
    {
        SerialPortConfig result;
        result.portName = name;
        return result;
    }

    static QByteArray received(const QSignalSpy &spy)
    {
        QByteArray result;
        for (const QList<QVariant> &arguments : spy) {
            result.append(arguments.at(0).toByteArray());
        }
        return result;
    }

    static QStringList virtualPorts()
    {
        QStringList names;
        for (const SerialPortDescriptor &port : SerialController::availablePorts()) {
            if (VirtualSerialPair::isVirtualPort(port.portName)) {
                names.append(port.portName);
            }
        }
        return names;
    }

  private slots:
    void initTestCase()
    {
        AppSettings settings;
        settings.clear();
        settings.sync();
        QVERIFY(!VirtualSerialPair::instance()->isEnabled());
    }

    void init() { VirtualSerialPair::instance()->setEnabled(false); }

    void cleanup()
    {
        VirtualSerialPair::instance()->setEnabled(false);
        QCoreApplication::processEvents();
    }

    void defaultsDisabledAndPersistsToggle()
    {
        auto *pair = VirtualSerialPair::instance();
        QVERIFY(virtualPorts().isEmpty());
        QVERIFY(!VirtualSerialPair::isVirtualPort(QStringLiteral("COM1")));
        SerialController controller;
        QSignalSpy errors(&controller, &SerialController::errorOccurred);
        QVERIFY(!controller.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(!controller.isOpen());
        QVERIFY(!controller.errorString().isEmpty());
        QCOMPARE(errors.size(), 1);

        QSignalSpy toggles(pair, &VirtualSerialPair::enabledChanged);
        pair->setEnabled(true);
        pair->setEnabled(true);
        QCOMPARE(toggles.size(), 1);
        QCOMPARE(virtualPorts(), (QStringList{VirtualSerialPair::portAName(), VirtualSerialPair::portBName()}));
        AppSettings settings;
        QCOMPARE(settings.value(QStringLiteral("serial/virtualPairEnabled")).toBool(), true);
        pair->setEnabled(false);
        settings.sync();
        QCOMPARE(settings.value(QStringLiteral("serial/virtualPairEnabled")).toBool(), false);
        QCOMPARE(toggles.size(), 2);
        QVERIFY(virtualPorts().isEmpty());
    }

    void bidirectionalBinaryTrafficIsAsynchronousAndOrdered()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        SerialController a;
        SerialController b;
        QSignalSpy openedA(&a, &SerialController::opened);
        QSignalSpy rxA(&a, &SerialController::dataReceived);
        QSignalSpy rxB(&b, &SerialController::dataReceived);
        QSignalSpy txA(&a, &SerialController::writeQueued);
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        SerialPortConfig bConfig = config(VirtualSerialPair::portBName());
        bConfig.baudRate = 9600;
        bConfig.parity = QSerialPort::OddParity;
        QVERIFY(b.openPort(bConfig));
        QCOMPARE(openedA.size(), 1);
        QCOMPARE(openedA.at(0).at(0).toString(), VirtualSerialPair::portAName());
        QVERIFY(a.isOpen());
        QCOMPARE(a.portName(), VirtualSerialPair::portAName());
        QVERIFY(a.errorString().isEmpty());
        QVERIFY(a.setRequestToSend(true));
        QVERIFY(a.setDataTerminalReady(false));

        const QByteArray first = QByteArray::fromHex("0001027f80ff");
        const QByteArray second = QByteArray::fromHex("ff00aabbcc0d0a");
        QVERIFY(a.writeData(first));
        QVERIFY(a.writeData(second));
        QVERIFY(b.writeData(second));
        QVERIFY(b.writeData(first));
        QVERIFY(rxA.isEmpty());
        QVERIFY(rxB.isEmpty());
        QTRY_COMPARE(received(rxA), second + first);
        QTRY_COMPARE(received(rxB), first + second);
        QCOMPARE(rxA.size(), 1);
        QCOMPARE(rxB.size(), 1);
        QCOMPARE(txA.size(), 2);
        QCOMPARE(txA.at(0).at(0).toLongLong(), first.size());
        QCOMPARE(txA.at(1).at(0).toLongLong(), second.size());

        a.closePort();
        QVERIFY(!a.isOpen());
        QCOMPARE(a.portName(), VirtualSerialPair::portAName());
        QVERIFY(!a.setRequestToSend(false));
        QVERIFY(!a.setDataTerminalReady(true));
        QString error;
        QVERIFY(!a.writeData(first, &error));
        QVERIFY(!error.isEmpty());
    }

    void occupiedEndpointRejectsSecondSession()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        SerialController a;
        SerialController b;
        SerialController duplicate;
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QSignalSpy errors(&duplicate, &SerialController::errorOccurred);
        QVERIFY(!duplicate.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(!duplicate.isOpen());
        QVERIFY(duplicate.errorString().contains(VirtualSerialPair::portAName()));
        QCOMPARE(errors.size(), 1);
        QVERIFY(!duplicate.openPort(config(VirtualSerialPair::portBName())));
        QVERIFY(a.isOpen());
        QVERIFY(b.isOpen());
        a.closePort();
        QVERIFY(duplicate.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(duplicate.errorString().isEmpty());
    }

    void disconnectedPeerDropsBytes()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        SerialController a;
        SerialController b;
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(a.writeData(QByteArrayLiteral("before connection")));
        QSignalSpy rxB(&b, &SerialController::dataReceived);
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QCoreApplication::processEvents();
        QVERIFY(rxB.isEmpty());
        QVERIFY(a.writeData(QByteArrayLiteral("connected")));
        QTRY_COMPARE(received(rxB), QByteArrayLiteral("connected"));
        b.closePort();
        QVERIFY(a.writeData(QByteArrayLiteral("after disconnect")));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QCoreApplication::processEvents();
        QCOMPARE(received(rxB), QByteArrayLiteral("connected"));
    }

    void closeAndReopenDiscardsPending_data()
    {
        QTest::addColumn<bool>("closeSender");
        QTest::newRow("sender") << true;
        QTest::newRow("receiver") << false;
    }

    void closeAndReopenDiscardsPending()
    {
        QFETCH(bool, closeSender);
        VirtualSerialPair::instance()->setEnabled(true);
        SerialController a;
        SerialController b;
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QSignalSpy rxA(&a, &SerialController::dataReceived);
        QSignalSpy rxB(&b, &SerialController::dataReceived);
        for (int count = 0; count < 20; ++count) {
            QVERIFY(a.writeData(QByteArrayLiteral("old A")));
            QVERIFY(b.writeData(QByteArrayLiteral("old B")));
            SerialController &reopened = closeSender ? a : b;
            reopened.closePort();
            QVERIFY(reopened.openPort(
                config(closeSender ? VirtualSerialPair::portAName() : VirtualSerialPair::portBName())));
        }
        QVERIFY(a.writeData(QByteArrayLiteral("new A")));
        QVERIFY(b.writeData(QByteArrayLiteral("new B")));
        QTRY_COMPARE(received(rxB), QByteArrayLiteral("new A"));
        QTRY_COMPARE(received(rxA), QByteArrayLiteral("new B"));
        QCOMPARE(rxA.size(), 1);
        QCOMPARE(rxB.size(), 1);
    }

    void disablingClosesEndpointsBeforeNotifyingAndDropsPending()
    {
        auto *pair = VirtualSerialPair::instance();
        pair->setEnabled(true);
        SerialController a;
        SerialController b;
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QSignalSpy rxA(&a, &SerialController::dataReceived);
        QSignalSpy rxB(&b, &SerialController::dataReceived);
        QStringList events;
        bool disabledWhenClosing = true;
        connect(&a, &SerialController::closed, &a, [&]() {
            disabledWhenClosing = disabledWhenClosing && !pair->isEnabled();
            events.append(QStringLiteral("closed-a"));
        });
        connect(&b, &SerialController::closed, &b, [&]() {
            disabledWhenClosing = disabledWhenClosing && !pair->isEnabled();
            events.append(QStringLiteral("closed-b"));
        });
        connect(pair, &VirtualSerialPair::enabledChanged, &a,
                [&](bool enabled) { events.append(enabled ? QStringLiteral("enabled") : QStringLiteral("disabled")); });
        QVERIFY(a.writeData(QByteArrayLiteral("old A")));
        QVERIFY(b.writeData(QByteArrayLiteral("old B")));
        pair->setEnabled(false);
        QVERIFY(disabledWhenClosing);
        QCOMPARE(events,
                 (QStringList{QStringLiteral("closed-a"), QStringLiteral("closed-b"), QStringLiteral("disabled")}));
        QVERIFY(!a.isOpen());
        QVERIFY(!b.isOpen());
        QVERIFY(!a.openPort(config(VirtualSerialPair::portAName())));
        pair->setEnabled(true);
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QCoreApplication::processEvents();
        QVERIFY(rxA.isEmpty());
        QVERIFY(rxB.isEmpty());
        QVERIFY(a.writeData(QByteArrayLiteral("new")));
        QTRY_COMPARE(received(rxB), QByteArrayLiteral("new"));
    }

    void destroyingSessionReleasesEndpointAndPendingTraffic()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        auto a = std::make_unique<SerialController>();
        auto b = std::make_unique<SerialController>();
        QVERIFY(a->openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b->openPort(config(VirtualSerialPair::portBName())));
        QSignalSpy rxA(a.get(), &SerialController::dataReceived);
        QVERIFY(a->writeData(QByteArrayLiteral("old A")));
        QVERIFY(b->writeData(QByteArrayLiteral("old B")));
        b.reset();
        b = std::make_unique<SerialController>();
        QVERIFY(b->openPort(config(VirtualSerialPair::portBName())));
        QSignalSpy rxB(b.get(), &SerialController::dataReceived);
        QCoreApplication::processEvents();
        QVERIFY(rxA.isEmpty());
        QVERIFY(rxB.isEmpty());
        QVERIFY(b->writeData(QByteArrayLiteral("new B")));
        QTRY_COMPARE(received(rxA), QByteArrayLiteral("new B"));
        a.reset();
        a = std::make_unique<SerialController>();
        QVERIFY(a->openPort(config(VirtualSerialPair::portAName())));
    }

    void bufferLimitRejectsWholeWriteAndRecoversAfterDelivery()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        SerialController a;
        SerialController b;
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        QSignalSpy rxB(&b, &SerialController::dataReceived);
        QSignalSpy errors(&a, &SerialController::errorOccurred);
        const QByteArray prefix(1024 * 1024 - 4, 'x');
        QVERIFY(a.writeData(prefix));
        QString error;
        QVERIFY(!a.writeData(QByteArrayLiteral("12345"), &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(a.errorString(), error);
        QCOMPARE(errors.size(), 1);
        QVERIFY(a.isOpen());
        QVERIFY(b.isOpen());
        QVERIFY(a.writeData(QByteArrayLiteral("tail")));
        QVERIFY(a.writeData(QByteArray()));
        QVERIFY(!a.writeData(QByteArrayLiteral("!")));
        QVERIFY(rxB.isEmpty());
        QTRY_COMPARE(received(rxB), prefix + QByteArrayLiteral("tail"));
        QCOMPARE(rxB.size(), 1);
        rxB.clear();
        QVERIFY(a.writeData(QByteArrayLiteral("recovered")));
        QVERIFY(a.errorString().isEmpty());
        QTRY_COMPARE(received(rxB), QByteArrayLiteral("recovered"));
    }

    void immediateRepliesDoNotRecurse()
    {
        VirtualSerialPair::instance()->setEnabled(true);
        SerialController a;
        SerialController b;
        QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
        QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
        int depth = 0;
        int maximumDepth = 0;
        int replies = 0;
        connect(&b, &SerialController::dataReceived, &b, [&](const QByteArray &data) {
            ++depth;
            maximumDepth = qMax(maximumDepth, depth);
            b.writeData(data);
            --depth;
        });
        connect(&a, &SerialController::dataReceived, &a, [&](const QByteArray &data) {
            ++depth;
            maximumDepth = qMax(maximumDepth, depth);
            if (++replies < 10) {
                a.writeData(data);
            }
            --depth;
        });
        QVERIFY(a.writeData(QByteArrayLiteral("ping")));
        QCOMPARE(replies, 0);
        QTRY_COMPARE(replies, 10);
        QCOMPARE(maximumDepth, 1);
    }

    void physicalPortRemainsConnectedAcrossVirtualPairToggle()
    {
#ifdef Q_OS_UNIX
        struct PseudoTerminal
        {
            int descriptor = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
            ~PseudoTerminal()
            {
                if (descriptor >= 0) {
                    ::close(descriptor);
                }
            }
        } terminal;
        QVERIFY(terminal.descriptor >= 0);
        QCOMPARE(grantpt(terminal.descriptor), 0);
        QCOMPARE(unlockpt(terminal.descriptor), 0);
        const char *portName = ptsname(terminal.descriptor);
        QVERIFY(portName);

        SerialController physical;
        QVERIFY2(physical.openPort(config(QString::fromLocal8Bit(portName))), qPrintable(physical.errorString()));
        const QString physicalPortName = physical.portName();
        QSignalSpy closed(&physical, &SerialController::closed);
        QSignalSpy incoming(&physical, &SerialController::dataReceived);
        SerialController a;
        SerialController b;
        auto *pair = VirtualSerialPair::instance();
        for (int stage = 0; stage < 3; ++stage) {
            if (stage == 1) {
                pair->setEnabled(true);
                QVERIFY(a.openPort(config(VirtualSerialPair::portAName())));
                QVERIFY(b.openPort(config(VirtualSerialPair::portBName())));
                QVERIFY(a.writeData(QByteArrayLiteral("virtual only")));
            } else if (stage == 2) {
                pair->setEnabled(false);
                QVERIFY(!a.isOpen());
                QVERIFY(!b.isOpen());
            }
            QVERIFY(physical.isOpen());
            QCOMPARE(physical.portName(), physicalPortName);
            QVERIFY(closed.isEmpty());

            incoming.clear();
            const QByteArray toPhysical = QByteArray::fromHex("00ff807f") + QByteArray::number(stage);
            QCOMPARE(::write(terminal.descriptor, toPhysical.constData(), toPhysical.size()), toPhysical.size());
            QTRY_COMPARE(received(incoming), toPhysical);

            const QByteArray toTerminal = QByteArray::number(stage) + QByteArray::fromHex("ff000d0a");
            QVERIFY(physical.writeData(toTerminal));
            QByteArray terminalReceived;
            const auto readTerminal = [&]() {
                char buffer[256];
                const auto count = ::read(terminal.descriptor, buffer, sizeof(buffer));
                if (count > 0) {
                    terminalReceived.append(buffer, count);
                }
                return terminalReceived;
            };
            QTRY_COMPARE(readTerminal(), toTerminal);
            QVERIFY(physical.isOpen());
            QCOMPARE(physical.portName(), physicalPortName);
            QVERIFY(closed.isEmpty());
        }
#else
        QSKIP("Pseudo-terminal serial integration is available on Unix platforms");
#endif
    }
};

QTEST_GUILESS_MAIN(VirtualSerialPairTest)

#include "tst_virtual_serial_pair.moc"
