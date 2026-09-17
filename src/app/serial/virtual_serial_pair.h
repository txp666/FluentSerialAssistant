#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <array>

class SerialController;

// An application-local null-modem pair. All operations run on the UI thread,
// like the SerialController instances that own its endpoints.
class VirtualSerialPair : public QObject
{
    Q_OBJECT

  public:
    static VirtualSerialPair *instance();
    static QString portAName();
    static QString portBName();
    static bool isVirtualPort(const QString &portName);

    bool isEnabled() const;
    void setEnabled(bool enabled);

  signals:
    // On disable, isEnabled() is already false when controllers emit closed();
    // enabledChanged(false) follows after both endpoints have been closed.
    void enabledChanged(bool enabled);

  private:
    friend class SerialController;

    struct Endpoint
    {
        QPointer<SerialController> owner;
        QByteArray pending;
        quint64 generation = 0;
        bool deliveryScheduled = false;
    };

    VirtualSerialPair();
    bool acquire(const QString &portName, SerialController *controller, QString *error);
    void release(SerialController *controller);
    bool write(SerialController *controller, const QByteArray &data, QString *error);
    void clearPending();

    bool m_enabled = false;
    std::array<Endpoint, 2> m_endpoints;
};
