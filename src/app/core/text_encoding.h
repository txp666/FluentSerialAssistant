#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>

namespace AppTextEncoding {

struct EncodingOption
{
    QString key;
    QString label;
    QByteArray codecName;
};

struct EncodeResult
{
    bool ok = false;
    QByteArray bytes;
    qsizetype invalidChars = 0;
    QString errorMessage;
};

QList<EncodingOption> options();
QString defaultKey();
QString normalizedKey(const QString &key);
QString labelForKey(const QString &key);

EncodeResult encode(const QString &text, const QString &key);
QString decode(const QByteArray &data, const QString &key);
QString toTerminalText(QString text);
QString toSingleLineText(QString text);

} // namespace AppTextEncoding
