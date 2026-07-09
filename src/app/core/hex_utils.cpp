#include "app/core/hex_utils.h"
#include "app/core/app_i18n.h"

#include <QtCore/QChar>
#include <QtCore/QStringList>

namespace {

bool isSeparator(QChar ch)
{
    return ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(';') || ch == QLatin1Char('-');
}

bool isHexChar(QChar ch)
{
    return (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) || (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) ||
           (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
}

int hexValue(QChar ch)
{
    if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) {
        return ch.unicode() - QLatin1Char('0').unicode();
    }
    if (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) {
        return 10 + ch.unicode() - QLatin1Char('a').unicode();
    }
    return 10 + ch.unicode() - QLatin1Char('A').unicode();
}

HexParseResult fail(qsizetype offset, const QString &message)
{
    HexParseResult result;
    result.ok = false;
    result.errorOffset = offset;
    result.errorMessage = message;
    return result;
}

} // namespace

HexParseResult parseHexPayload(const QString &input)
{
    HexParseResult result;
    result.ok = true;

    qsizetype i = 0;
    const qsizetype size = input.size();
    while (i < size) {
        while (i < size && isSeparator(input.at(i))) {
            ++i;
        }
        if (i >= size) {
            break;
        }

        const qsizetype tokenStart = i;
        if (i + 1 < size && input.at(i) == QLatin1Char('0') &&
            (input.at(i + 1) == QLatin1Char('x') || input.at(i + 1) == QLatin1Char('X'))) {
            i += 2;
        }

        QString token;
        const qsizetype hexStart = i;
        while (i < size && !isSeparator(input.at(i))) {
            const QChar ch = input.at(i);
            if (!isHexChar(ch)) {
                return fail(i, AppI18n::text("包含非 HEX 字符"));
            }
            token.append(ch);
            ++i;
        }

        if (token.isEmpty()) {
            return fail(tokenStart, AppI18n::text("0x 后缺少字节"));
        }
        if (token.size() > 2 && token.size() % 2 != 0) {
            return fail(hexStart, AppI18n::text("连续 HEX 字符数量必须为偶数"));
        }

        if (token.size() <= 2) {
            int value = 0;
            for (const QChar ch : token) {
                value = (value << 4) | hexValue(ch);
            }
            result.bytes.append(static_cast<char>(value));
            continue;
        }

        for (qsizetype n = 0; n < token.size(); n += 2) {
            const int value = (hexValue(token.at(n)) << 4) | hexValue(token.at(n + 1));
            result.bytes.append(static_cast<char>(value));
        }
    }

    if (result.bytes.isEmpty() && !input.trimmed().isEmpty()) {
        return fail(0, AppI18n::text("没有解析到有效字节"));
    }

    return result;
}

QString bytesToHex(const QByteArray &data)
{
    QStringList bytes;
    bytes.reserve(data.size());
    for (unsigned char byte : data) {
        bytes.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')).toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}

QString bytesToPrintableText(const QByteArray &data)
{
    QString text = QString::fromUtf8(data);
    for (QChar &ch : text) {
        if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            ch = QLatin1Char(' ');
        } else if (ch.unicode() < 0x20 && ch != QLatin1Char('\t')) {
            ch = QLatin1Char('.');
        }
    }
    return text;
}
