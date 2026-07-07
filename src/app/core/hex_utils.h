#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

struct HexParseResult
{
    bool ok = false;
    QByteArray bytes;
    qsizetype errorOffset = -1;
    QString errorMessage;
};

HexParseResult parseHexPayload(const QString &input);
QString bytesToHex(const QByteArray &data);
QString bytesToPrintableText(const QByteArray &data);
