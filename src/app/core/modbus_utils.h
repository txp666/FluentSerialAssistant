#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QString>

namespace AppModbus {

struct FunctionOption
{
    QString key;
    QString label;
    quint8 code = 0;
    bool write = false;
    bool multiple = false;
    bool coil = false;
};

struct RequestConfig
{
    quint8 slaveId = 1;
    QString functionKey;
    quint16 address = 0;
    quint16 quantity = 1;
    QString valuesText;
};

struct BuildResult
{
    bool ok = false;
    QByteArray frame;
    QString summary;
    QString errorMessage;
};

struct ParseResult
{
    bool recognized = false;
    bool crcValid = false;
    QString summary;
};

QList<FunctionOption> functionOptions();
QString defaultFunctionKey();
QString normalizedFunctionKey(const QString &key);
FunctionOption functionForKey(const QString &key);

BuildResult buildRequest(const RequestConfig &config);
ParseResult parseResponse(const QByteArray &frame);

} // namespace AppModbus
