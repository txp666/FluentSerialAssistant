#include "app/core/text_encoding.h"
#include "app/core/app_i18n.h"

#include <QtCore/QChar>
#include <QtCore5Compat/QTextCodec>

namespace {

const QList<AppTextEncoding::EncodingOption> &encodingOptions()
{
    static const QList<AppTextEncoding::EncodingOption> options = {
        {QStringLiteral("utf-8"), QStringLiteral("UTF-8"), QByteArrayLiteral("UTF-8")},
        {QStringLiteral("gbk"), QStringLiteral("GBK"), QByteArrayLiteral("GBK")},
        {QStringLiteral("ascii"), QStringLiteral("ASCII"), QByteArrayLiteral("US-ASCII")},
        {QStringLiteral("latin1"), QStringLiteral("Latin1"), QByteArrayLiteral("ISO-8859-1")},
    };
    return options;
}

const AppTextEncoding::EncodingOption &optionForKey(const QString &key)
{
    const QString normalized = AppTextEncoding::normalizedKey(key);
    for (const AppTextEncoding::EncodingOption &option : encodingOptions()) {
        if (option.key == normalized) {
            return option;
        }
    }
    return encodingOptions().first();
}

QTextCodec *codecForKey(const QString &key)
{
    const AppTextEncoding::EncodingOption &option = optionForKey(key);
    QTextCodec *codec = QTextCodec::codecForName(option.codecName);
    if (!codec && option.key == QStringLiteral("gbk")) {
        codec = QTextCodec::codecForName("GB18030");
    }
    if (!codec) {
        codec = QTextCodec::codecForName("UTF-8");
    }
    return codec;
}

QString encodeErrorMessage(const QString &key, qsizetype invalidChars)
{
    const QString label = AppTextEncoding::labelForKey(key);
    if (invalidChars > 0) {
        return AppI18n::text("文本包含 %1 个无法用 %2 编码的字符").arg(invalidChars).arg(label);
    }
    return AppI18n::text("文本无法用 %1 编码").arg(label);
}

} // namespace

namespace AppTextEncoding {

QList<EncodingOption> options() { return encodingOptions(); }

QString defaultKey() { return QStringLiteral("utf-8"); }

QString normalizedKey(const QString &key)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("utf8")) {
        return QStringLiteral("utf-8");
    }
    if (normalized == QStringLiteral("gb2312") || normalized == QStringLiteral("gb18030")) {
        return QStringLiteral("gbk");
    }
    if (normalized == QStringLiteral("us-ascii")) {
        return QStringLiteral("ascii");
    }
    if (normalized == QStringLiteral("latin-1") || normalized == QStringLiteral("iso-8859-1")) {
        return QStringLiteral("latin1");
    }

    for (const EncodingOption &option : encodingOptions()) {
        if (option.key == normalized) {
            return option.key;
        }
    }
    return defaultKey();
}

QString labelForKey(const QString &key) { return optionForKey(key).label; }

EncodeResult encode(const QString &text, const QString &key)
{
    EncodeResult result;
    QTextCodec *codec = codecForKey(key);
    if (!codec) {
        result.errorMessage = AppI18n::text("编码器不可用");
        return result;
    }

    if (!codec->canEncode(text)) {
        result.errorMessage = encodeErrorMessage(key, 0);
        return result;
    }

    QTextCodec::ConverterState state;
    result.bytes = codec->fromUnicode(text.constData(), static_cast<int>(text.size()), &state);
    result.invalidChars = state.invalidChars;
    result.ok = state.invalidChars == 0;
    if (!result.ok) {
        result.errorMessage = encodeErrorMessage(key, state.invalidChars);
    }
    return result;
}

QString decode(const QByteArray &data, const QString &key)
{
    QTextCodec *codec = codecForKey(key);
    if (!codec) {
        return QString::fromUtf8(data);
    }

    QTextCodec::ConverterState state;
    return codec->toUnicode(data.constData(), static_cast<int>(data.size()), &state);
}

QString toTerminalText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    for (QChar &ch : text) {
        if (ch == QLatin1Char('\n') || ch == QLatin1Char('\t')) {
            continue;
        }
        if (ch.unicode() < 0x20) {
            ch = QLatin1Char('.');
        }
    }
    while (text.endsWith(QLatin1Char('\n'))) {
        text.chop(1);
    }
    return text;
}

QString toSingleLineText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral(" "));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    for (QChar &ch : text) {
        if (ch == QLatin1Char('\t')) {
            continue;
        }
        if (ch.unicode() < 0x20) {
            ch = QLatin1Char('.');
        }
    }
    return text;
}

} // namespace AppTextEncoding
