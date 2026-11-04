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
// The part of the release notes in the interface language, without a leading level-1 heading.
QString notesFor(const QString &notes, bool english);

} // namespace AppUpdate

Q_DECLARE_METATYPE(AppUpdate::ReleaseInfo)
