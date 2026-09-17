#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QUrl>

namespace AppUpdate {

struct ReleaseInfo
{
    QString version;
    QString notes;
    QUrl releaseUrl;
    QString assetName;
    QUrl downloadUrl;
    qint64 size = 0;
    QByteArray sha256;
};

QString currentPlatformKey();
bool parseRelease(const QByteArray &json, const QString &platformKey, const QString &currentVersion,
                  ReleaseInfo *release, bool *updateAvailable, QString *error);

} // namespace AppUpdate

Q_DECLARE_METATYPE(AppUpdate::ReleaseInfo)
