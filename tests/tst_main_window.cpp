#include "app/core/app_settings.h"
#include "app/view/main_window.h"

#include <FluentQtWidgets/Config.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/Widgets/ScrollArea.h>
#include <FluentQtWidgets/Window/FluentWindow.h>

#include <QtCore/QDir>
#include <QtCore/QScopeGuard>
#include <QtGui/QImage>
#include <QtGui/QScreen>
#include <QtTest/QTest>

class MainWindowTest : public QObject
{
    Q_OBJECT

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentQt::FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
    }

    // As in the Gallery, the window's background (Mica on Windows 11) shows through the title bar
    // and, under the page layer's translucent white, between the cards. A transparent window colour
    // stands in for Mica, which the test platforms do not have.
    void windowBackgroundShowsBetweenTheCards()
    {
        MainWindow window;
        window.setCustomBackgroundColor(Qt::transparent, Qt::transparent);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        for (auto *area : window.findChildren<FluentQt::ScrollArea *>()) {
            QVERIFY2(!area->viewport()->autoFillBackground(), area->metaObject()->className());
            QVERIFY2(!area->widget() || !area->widget()->autoFillBackground(), area->metaObject()->className());
        }

        const QImage image = window.grab().toImage();
        const qreal ratio = image.devicePixelRatio();
        const auto alphaAt = [&image, ratio](const QPoint &point) {
            return image.pixelColor((QPointF(point) * ratio).toPoint()).alpha();
        };
        QCOMPARE(alphaAt(QPoint(window.width() / 2, 2)), 0);
        QWidget *page = nullptr;
        for (auto *view : window.findChildren<QWidget *>(QStringLiteral("appPageView"))) {
            if (view->isVisible()) {
                page = view;
            }
        }
        QVERIFY(page != nullptr);
        // The page's bottom margin, below its cards.
        const int margin = alphaAt(page->mapTo(&window, QPoint(page->width() / 2, page->height() - 4)));
        QVERIFY2(margin > 0 && margin < 255, qPrintable(QString::number(margin)));
    }

    // The window follows the saved Mica setting, which the settings page switches on Windows only.
    // Mica needs Windows 11; elsewhere the window stays without it and the switch is disabled.
    void micaFollowsTheSetting()
    {
        auto *config = FluentQt::FluentConfig::instance();
        const bool original = config->isMicaEnabled();
        const auto restore = qScopeGuard([config, original]() { config->setMicaEnabled(original); });
        const bool available = FluentQt::isMicaEffectAvailable();

        config->setMicaEnabled(true);
        MainWindow window;
        QCOMPARE(window.isMicaEffectEnabled(), available);
        config->setMicaEnabled(false);
        QVERIFY(!window.isMicaEffectEnabled());

        auto *card = window.findChild<FluentQt::SwitchSettingCard *>(QStringLiteral("micaEffectCard"));
#ifdef Q_OS_WIN
        QVERIFY(card);
        QCOMPARE(card->isEnabled(), available);
        QVERIFY(!card->isChecked());
        if (available) {
            card->setChecked(true);
            QVERIFY(config->isMicaEnabled());
            QVERIFY(window.isMicaEffectEnabled());
        }
#else
        QVERIFY(!card);
#endif
    }

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
