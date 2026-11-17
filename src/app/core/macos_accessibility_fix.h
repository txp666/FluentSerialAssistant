#pragma once

#include <QtCore/QtGlobal>

namespace AppPlatform {
// Call on the GUI thread immediately after QApplication, before creating widgets.
#if defined(Q_OS_MACOS) && QT_VERSION == QT_VERSION_CHECK(6, 12, 0)
bool installMacAccessibilityFix();
#else
inline bool installMacAccessibilityFix() { return false; }
#endif
} // namespace AppPlatform
