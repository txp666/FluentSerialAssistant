#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>

#include <memory>

class QTextDecoder;

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

// Decodes a byte stream that arrives in arbitrary chunks. A multi-byte
// character split between chunks is completed by the next chunk instead of
// being decoded as replacement characters on both sides.
class StreamDecoder
{
  public:
    StreamDecoder();
    ~StreamDecoder();

    QString decode(const QByteArray &data, const QString &key);
    void reset();

  private:
    QString m_key;
    std::unique_ptr<QTextDecoder> m_decoder;
};

} // namespace AppTextEncoding
