#include "app/view/main_window.h"

#include <QtGui/QScreen>
#include <QtTest/QTest>

class MainWindowTest : public QObject
{
    Q_OBJECT

  private slots:
    // The offscreen screen is 800 pixels wide, narrower than the window's preferred minimum size.
    void sizeStaysWithinTheScreen()
    {
        MainWindow window;
        QScreen *screen = window.screen();
        QVERIFY(screen != nullptr);
        const QRect available = screen->availableGeometry();
        const QSize expectedMinimum = QSize(1040, 700).boundedTo(available.size());
        QCOMPARE(window.minimumSize(), expectedMinimum);
        QCOMPARE(window.size(), QSize(1120, 900).boundedTo(available.size()));
        QVERIFY(available.contains(window.geometry()));

        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(window.minimumSize(), expectedMinimum);
        QVERIFY(available.contains(window.geometry()));
    }

    void aShownWindowIsFittedIntoAScreenTooSmallForIt()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QScreen *screen = window.screen();
        QVERIFY(screen != nullptr);
        const QRect available = screen->availableGeometry();
        // As the window would be when it comes from a larger screen.
        window.setMinimumSize(QSize(0, 0));
        window.setGeometry(QRect(available.topLeft() + QPoint(200, 150), available.size() + QSize(400, 300)));
        QTRY_COMPARE(window.size(), available.size() + QSize(400, 300));
        emit screen->availableGeometryChanged(available);
        QTRY_VERIFY(available.contains(window.frameGeometry()));
        QCOMPARE(window.frameGeometry().size(), available.size());
    }
};

QTEST_MAIN(MainWindowTest)
#include "tst_main_window.moc"
