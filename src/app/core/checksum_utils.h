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
QString normalizedAlgorithmKey(const QString &key);
QString labelForAlgorithm(const QString &key);
int widthForAlgorithm(const QString &key);
ByteOrder byteOrderFromKey(const QString &key);
QString byteOrderKey(ByteOrder byteOrder);
QString byteOrderLabel(ByteOrder byteOrder);

ChecksumResult calculate(const QByteArray &data, const QString &algorithmKey, ByteOrder byteOrder);

} // namespace AppChecksum
