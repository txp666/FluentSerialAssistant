#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace AppUpdate {

// The caller must verify the package size and SHA-256 before calling this; sha256 is the release's
// digest, which the installer checks again on its own copy before installing with system rights.
// Success means a separate installer has acknowledged the handoff and is waiting for this process
// to exit. Save session state before quitting.
bool launchUpdateInstaller(const QString &packagePath, const QByteArray &sha256, QString *error = nullptr);

} // namespace AppUpdate
