#include "app/control/control_protocol.h"

#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QStandardPaths>
#include <QtNetwork/QLocalSocket>

#ifdef Q_OS_WIN
#include <windows.h>

#include <sddl.h>
#endif

namespace AppControl {

namespace {

#ifdef Q_OS_WIN
// The string form of the SID of the user the process runs as, or an empty string.
QString processUserSid(HANDLE process)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) {
        return {};
    }
    QString sid;
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    QByteArray buffer(static_cast<int>(size), '\0');
    if (size > 0 && GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
        LPWSTR text = nullptr;
        if (ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER *>(buffer.data())->User.Sid, &text)) {
            sid = QString::fromWCharArray(text);
            LocalFree(text);
        }
    }
    CloseHandle(token);
    return sid;
}
#endif

} // namespace

QString localServerName()
{
#ifdef Q_OS_UNIX
    // A bare name puts the socket in the temporary directory, which Linux shares
    // between users: another user could create it first and receive the
    // commands. The runtime directory is private to the user.
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return directory.isEmpty() ? QString() : QDir(directory).filePath(QStringLiteral("fluentserial-control-v1"));
#else
    // Pipe names are shared by all users of the machine. The user's SID keeps each user's
    // service apart; serverRunsAsCurrentUser() keeps a pipe another user created under this
    // name from receiving the commands.
    const QString sid = processUserSid(GetCurrentProcess());
    return sid.isEmpty() ? QString() : QStringLiteral("tech.zhangshu.FluentSerialAssistant.control.v1-") + sid;
#endif
}

bool serverRunsAsCurrentUser(const QLocalSocket &socket)
{
#ifdef Q_OS_WIN
    ULONG serverPid = 0;
    if (!GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &serverPid)) {
        return false;
    }
    HANDLE server = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, serverPid);
    if (!server) {
        return false;
    }
    const QString serverSid = processUserSid(server);
    CloseHandle(server);
    return !serverSid.isEmpty() && serverSid == processUserSid(GetCurrentProcess());
#else
    // The socket lives in a directory only this user can enter.
    Q_UNUSED(socket)
    return true;
#endif
}

QByteArray encodeMessage(const QJsonObject &message)
{
    QByteArray output = QJsonDocument(message).toJson(QJsonDocument::Compact);
    output.append('\n');
    return output;
}

QJsonObject successResponse(const QJsonValue &id, const QJsonObject &result)
{
    QJsonObject response;
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("result"), result);
    return response;
}

QJsonObject errorResponse(const QJsonValue &id, const QString &code, const QString &message)
{
    QJsonObject error;
    error.insert(QStringLiteral("code"), code);
    error.insert(QStringLiteral("message"), message);

    QJsonObject response;
    response.insert(QStringLiteral("id"), id);
    response.insert(QStringLiteral("ok"), false);
    response.insert(QStringLiteral("error"), error);
    return response;
}

} // namespace AppControl
