#include "app/core/macos_accessibility_fix.h"
#import <AppKit/AppKit.h>
#include <QtGui/QAccessible>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QTableWidget>
#import <objc/message.h>
#import <objc/runtime.h>

namespace {
id nativeQuery(id object, const char *selector)
{
    return reinterpret_cast<id (*)(id, SEL)>(objc_msgSend)(object, sel_registerName(selector));
}

id nativeElement(QAccessibleInterface *interface)
{
    return reinterpret_cast<id (*)(id, SEL, unsigned int)>(objc_msgSend)(
        reinterpret_cast<id>(objc_getClass("QMacAccessibilityElement")), sel_registerName("elementWithId:"),
        QAccessible::uniqueId(interface));
}
} // namespace

class MacAccessibilityTest : public QObject
{
    Q_OBJECT
  private slots:
    void selectedCellsSurviveNativeTeardown()
    {
        if (QString::fromLatin1(qVersion()) != QStringLiteral("6.12.0"))
            QSKIP("The runtime Qt version does not need this shim");
        QCOMPARE(QApplication::platformName(), QStringLiteral("cocoa"));
        QVERIFY(AppPlatform::installMacAccessibilityFix());
        QAccessible::setActive(true);
        QTableWidget table(2, 2);
        table.setSelectionBehavior(QAbstractItemView::SelectRows);
        table.setSelectionMode(QAbstractItemView::SingleSelection);
        const auto populate = [&table]() {
            for (int row = 0; row < table.rowCount(); ++row)
                for (int column = 0; column < 2; ++column)
                    if (!table.item(row, column))
                        table.setItem(row, column, new QTableWidgetItem(QString::number(row * 2 + column)));
        };
        populate();
        table.selectRow(0);
        auto *interface = QAccessible::queryAccessibleInterface(&table);
        QVERIFY(interface && interface->selectionInterface());
        id element = nativeElement(interface);
        QVERIFY(element);
        // Before the backport, this first native query deletes the table's
        // interfaces while iterating selected cells and deterministically crashes.
        NSArray *initial = nativeQuery(element, "accessibilitySelectedChildren");
        QCOMPARE([initial count], NSUInteger(2));
        // Exercise both native model growth and removal, draining each batch.
        for (int rowCount : {3, 2}) {
            @autoreleasepool {
                table.setRowCount(rowCount);
                populate();
                const int selectedRow = rowCount - 1;
                table.selectRow(selectedRow);
                NSArray *children = nativeQuery(element, "accessibilityChildren");
                QVERIFY([children count] > 0);
                for (id child in children) {
                    NSArray *cells = nativeQuery(child, "accessibilityChildren");
                    for (id cell in cells)
                        QVERIFY(nativeQuery(cell, "accessibilityTitle"));
                }
                NSArray *selected = nativeQuery(element, "accessibilitySelectedChildren");
                QCOMPARE([selected count], NSUInteger(2));
                for (NSUInteger column = 0; column < 2; ++column) {
                    id cell = selected[column];
                    NSString *title = nativeQuery(cell, "accessibilityTitle");
                    QCOMPARE(QString::fromNSString(title), QString::number(selectedRow * 2 + int(column)));
                    QVERIFY(nativeQuery(cell, "accessibilityParent"));
                }
            }
            // Pool draining must not delete the interfaces still owned by Qt.
            const auto selectedInterfaces = interface->selectionInterface()->selectedItems();
            QCOMPARE(selectedInterfaces.size(), 2);
            for (auto *cell : selectedInterfaces)
                QVERIFY(cell && cell->isValid());
        }
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    MacAccessibilityTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_macos_accessibility.moc"
