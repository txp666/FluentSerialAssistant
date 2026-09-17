#include "app/serial/virtual_serial_pair.h"

#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/serial/serial_controller.h"

#include <QtCore/QTimer>

#include <utility>

namespace {

constexpr qsizetype kMaximumPendingBytes = 1024 * 1024;

} // namespace

VirtualSerialPair::VirtualSerialPair()
{
    AppSettings settings;
    m_enabled = settings.value(QStringLiteral("serial/virtualPairEnabled"), false).toBool();
}

VirtualSerialPair *VirtualSerialPair::instance()
{
    static VirtualSerialPair pair;
    return &pair;
}

QString VirtualSerialPair::portAName() { return QStringLiteral("VIRTUAL-A"); }

QString VirtualSerialPair::portBName() { return QStringLiteral("VIRTUAL-B"); }

bool VirtualSerialPair::isVirtualPort(const QString &portName)
{
    return portName == portAName() || portName == portBName();
}

bool VirtualSerialPair::isEnabled() const { return m_enabled; }

void VirtualSerialPair::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    AppSettings settings;
    settings.setValue(QStringLiteral("serial/virtualPairEnabled"), enabled);
    settings.sync();

    if (!enabled) {
        clearPending();
        const auto owners = std::array{m_endpoints[0].owner, m_endpoints[1].owner};
        for (const QPointer<SerialController> &owner : owners) {
            // A closed() handler may destroy another session or open a physical
            // port on it. Only close controllers still owning virtual endpoints.
            if (owner && (m_endpoints[0].owner == owner || m_endpoints[1].owner == owner)) {
                owner->closePort();
            }
        }
    }
    emit enabledChanged(enabled);
}

bool VirtualSerialPair::acquire(const QString &portName, SerialController *controller, QString *error)
{
    if (!m_enabled) {
        *error = AppI18n::text("虚拟串口对未开启，请先在设置中开启");
        return false;
    }
    Endpoint &endpoint = m_endpoints[portName == portAName() ? 0 : 1];
    if (endpoint.owner) {
        *error = AppI18n::text("虚拟串口 %1 已被其他会话占用").arg(portName);
        return false;
    }
    endpoint.owner = controller;
    return true;
}

void VirtualSerialPair::clearPending()
{
    for (Endpoint &endpoint : m_endpoints) {
        endpoint.pending.clear();
        endpoint.deliveryScheduled = false;
        ++endpoint.generation;
    }
}

void VirtualSerialPair::release(SerialController *controller)
{
    for (Endpoint &endpoint : m_endpoints) {
        if (endpoint.owner == controller) {
            endpoint.owner.clear();
            // Also discard bytes queued by this sender at the other end. Neither
            // a reopened receiver nor a reopened sender inherits old traffic.
            clearPending();
            return;
        }
    }
}

bool VirtualSerialPair::write(SerialController *controller, const QByteArray &data, QString *error)
{
    int receiverIndex = -1;
    for (int index = 0; index < 2; ++index) {
        if (m_endpoints[index].owner == controller) {
            receiverIndex = 1 - index;
            break;
        }
    }
    if (!m_enabled || receiverIndex < 0) {
        *error = AppI18n::text("串口未连接");
        return false;
    }
    Endpoint &receiver = m_endpoints[receiverIndex];
    if (!receiver.owner || data.isEmpty()) {
        // A disconnected peer does not retain bytes for a future connection.
        return true;
    }
    if (data.size() > kMaximumPendingBytes - receiver.pending.size()) {
        *error = AppI18n::text("虚拟串口接收缓冲区已满，请降低发送速率");
        return false;
    }
    receiver.pending.append(data);
    if (!receiver.deliveryScheduled) {
        receiver.deliveryScheduled = true;
        const quint64 generation = receiver.generation;
        QTimer::singleShot(0, this, [this, receiverIndex, generation]() {
            Endpoint &endpoint = m_endpoints[receiverIndex];
            if (endpoint.generation != generation) {
                return;
            }
            endpoint.deliveryScheduled = false;
            const QByteArray data = std::exchange(endpoint.pending, {});
            if (m_enabled && endpoint.owner && !data.isEmpty()) {
                emit endpoint.owner->dataReceived(data);
            }
        });
    }
    return true;
}
