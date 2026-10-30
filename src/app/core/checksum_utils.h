#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>

namespace AppChecksum {

enum class ByteOrder
{
    LittleEndian,
    BigEndian
};

struct AlgorithmOption
{
    QString key;
    QString label;
    int widthBytes = 1;
};

struct ChecksumResult
{
    bool ok = false;
    quint32 value = 0;
    QByteArray bytes;
    QString errorMessage;
};

QList<AlgorithmOption> options();
QString defaultAlgorithmKey();
// Unsupported keys normalize to the default algorithm; check them first where
// silently checking a different algorithm would be wrong.
QString normalizedAlgorithmKey(const QString &key);
bool isSupportedAlgorithm(const QString &key);
QString labelForAlgorithm(const QString &key);
int widthForAlgorithm(const QString &key);
ByteOrder byteOrderFromKey(const QString &key);
QString byteOrderKey(ByteOrder byteOrder);
QString byteOrderLabel(ByteOrder byteOrder);

ChecksumResult calculate(const QByteArray &data, const QString &algorithmKey, ByteOrder byteOrder);
// Calculates the checksum over data without its first skipLeading and last
// skipTrailing bytes. Fails when they leave no bytes to check.
ChecksumResult calculate(const QByteArray &data, const QString &algorithmKey, ByteOrder byteOrder, int skipLeading,
                         int skipTrailing);

} // namespace AppChecksum
