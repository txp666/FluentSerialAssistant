#include "app/core/macos_accessibility_fix.h"

#if defined(Q_OS_MACOS) && QT_VERSION == QT_VERSION_CHECK(6, 12, 0)
#import <AppKit/AppKit.h>
#include <QtCore/QCoreApplication>
#include <QtCore/QDebug>
#include <QtCore/QThread>
#include <cstdlib>
#include <cstring>
#import <objc/message.h>
#import <objc/runtime.h>

namespace {
// Qt 6.12.0 backport of QTBUG-149612:
// https://github.com/qt/qtbase/commit/8148c48deb8e4345bacb797cb75c011df6f797fa
// Native table placeholders share their table's axid. Their teardown must not
// delete QAccessibleInterfaces owned by Qt's cache and item views. Keep all AX
// querying/selection behavior intact. Remove this shim when upgrading to a Qt
// release containing the upstream fix; never enable it for an unverified ABI.
Class elementClass = Nil;
Ivar axidIvar = nullptr;
Ivar rowsIvar = nullptr;
Ivar columnsIvar = nullptr;
Ivar roleIvar = nullptr;

void invalidateElement(id element, SEL)
{
    auto *axid = reinterpret_cast<unsigned int *>(reinterpret_cast<char *>(element) + ivar_getOffset(axidIvar));
    *axid = 0;
    for (Ivar ivar : {rowsIvar, columnsIvar}) {
        id array = object_getIvar(element, ivar);
        [array autorelease];
        object_setIvar(element, ivar, nil);
    }
    object_setIvar(element, roleIvar, nil);
    NSAccessibilityPostNotification(element, NSAccessibilityUIElementDestroyedNotification);
}

void deallocateElement(id element, SEL selector)
{
    [object_getIvar(element, rowsIvar) release];
    [object_getIvar(element, columnsIvar) release];
    objc_super superCall{element, class_getSuperclass(elementClass)};
    reinterpret_cast<void (*)(objc_super *, SEL)>(objc_msgSendSuper)(&superCall, selector);
}

bool isVoidInstanceMethod(Method method)
{
    if (!method || method_getNumberOfArguments(method) != 2)
        return false;
    char *result = method_copyReturnType(method);
    char *self = method_copyArgumentType(method, 0);
    char *selector = method_copyArgumentType(method, 1);
    const bool valid = result && self && selector && std::strcmp(result, "v") == 0 && std::strcmp(self, "@") == 0 &&
                       std::strcmp(selector, ":") == 0;
    std::free(result);
    std::free(self);
    std::free(selector);
    return valid;
}

bool ownsMethod(Class owner, Method method)
{
    unsigned int count = 0;
    Method *methods = class_copyMethodList(owner, &count);
    bool found = false;
    for (unsigned int index = 0; index < count; ++index)
        found = found || methods[index] == method;
    std::free(methods);
    return found;
}

bool isObjectIvar(Ivar ivar) { return ivar && ivar_getTypeEncoding(ivar) && ivar_getTypeEncoding(ivar)[0] == '@'; }
} // namespace

bool AppPlatform::installMacAccessibilityFix()
{
    static bool installed = false;
    if (installed)
        return true;
    if (std::strcmp(qVersion(), "6.12.0") != 0)
        return false;
    if (!QCoreApplication::instance() || QThread::currentThread() != QCoreApplication::instance()->thread()) {
        qWarning("Qt macOS accessibility fix must be installed on the QApplication thread");
        return false;
    }
    elementClass = objc_getClass("QMacAccessibilityElement");
    // Offscreen and other non-Cocoa platforms have no native bridge to repair.
    if (!elementClass)
        return false;
    axidIvar = class_getInstanceVariable(elementClass, "axid");
    rowsIvar = class_getInstanceVariable(elementClass, "rows");
    columnsIvar = class_getInstanceVariable(elementClass, "columns");
    roleIvar = class_getInstanceVariable(elementClass, "synthesizedRole");
    const Method invalidate = class_getInstanceMethod(elementClass, sel_registerName("invalidate"));
    const Method deallocate = class_getInstanceMethod(elementClass, sel_registerName("dealloc"));
    if (class_getSuperclass(elementClass) != objc_getClass("NSObject") || !axidIvar ||
        std::strcmp(ivar_getTypeEncoding(axidIvar), @encode(unsigned int)) != 0 || !isObjectIvar(rowsIvar) ||
        !isObjectIvar(columnsIvar) || !isObjectIvar(roleIvar) || !isVoidInstanceMethod(invalidate) ||
        !isVoidInstanceMethod(deallocate) || !ownsMethod(elementClass, invalidate) ||
        !ownsMethod(elementClass, deallocate)) {
        qWarning("Qt macOS accessibility fix skipped: unexpected native bridge ABI");
        return false;
    }
    // Do not call the old implementations: they delete interfaces behind the
    // view's back. Both replacements preserve the upstream retain/release rules.
    method_setImplementation(invalidate, reinterpret_cast<IMP>(invalidateElement));
    method_setImplementation(deallocate, reinterpret_cast<IMP>(deallocateElement));
    installed = true;
    return true;
}
#endif
