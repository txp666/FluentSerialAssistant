#pragma once

#include <QtCore/QString>

namespace AppUpdate {

// The caller must verify the package size and SHA-256 before calling this.
// Success means a separate installer has acknowledged the handoff and is
// waiting for this process to exit. Save session state before quitting.
bool launchUpdateInstaller(const QString &packagePath, QString *error = nullptr);

} // namespace AppUpdate
