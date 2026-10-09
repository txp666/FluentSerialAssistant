#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtCore/QScopeGuard>
#include <QtGui/QHelpEvent>
#include <QtGui/QKeySequence>
#include <QtGui/QPixmap>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QTextLayout>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QToolTip>

class TerminalSearchTest : public QObject
{
    Q_OBJECT

    static QByteArray logLine(int number, bool match = false)
    {
        return QStringLiteral("record-%1: %2\n")
            .arg(number, 4, 10, QLatin1Char('0'))
            .arg(match ? QStringLiteral("needle") : QStringLiteral("ordinary log line"))
            .toUtf8();
    }

    static void preparePage(WorkbenchPage &page, bool autoScroll)
    {
        page.m_flushTimer.stop();
        page.m_statsTimer.stop();
        page.m_autoFrameBreakCheck->setChecked(false);
        page.m_timestampCheck->setChecked(false);
        page.m_autoScrollCheck->setChecked(autoScroll);
        page.m_terminalView->setLineWrapMode(QTextEdit::NoWrap);
        page.resize(1120, 900);
        page.show();
        QCoreApplication::processEvents();
    }

    static void prepareHistory(WorkbenchPage &page, bool autoScroll)
    {
        preparePage(page, autoScroll);
        for (int row = 0; row < 300; ++row) {
            page.handleReceivedData(logLine(row, row == 10 || row == 150 || row == 280));
        }
        page.flushPendingLines();
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QCoreApplication::processEvents();
    }

    static QString firstVisibleLine(WorkbenchPage &page)
    {
        return page.m_terminalView->cursorForPosition(QPoint(8, 8)).block().text();
    }

    static bool hasHighlight(QTextDocument *document, int position)
    {
        QTextCursor cursor(document);
        cursor.setPosition(position + 1);
        return cursor.charFormat().background().style() != Qt::NoBrush;
    }

    static bool validHighlightedMatches(WorkbenchPage &page)
    {
        auto *document = page.m_terminalView->document();
        int previousPosition = -1;
        for (const auto &match : page.m_terminalSearchMatches) {
            if (match.position <= previousPosition || match.length != 6 ||
                match.position + match.length >= document->characterCount()) {
                return false;
            }
            QTextCursor cursor(document);
            cursor.setPosition(match.position);
            cursor.setPosition(match.position + match.length, QTextCursor::KeepAnchor);
            if (cursor.selectedText() != QStringLiteral("needle") || !hasHighlight(document, match.position)) {
                return false;
            }
            previousPosition = match.position;
        }
        return true;
    }

    static bool captureSearchBar(WorkbenchPage &page)
    {
        const QString directory = qEnvironmentVariable("FLUENT_TERMINAL_SEARCH_CAPTURE_DIR");
        if (directory.isEmpty()) {
            return true;
        }
        if (!QDir().mkpath(directory)) {
            return false;
        }
        QCoreApplication::processEvents();
        const QString theme = FluentQt::ThemeManager::instance()->effectiveTheme() == FluentQt::Theme::Dark
                                  ? QStringLiteral("dark")
                                  : QStringLiteral("light");
        const QDir output(directory);
        return page.m_terminalSearchBar->grab().save(
                   output.filePath(QStringLiteral("terminal-search-%1.png").arg(theme))) &&
               page.grab().save(output.filePath(QStringLiteral("terminal-search-page-%1.png").arg(theme)));
    }

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentQt::FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
    }

    void init()
    {
        AppSettings settings;
        settings.clear();
        settings.setValue(QStringLiteral("terminal/maxRecords"), 1000);
        settings.sync();
        FluentQt::ThemeManager::instance()->setTheme(FluentQt::Theme::Light);
    }

    void terminalHeaderKeepsActionsVisibleAndRestoresStatistics_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<bool>("dark");
        QTest::newRow("chinese-light") << QStringLiteral("zh_CN") << false;
        QTest::newRow("english-light") << QStringLiteral("en_US") << false;
        QTest::newRow("chinese-dark") << QStringLiteral("zh_CN") << true;
        QTest::newRow("english-dark") << QStringLiteral("en_US") << true;
    }

    void terminalHeaderKeepsActionsVisibleAndRestoresStatistics()
    {
        QFETCH(QString, locale);
        QFETCH(bool, dark);
        const QString originalLocale = FluentQt::FluentConfig::instance()->localeName();
        const auto restoreLocale = qScopeGuard([originalLocale]() { AppI18n::applyLocale(originalLocale); });
        AppI18n::applyLocale(locale);
        FluentQt::ThemeManager::instance()->setTheme(dark ? FluentQt::Theme::Dark : FluentQt::Theme::Light);
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        auto *statistics = page.m_terminalStatsWidget;
        auto *actions = page.findChild<QWidget *>(QStringLiteral("terminalHeaderActions"));
        QVERIFY(statistics);
        QVERIFY(actions);
        QCOMPARE(page.m_terminalStatsLabels.size(), 10);
        QCOMPARE(page.m_terminalStatsGroups.size(), 3);
        QCOMPARE(statistics->height(), 38);
        QCOMPARE(page.m_terminalStatsLabels.at(2)->text(),
                 locale == QStringLiteral("en_US") ? QStringLiteral("Total") : QStringLiteral("累计"));
        const auto buttons = actions->findChildren<FluentQt::ToolButton *>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(buttons.size(), 7);
        auto *clearButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalClearButton"));
        QVERIFY(clearButton);
        QWidget *header = actions->parentWidget();
        QCOMPARE(statistics->parentWidget(), header);
        QCOMPARE(clearButton->parentWidget(), header);
        QCOMPARE(header->layout()->itemAt(0)->widget(), clearButton);
        const QString connectedText = AppI18n::text("已连接");
        QCOMPARE(connectedText,
                 locale == QStringLiteral("en_US") ? QStringLiteral("Connected") : QStringLiteral("已连接"));
        const QString disconnectedText = AppI18n::text("未连接");
        const QString connectionText = QStringLiteral("123:45:56");
        const QString regularConnectionText = QStringLiteral("03:41");

        const auto verifyVisibleHeader = [&]() {
            const QRect clearBounds(clearButton->mapTo(page.viewport(), QPoint()), clearButton->size());
            const QRect statisticsBounds(statistics->mapTo(page.viewport(), QPoint()), statistics->size());
            const QRect actionBounds(actions->mapTo(page.viewport(), QPoint()), actions->size());
            QVERIFY(clearButton->isVisible());
            QCOMPARE(clearButton->size(), QSize(32, 32));
            QVERIFY(page.viewport()->rect().contains(clearBounds));
            QVERIFY(page.viewport()->rect().contains(statisticsBounds));
            QVERIFY(page.viewport()->rect().contains(actionBounds));
            QVERIFY(clearBounds.right() < statisticsBounds.left());
            QVERIFY(statisticsBounds.right() < actionBounds.left());
            QList<QRect> buttonBounds;
            for (auto *button : buttons) {
                QVERIFY(button->isVisible());
                QCOMPARE(button->size(), QSize(32, 32));
                const QRect bounds(button->mapTo(page.viewport(), QPoint()), button->size());
                QVERIFY(page.viewport()->rect().contains(bounds));
                QVERIFY(actionBounds.contains(bounds));
                for (const QRect &other : buttonBounds) {
                    QVERIFY(!bounds.intersects(other));
                }
                buttonBounds.append(bounds);
            }
        };
        const auto verifyStatisticsBounds = [&]() {
            QList<QRect> groupBounds;
            for (auto *group : page.m_terminalStatsGroups) {
                QCOMPARE(group->parentWidget(), statistics);
                if (group->width() == 0 || !group->isVisible()) {
                    continue;
                }
                const QRect bounds(group->mapTo(statistics, QPoint()), group->size());
                QVERIFY(statistics->rect().contains(bounds));
                for (const QRect &other : groupBounds) {
                    QVERIFY(!bounds.intersects(other));
                }
                groupBounds.append(bounds);
            }
            QList<QRect> labelBounds;
            for (auto *label : page.m_terminalStatsLabels) {
                QVERIFY(page.m_terminalStatsGroups.contains(label->parentWidget()));
                if (label->width() == 0 || !label->isVisible()) {
                    continue;
                }
                const QRect bounds(label->mapTo(statistics, QPoint()), label->size());
                QVERIFY(statistics->rect().contains(bounds));
                QVERIFY(label->parentWidget()->rect().contains(label->geometry()));
                for (const QRect &other : labelBounds) {
                    QVERIFY(!bounds.intersects(other));
                }
                labelBounds.append(bounds);
            }
        };
        struct HeaderPresentation
        {
            QList<QRect> groupBounds;
            QList<bool> groupVisibility;
            QList<QRect> labelBounds;
            QList<QFont> labelFonts;
            QList<bool> labelVisibility;
            QRect statisticsBounds;
            QRect actionBounds;
        };
        const auto capturePresentation = [&]() {
            HeaderPresentation presentation;
            for (auto *group : page.m_terminalStatsGroups) {
                presentation.groupBounds.append(group->geometry());
                presentation.groupVisibility.append(group->isVisible());
            }
            for (auto *label : page.m_terminalStatsLabels) {
                presentation.labelBounds.append(label->geometry());
                presentation.labelFonts.append(label->font());
                presentation.labelVisibility.append(label->isVisible());
            }
            presentation.statisticsBounds = statistics->geometry();
            presentation.actionBounds = actions->geometry();
            return presentation;
        };
        const auto verifyStablePresentation = [&](const HeaderPresentation &expected) {
            for (int index = 0; index < page.m_terminalStatsGroups.size(); ++index) {
                auto *group = page.m_terminalStatsGroups.at(index);
                QCOMPARE(group->geometry(), expected.groupBounds.at(index));
                QCOMPARE(group->isVisible(), expected.groupVisibility.at(index));
            }
            for (int index = 0; index < page.m_terminalStatsLabels.size(); ++index) {
                auto *label = page.m_terminalStatsLabels.at(index);
                QCOMPARE(label->geometry(), expected.labelBounds.at(index));
                QCOMPARE(label->font(), expected.labelFonts.at(index));
                QCOMPARE(label->isVisible(), expected.labelVisibility.at(index));
            }
            QCOMPARE(statistics->geometry(), expected.statisticsBounds);
            QCOMPARE(actions->geometry(), expected.actionBounds);
        };
        const auto restoreUsualStatistics = [&]() {
            page.m_rxCount = 7500;
            page.m_txCount = 0;
            page.m_rxRateLabel->setText(QStringLiteral("40 B/s"));
            page.m_txRateLabel->setText(QStringLiteral("0 B/s"));
            page.m_connectionStatusLabel->setText(connectedText);
            page.m_connectionTimeLabel->setText(regularConnectionText);
            page.updateCounters();
            QCoreApplication::processEvents();
        };
        struct StatisticsValues
        {
            qint64 rxCount;
            qint64 txCount;
            QString rxRate;
            QString txRate;
            QString connectionStatus;
            QString connectionTime;
        };
        const QList<StatisticsValues> statisticsChanges{
            {0, 0, QStringLiteral("0 B/s"), QStringLiteral("0 B/s"), disconnectedText, QStringLiteral("—")},
            {9, 999, QStringLiteral("1 B/s"), QStringLiteral("999 B/s"), connectedText, QStringLiteral("00:09")},
            {1536, 1023, QStringLiteral("1.5 KB/s"), QStringLiteral("999 B/s"), connectedText, QStringLiteral("09:59")},
            {1024 * 1024 - 1, 1024, QStringLiteral("999.9 KB/s"), QStringLiteral("1 KB/s"), connectedText,
             QStringLiteral("59:59")},
            {1024 * 1024, 15 * 1024 * 1024, QStringLiteral("1 MB/s"), QStringLiteral("15.5 MB/s"), connectedText,
             QStringLiteral("01:00:00")},
            {0, 0, QStringLiteral("0 B/s"), QStringLiteral("0 B/s"), disconnectedText, QStringLiteral("—")},
            {Q_INT64_C(987654) * 1024 * 1024, Q_INT64_C(123456) * 1024 * 1024, QStringLiteral("987.6 MB/s"),
             QStringLiteral("123.4 MB/s"), connectedText, connectionText},
        };
        const auto verifyChangingStatistics = [&]() {
            const HeaderPresentation baseline = capturePresentation();
            for (const auto &values : statisticsChanges) {
                page.m_rxCount = values.rxCount;
                page.m_txCount = values.txCount;
                page.m_rxRateLabel->setText(values.rxRate);
                page.m_txRateLabel->setText(values.txRate);
                page.m_connectionStatusLabel->setText(values.connectionStatus);
                page.m_connectionTimeLabel->setText(values.connectionTime);
                page.updateCounters();
                QCoreApplication::processEvents();
                verifyStablePresentation(baseline);
                verifyStatisticsBounds();
                QCOMPARE(page.m_connectionStatusLabel->text(), values.connectionStatus);
                QCOMPARE(page.m_connectionTimeLabel->text(), values.connectionTime);
            }
        };

        const QString directory = qEnvironmentVariable("FLUENT_TERMINAL_SEARCH_CAPTURE_DIR");
        if (!directory.isEmpty()) {
            QVERIFY(QDir().mkpath(directory));
        }
        for (int width : {1440, 1120, 1040, 980, 1440}) {
            page.resize(width, 900);
            QCoreApplication::processEvents();
            restoreUsualStatistics();
            verifyVisibleHeader();
            verifyStatisticsBounds();
            if (!directory.isEmpty()) {
                QVERIFY(header->grab().save(QDir(directory).filePath(
                    QStringLiteral("terminal-header-%1-%2-%3.png")
                        .arg(width)
                        .arg(locale, dark ? QStringLiteral("dark") : QStringLiteral("light")))));
            }
            // Live values must never move the slots or change their presentation.
            verifyChangingStatistics();
            verifyVisibleHeader();
            QCOMPARE(page.m_connectionTimeLabel->text(), connectionText);
            if (!directory.isEmpty()) {
                QVERIFY(header->grab().save(QDir(directory).filePath(
                    QStringLiteral("terminal-header-%1-%2-%3-large.png")
                        .arg(width)
                        .arg(locale, dark ? QStringLiteral("dark") : QStringLiteral("light")))));
            }
        }
        restoreUsualStatistics();
        QCOMPARE(page.m_rxRateLabel->font().pixelSize(), 14);
        QCOMPARE(page.m_txRateLabel->font().pixelSize(), 14);
        QCOMPARE(page.m_rxCounterLabel->font().pixelSize(), 11);
        QCOMPARE(page.m_txCounterLabel->font().pixelSize(), 11);
        QCOMPARE(page.m_connectionTimeLabel->font().pixelSize(), 11);
        QVERIFY(statistics->toolTip().isEmpty());

        // Exercise the final elision fallback independently of platform window minima.
        for (int width : {180, 260, 340, 420, 900}) {
            statistics->resize(width, statistics->height());
            page.updateTerminalHeaderLayout();
            QCoreApplication::processEvents();
            verifyChangingStatistics();
            verifyStatisticsBounds();
            QCOMPARE(page.m_connectionTimeLabel->text(), connectionText);
            QVERIFY(statistics->toolTip().isEmpty());
            for (auto *group : page.m_terminalStatsGroups) {
                QVERIFY(group->toolTip().isEmpty());
            }
        }
        restoreUsualStatistics();
        QCOMPARE(page.m_rxRateLabel->font().pixelSize(), 14);
        QCOMPARE(page.m_txRateLabel->font().pixelSize(), 14);
        QCOMPARE(page.m_rxCounterLabel->font().pixelSize(), 11);
        QCOMPARE(page.m_txCounterLabel->font().pixelSize(), 11);
        QCOMPARE(page.m_connectionTimeLabel->font().pixelSize(), 11);
        for (auto *label : page.m_terminalStatsLabels) {
            QVERIFY(label->width() >= label->fontMetrics().horizontalAdvance(label->text()));
        }
        QVERIFY(statistics->toolTip().isEmpty());
    }

    void searchBarStaysOpenDuringTerminalAndDropdownInteraction()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        QPointer<QWidget> searchBar = page.m_terminalSearchBar;
        QVERIFY(searchBar);
        QVERIFY(!searchBar->isVisible());
        QCOMPARE(searchBar->objectName(), QStringLiteral("terminalSearchBar"));
        QCOMPARE(searchBar->parentWidget(), page.m_terminalView->viewport());
        QVERIFY(!searchBar->isWindow());
        const QSize viewportSize = page.m_terminalView->viewport()->size();
        const int readingPosition = page.m_terminalView->verticalScrollBar()->value();
        searchButton->click();
        QTRY_VERIFY(searchBar->isVisible());
        QCOMPARE(page.m_terminalView->viewport()->size(), viewportSize);
        QCOMPARE(page.m_terminalView->verticalScrollBar()->value(), readingPosition);

        QTest::mouseClick(page.m_terminalView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCoreApplication::processEvents();
        QVERIFY(searchBar && searchBar->isVisible());
        QTest::mouseClick(page.m_sendEdit->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCoreApplication::processEvents();
        QVERIFY(searchBar && searchBar->isVisible());

        QTest::mouseClick(page.m_terminalFilterCombo, Qt::LeftButton);
        QTRY_VERIFY(page.m_terminalFilterCombo->dropMenu());
        QPointer<FluentQt::ComboBoxMenu> menu = page.m_terminalFilterCombo->dropMenu();
        QTRY_VERIFY(menu && menu->isVisible());
        QVERIFY(searchBar && searchBar->isVisible());
        const int receiveIndex = page.m_terminalFilterCombo->findData(QStringLiteral("rx"));
        QVERIFY(receiveIndex >= 0);
        auto *item = menu->view()->item(receiveIndex);
        QVERIFY(item);
        const QRect itemRect = menu->view()->visualItemRect(item);
        QVERIFY(!itemRect.isEmpty());
        QTest::mouseClick(menu->view()->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());
        QTRY_COMPARE(page.m_terminalFilterCombo->currentData().toString(), QStringLiteral("rx"));
        QTRY_VERIFY(!menu || !menu->isVisible());
        QVERIFY(searchBar && searchBar->isVisible());
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
    }

    void searchBarCannotBeDraggedAndReopensWithTheSameQuery()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        QPointer<QWidget> searchBar = page.m_terminalSearchBar;
        QTRY_VERIFY(searchBar->isVisible());
        QVERIFY(!searchBar->testAttribute(Qt::WA_DeleteOnClose));
        const auto anchorOffset = [&page, searchBar]() {
            return QPoint(page.m_terminalView->viewport()->width() - searchBar->x() - searchBar->width(),
                          searchBar->y());
        };
        const QPoint originalOffset = anchorOffset();
        QTest::mousePress(searchBar, Qt::LeftButton, Qt::NoModifier, QPoint(3, 3));
        QTest::mouseMove(searchBar, QPoint(40, 26));
        QTest::mouseRelease(searchBar, Qt::LeftButton, Qt::NoModifier, QPoint(40, 26));
        QCoreApplication::processEvents();
        // Hover can hide the native scrollbar and widen the viewport. The bar
        // must remain anchored to its edge rather than to an absolute X value.
        QCOMPARE(anchorOffset(), originalOffset);
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);

        auto *closeButton = searchBar->findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchClose"));
        QVERIFY(closeButton);
        const QSize viewportSize = page.m_terminalView->viewport()->size();
        const int readingPosition = page.m_terminalView->verticalScrollBar()->value();
        closeButton->click();
        QCoreApplication::processEvents();
        QVERIFY(searchBar);
        QVERIFY(!searchBar->isVisible());
        QCOMPARE(page.m_terminalView->viewport()->size(), viewportSize);
        QCOMPARE(page.m_terminalView->verticalScrollBar()->value(), readingPosition);
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        searchButton->click();
        QTRY_VERIFY(searchBar->isVisible());
        QCOMPARE(page.m_terminalSearchBar, searchBar.data());
        QCOMPARE(anchorOffset(), originalOffset);
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
    }

    void searchBarFollowsTheViewportTopRightWhenResized()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        auto *searchBar = page.m_terminalSearchBar;
        QTRY_VERIFY(searchBar->isVisible());

        const QList<QSize> sizes{QSize(1440, 900), QSize(1120, 900), QSize(980, 900)};
        for (const QSize &size : sizes) {
            page.resize(size);
            QCoreApplication::processEvents();
            const QRect viewport = page.m_terminalView->viewport()->rect();
            QVERIFY(viewport.contains(searchBar->geometry()));
            QCOMPARE(searchBar->y(), 8);
            QCOMPARE(searchBar->geometry().right(), viewport.right() - 8);
            QCOMPARE(searchBar->height(), 44);
            QVERIFY(searchBar->width() <= 560);
            QVERIFY(searchBar->width() <= viewport.width() - 16);
        }
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
    }

    void fragmentedReceiveLineReflowsExistingTextWhenResized()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        page.m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
        const QByteArray payload = QByteArrayLiteral("[66085144][DATA] ms=66085144,status=49,") +
                                   QByteArrayLiteral("raw=-839382,avg=-839378,").repeated(5) +
                                   QByteArrayLiteral("needle,nV=-1172598,milli_ue=-195433,log_drop=0,GFx10000=20000");
        // Serial readyRead notifications may split an application line anywhere,
        // including leaving its final digit and CR/LF in a separate notification.
        page.handleReceivedData(payload.left(64));
        page.flushPendingLines();
        page.handleReceivedData(payload.mid(64, payload.size() - 65));
        page.flushPendingLines();
        page.handleReceivedData(payload.right(1) + QByteArrayLiteral("\r\n"));
        page.flushPendingLines();
        QCoreApplication::processEvents();

        auto *document = page.m_terminalView->document();
        const QString expected = QStringLiteral("« ") + QString::fromUtf8(payload);
        QCOMPARE(page.m_terminalView->toPlainText(), expected);
        QCOMPARE(document->blockCount(), 1);
        const int narrowLines = document->firstBlock().layout()->lineCount();
        QVERIFY(narrowLines > 1);
        const QString captureDirectory = qEnvironmentVariable("FLUENT_TERMINAL_SEARCH_CAPTURE_DIR");
        if (!captureDirectory.isEmpty()) {
            QVERIFY(QDir().mkpath(captureDirectory));
            QVERIFY(page.grab().save(QDir(captureDirectory).filePath(QStringLiteral("terminal-wrap-narrow.png"))));
        }
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 1);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        const int matchPosition = page.m_terminalSearchMatches.first().position;
        const int selectionAnchor = page.m_terminalView->textCursor().anchor();
        const int selectionPosition = page.m_terminalView->textCursor().position();

        QSignalSpy changes(document, &QTextDocument::contentsChange);
        page.resize(1920, 1050);
        QTRY_VERIFY(document->firstBlock().layout()->lineCount() < narrowLines);
        const int wideLines = document->firstBlock().layout()->lineCount();
        if (!captureDirectory.isEmpty()) {
            QVERIFY(page.grab().save(QDir(captureDirectory).filePath(QStringLiteral("terminal-wrap-wide.png"))));
        }
        QCOMPARE(page.m_terminalView->toPlainText(), expected);
        QCOMPARE(document->blockCount(), 1);
        QCOMPARE(page.m_terminalSearchMatches.size(), 1);
        QCOMPARE(page.m_terminalSearchMatches.first().position, matchPosition);
        QCOMPARE(page.m_terminalView->textCursor().anchor(), selectionAnchor);
        QCOMPARE(page.m_terminalView->textCursor().position(), selectionPosition);
        QVERIFY(hasHighlight(document, matchPosition));
        QVERIFY(changes.isEmpty());

        page.resize(1120, 900);
        QTRY_VERIFY(document->firstBlock().layout()->lineCount() > wideLines);
        QCOMPARE(page.m_terminalView->toPlainText(), expected);
        QCOMPARE(page.m_terminalSearchMatches.first().position, matchPosition);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QVERIFY(changes.isEmpty());
    }

    void receiveFragmentsPreserveRealLineEndings()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        page.handleReceivedData(QByteArrayLiteral("first nee"));
        page.flushPendingLines();
        page.handleReceivedData(QByteArrayLiteral("dle\r"));
        page.flushPendingLines();
        page.handleReceivedData(QByteArrayLiteral("\nsecond needle\nthird"));
        page.flushPendingLines();
        page.handleReceivedData(QByteArrayLiteral(" line\r\n"));
        page.flushPendingLines();

        const QStringList lines = page.m_terminalView->toPlainText().split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 3);
        // Markers may introduce logical lines, but transport notifications must
        // neither add newlines nor consume actual device line boundaries.
        QVERIFY(lines.at(0).endsWith(QStringLiteral("first needle")));
        QVERIFY(lines.at(1).endsWith(QStringLiteral("second needle")));
        QVERIFY(lines.at(2).endsWith(QStringLiteral("third line")));
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        QVERIFY(validHighlightedMatches(page));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QVERIFY(page.m_terminalView->textCursor().block().text().endsWith(QStringLiteral("second needle")));
    }

    void activeSearchMatchesAcrossIncrementallyFlushedReceiveFragments()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        page.handleReceivedData(QByteArrayLiteral("prefix nee"));
        page.flushPendingLines();
        QVERIFY(page.m_terminalSearchMatches.isEmpty());
        page.handleReceivedData(QByteArrayLiteral("dle suffix\r\n"));
        page.flushPendingLines();

        QCOMPARE(page.m_terminalView->toPlainText(), QStringLiteral("« prefix needle suffix"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 1);
        QVERIFY(validHighlightedMatches(page));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        const int matchPosition = page.m_terminalSearchMatches.first().position;
        for (int offset = 0; offset < 6; ++offset) {
            QVERIFY(hasHighlight(page.m_terminalView->document(), matchPosition + offset));
        }
    }

    void emptyReceiveLineEndingsKeepRecordMappingBounded()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        for (int batch = 0; batch < 15; ++batch) {
            for (int row = 0; row < 100; ++row) {
                page.handleReceivedData(QByteArrayLiteral("\r\n"));
            }
            page.flushPendingLines();
            QVERIFY(page.m_terminalRecordRanges.size() <= page.m_records.size());
            for (auto it = page.m_terminalRecordRanges.cbegin(); it != page.m_terminalRecordRanges.cend(); ++it) {
                QVERIFY(it.key() >= page.m_firstRecordIndex);
            }
        }
        QCOMPARE(page.m_records.size(), 1000);
        QCOMPARE(page.m_firstRecordIndex, 500);
        QVERIFY(page.m_terminalView->toPlainText().isEmpty());

        page.handleReceivedData(QByteArrayLiteral("needle\r\n"));
        page.flushPendingLines();
        QVERIFY(page.m_terminalRecordRanges.size() <= page.m_records.size());
        QVERIFY(page.m_terminalView->toPlainText().endsWith(QStringLiteral("needle")));
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 1);
        QVERIFY(validHighlightedMatches(page));
    }

    void continuousReceiveFragmentsTrimExpiredTextAndKeepSearching()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QString retainedText;
        for (int batch = 0; batch < 15; ++batch) {
            for (int row = batch * 100; row < (batch + 1) * 100; ++row) {
                const QString part = QStringLiteral("part-%1;").arg(row, 4, 10, QLatin1Char('0'));
                page.handleReceivedData(part.toUtf8());
                if (row >= 500) {
                    retainedText += part;
                }
            }
            page.flushPendingLines();
            QVERIFY(page.m_terminalRecordRanges.size() <= page.m_records.size());
        }
        QCOMPARE(page.m_records.size(), 1000);
        QCOMPARE(page.m_firstRecordIndex, 500);
        QCOMPARE(page.m_terminalView->document()->blockCount(), 1);
        const QString text = page.m_terminalView->toPlainText();
        QVERIFY(text.endsWith(retainedText));
        QVERIFY(text.size() <= retainedText.size() + 2);
        QVERIFY(!text.contains(QStringLiteral("part-0499;")));
        QVERIFY(page.m_terminalSearchMatches.isEmpty());

        page.handleReceivedData(QByteArrayLiteral("nee"));
        page.flushPendingLines();
        QVERIFY(page.m_terminalSearchMatches.isEmpty());
        page.handleReceivedData(QByteArrayLiteral("dle\n"));
        page.flushPendingLines();
        QCOMPARE(page.m_records.size(), 1000);
        QCOMPARE(page.m_firstRecordIndex, 502);
        QVERIFY(!page.m_terminalView->toPlainText().contains(QStringLiteral("part-0501;")));
        QVERIFY(page.m_terminalView->toPlainText().endsWith(QStringLiteral("part-1499;needle")));
        QCOMPARE(page.m_terminalSearchMatches.size(), 1);
        QVERIFY(validHighlightedMatches(page));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
    }

    void resizingWrappedHistoryFollowsTailWithAutoScrollOn()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, true);
        page.m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
        const QByteArray payload = QByteArrayLiteral(" telemetry value=12345,").repeated(12);
        for (int row = 0; row < 80; ++row) {
            page.handleReceivedData(logLine(row).chopped(1) + payload + QByteArrayLiteral("\r\n"));
        }
        page.flushPendingLines();
        QCoreApplication::processEvents();

        auto *document = page.m_terminalView->document();
        auto *scroll = page.m_terminalView->verticalScrollBar();
        const QString expected = page.m_terminalView->toPlainText();
        const int narrowMaximum = scroll->maximum();
        QVERIFY(narrowMaximum > 0);
        QCOMPARE(scroll->value(), narrowMaximum);
        page.resize(1920, 1050);
        QTRY_VERIFY(scroll->maximum() < narrowMaximum);
        QTRY_COMPARE(scroll->value(), scroll->maximum());
        QCOMPARE(page.m_terminalView->toPlainText(), expected);
        QCOMPARE(document->blockCount(), 80);

        const int wideMaximum = scroll->maximum();
        page.resize(1120, 900);
        QTRY_VERIFY(scroll->maximum() > wideMaximum);
        QTRY_COMPARE(scroll->value(), scroll->maximum());
        QCOMPARE(page.m_terminalView->toPlainText(), expected);
        QCOMPARE(page.m_terminalView->textCursor().position(), document->characterCount() - 1);
    }

    void searchBarControlsAndEmbeddedOptionsAreVerticallyCentered_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void searchBarControlsAndEmbeddedOptionsAreVerticallyCentered()
    {
        QFETCH(bool, dark);
        FluentQt::ThemeManager::instance()->setTheme(dark ? FluentQt::Theme::Dark : FluentQt::Theme::Light);
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        auto *searchBar = page.m_terminalSearchBar;
        QTRY_VERIFY(searchBar->isVisible());
        page.activateWindow();
        page.m_terminalSearchEdit->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(captureSearchBar(page));

        const auto centerY = [searchBar](QWidget *widget) {
            return widget->mapTo(searchBar, widget->rect().center()).y();
        };
        auto *closeButton = searchBar->findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchClose"));
        QVERIFY(closeButton);
        const QList<QWidget *> searchRow{page.m_terminalSearchEdit,       page.m_terminalSearchPrevButton,
                                         page.m_terminalSearchNextButton, page.m_terminalSearchCaseCheck,
                                         page.m_terminalSearchRegexCheck, page.m_terminalFilterCombo,
                                         page.m_terminalSummaryLabel,     closeButton};
        const int expectedCenter = centerY(searchRow.first());
        for (auto *widget : searchRow) {
            QVERIFY(widget->isVisible());
            QVERIFY2(qAbs(centerY(widget) - expectedCenter) <= 1,
                     qPrintable(QStringLiteral("%1 center Y=%2, row center Y=%3")
                                    .arg(widget->objectName())
                                    .arg(centerY(widget))
                                    .arg(expectedCenter)));
            const QRect bounds(widget->mapTo(searchBar, QPoint()), widget->size());
            QVERIFY2(searchBar->rect().contains(bounds), qPrintable(widget->objectName()));
        }

        QVERIFY(!page.m_terminalSearchEdit->clearButton()->isVisible());
        for (QWidget *button : {page.m_terminalSearchCaseCheck, page.m_terminalSearchRegexCheck}) {
            const QRect bounds(button->mapTo(page.m_terminalSearchEdit, QPoint()), button->size());
            QVERIFY(page.m_terminalSearchEdit->rect().contains(bounds));
        }
    }

    void headerClearResetsTerminalAndTrafficBeforeReceivingAgain()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        auto *clearButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalClearButton"));
        QVERIFY(clearButton);
        page.handleReceivedData(QByteArrayLiteral("needle 12"));
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArrayLiteral("sent"));
        page.flushPendingLines();
        page.updateRateStats();
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QVERIFY(!page.m_terminalView->toPlainText().isEmpty());
        QVERIFY(!page.m_terminalSearchMatches.isEmpty());
        QVERIFY(page.m_lastStatsRxCount > 0);
        QVERIFY(page.m_lastStatsTxCount > 0);
        const int retainedRecords = page.m_records.size();

        // Pending terminal output and a partial frame must not reappear after clearing.
        page.handleReceivedData(QByteArrayLiteral("pending"));
        page.m_rxFrameBuffer = QByteArrayLiteral("partial frame");
        clearButton->click();
        QVERIFY(page.m_terminalView->toPlainText().isEmpty());
        QCOMPARE(page.m_records.size(), retainedRecords + 1);
        QCOMPARE(page.m_terminalStartRecord, page.m_records.size());
        QVERIFY(page.m_pendingRecordIndexes.isEmpty());
        QVERIFY(page.m_rxFrameBuffer.isEmpty());
        QVERIFY(page.m_terminalSearchMatches.isEmpty());
        QCOMPARE(page.m_terminalCurrentSearchMatch, -1);
        QCOMPARE(page.m_rxCount, 0);
        QCOMPARE(page.m_txCount, 0);
        QCOMPARE(page.m_lastStatsRxCount, 0);
        QCOMPARE(page.m_lastStatsTxCount, 0);
        QCOMPARE(page.m_rxCounterLabel->text(), QStringLiteral("0 B"));
        QCOMPARE(page.m_txCounterLabel->text(), QStringLiteral("0 B"));
        QCOMPARE(page.m_rxRateLabel->text(), QStringLiteral("0 B/s"));
        QCOMPARE(page.m_txRateLabel->text(), QStringLiteral("0 B/s"));
        page.flushPendingLines();
        page.renderTerminal();
        QVERIFY(page.m_terminalView->toPlainText().isEmpty());

        page.handleReceivedData(QByteArrayLiteral("new"));
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArrayLiteral("tx"));
        page.flushPendingLines();
        page.updateCounters();
        page.updateRateStats();
        QVERIFY(page.m_terminalView->toPlainText().contains(QStringLiteral("new")));
        QVERIFY(!page.m_terminalView->toPlainText().contains(QStringLiteral("needle")));
        QVERIFY(!page.m_terminalView->toPlainText().contains(QStringLiteral("pending")));
        QCOMPARE(page.m_rxCounterLabel->text(), QStringLiteral("3 B"));
        QCOMPARE(page.m_txCounterLabel->text(), QStringLiteral("2 B"));
        QCOMPARE(page.m_rxRateLabel->text(), QStringLiteral("3 B/s"));
        QCOMPARE(page.m_txRateLabel->text(), QStringLiteral("2 B/s"));
    }

    void searchBarDoesNotShowOrLeaveBehindHoverTooltips()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        QPointer<QWidget> searchBar = page.m_terminalSearchBar;
        QTRY_VERIFY(searchBar->isVisible());

        for (int state = 0; state < 3; ++state) {
            if (state == 1) {
                page.m_terminalSearchNextButton->click();
                QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
            } else if (state == 2) {
                page.m_terminalSearchRegexCheck->setChecked(true);
                page.m_terminalSearchEdit->setText(QStringLiteral("["));
                QVERIFY(!page.terminalSearchQuery().valid);
            }
            auto widgets = searchBar->findChildren<QWidget *>();
            widgets.prepend(searchBar);
            for (auto *widget : widgets) {
                QVERIFY2(
                    widget->toolTip().isEmpty(),
                    qPrintable(
                        QStringLiteral("Unexpected tooltip on %1: %2").arg(widget->objectName(), widget->toolTip())));
                if (widget->isVisible()) {
                    const QPoint position = widget->rect().center();
                    QHelpEvent helpEvent(QEvent::ToolTip, position, widget->mapToGlobal(position));
                    QCoreApplication::sendEvent(widget, &helpEvent);
                }
            }
            // LineEdit's built-in buttons install empty tooltip filters. Check
            // visible behavior without depending on that component detail.
            QCoreApplication::processEvents();
            QVERIFY(!QToolTip::isVisible());
            for (auto *tooltip : searchBar->findChildren<FluentQt::ToolTip *>()) {
                QVERIFY(!tooltip->isVisible());
            }
        }

        QVERIFY(searchBar->close());
        QTest::qWait(350);
        QVERIFY(searchBar && !searchBar->isVisible());
        QVERIFY(!QToolTip::isVisible());
        for (auto *tooltip : searchBar->findChildren<FluentQt::ToolTip *>()) {
            QVERIFY(!tooltip->isVisible());
        }
    }

    void searchBarFollowsPageLifetimeAndClosesWithEscape()
    {
        QPointer<QWidget> searchBar;
        {
            WorkbenchPage page(nullptr, false, false);
            prepareHistory(page, false);
            auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
            QVERIFY(searchButton);
            searchButton->click();
            searchBar = page.m_terminalSearchBar;
            QTRY_VERIFY(searchBar->isVisible());

            page.hide();
            QTRY_VERIFY(!page.isVisible());
            QTRY_VERIFY(searchBar && !searchBar->isVisible());
            page.show();
            QCoreApplication::processEvents();
            QVERIFY(!searchBar->isVisible());
            searchButton->click();
            QTRY_VERIFY(searchBar->isVisible());
            QCOMPARE(page.m_terminalSearchBar, searchBar.data());
            QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));

            page.activateWindow();
            QVERIFY(QTest::qWaitForWindowActive(&page));
            if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
                // Offscreen can retain an active QWindow after hide/show without
                // reactivating the QWidget focus chain. Supply that activation
                // before checking Escape's actual focus hand-off.
                QT_WARNING_PUSH
                QT_WARNING_DISABLE_DEPRECATED
                QApplication::setActiveWindow(&page);
                QT_WARNING_POP
            }
            QTRY_VERIFY(page.isActiveWindow());
            page.m_terminalSearchEdit->setFocus();
            QTRY_VERIFY(page.m_terminalSearchEdit->hasFocus());
            QTest::keyClick(page.m_terminalSearchEdit, Qt::Key_Escape);
            QTRY_VERIFY(searchBar && !searchBar->isVisible());
            QTRY_VERIFY(page.m_terminalView->hasFocus());
            QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
            searchButton->click();
            QTRY_VERIFY(searchBar->isVisible());
            page.m_terminalView->setFocus();
            QCoreApplication::processEvents();
            QTest::keyClick(page.m_terminalView, Qt::Key_Escape);
            QTRY_VERIFY(!searchBar->isVisible());
            QVERIFY(page.m_terminalView->hasFocus());
        }
        QTRY_VERIFY(searchBar.isNull());
    }

    void findShortcutFocusesTheQueryAndEnterNavigatesBothDirections()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        page.activateWindow();
        page.m_terminalView->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(!page.m_terminalSearchBar->isVisible());
        QTest::keySequence(page.m_terminalView, QKeySequence(QKeySequence::Find));
        QTRY_VERIFY(page.m_terminalSearchBar->isVisible());
        QTRY_VERIFY(page.m_terminalSearchEdit->hasFocus());
        QCOMPARE(page.m_terminalSearchEdit->selectedText(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);

        QTest::keyClick(page.m_terminalSearchEdit, Qt::Key_Return);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0150")));
        QTest::keyClick(page.m_terminalSearchEdit, Qt::Key_Return, Qt::ShiftModifier);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QTest::keyClick(page.m_terminalSearchEdit, Qt::Key_Enter, Qt::ShiftModifier);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 2);
        QTest::keyClick(page.m_terminalSearchEdit, Qt::Key_Enter);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QVERIFY(page.m_terminalSearchEdit->hasFocus());

        page.m_sendEdit->setFocus();
        QCoreApplication::processEvents();
        QTest::keySequence(page.m_sendEdit, QKeySequence(QKeySequence::Find));
        QTRY_VERIFY(page.m_terminalSearchEdit->hasFocus());
        QCOMPARE(page.m_terminalSearchEdit->selectedText(), QStringLiteral("needle"));
    }

    void liveAppendKeepsReadingPositionWithAutoScrollOff()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        const int selectionAnchor = page.m_terminalView->textCursor().anchor();
        const int selectionPosition = page.m_terminalView->textCursor().position();

        auto *scroll = page.m_terminalView->verticalScrollBar();
        QVERIFY(scroll->maximum() > page.m_terminalView->viewport()->height());
        scroll->setValue(scroll->maximum() / 2);
        QCoreApplication::processEvents();
        const int readingPosition = scroll->value();
        const QString readingLine = firstVisibleLine(page);
        QVERIFY(readingPosition > 0);

        page.handleReceivedData(logLine(300, true));
        page.flushPendingLines();
        QCoreApplication::processEvents();

        QCOMPARE(scroll->value(), readingPosition);
        QCOMPARE(firstVisibleLine(page), readingLine);
        QCOMPARE(page.m_terminalView->textCursor().anchor(), selectionAnchor);
        QCOMPARE(page.m_terminalView->textCursor().position(), selectionPosition);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 4);
        for (const auto &match : page.m_terminalSearchMatches) {
            QVERIFY(hasHighlight(page.m_terminalView->document(), match.position));
        }
    }

    void liveAppendFollowsTailWithAutoScrollOn()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, true);
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        auto *scroll = page.m_terminalView->verticalScrollBar();
        QVERIFY(scroll->maximum() > 0);
        scroll->setValue(scroll->maximum() / 2);

        page.handleReceivedData(logLine(300, true));
        page.flushPendingLines();
        QCoreApplication::processEvents();

        QCOMPARE(scroll->value(), scroll->maximum());
        QCOMPARE(page.m_terminalView->textCursor().position(), page.m_terminalView->document()->characterCount() - 1);
        QCOMPARE(page.m_terminalSearchMatches.size(), 4);
        QVERIFY(hasHighlight(page.m_terminalView->document(), page.m_terminalSearchMatches.last().position));
    }

    void liveSearchAppendsWithoutRebuildingTheDocument()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *document = page.m_terminalView->document();
        const int existingCharacters = document->characterCount();
        QSignalSpy changes(document, &QTextDocument::contentsChange);
        for (int batch = 0; batch < 40; ++batch) {
            for (int row = 0; row < 5; ++row) {
                page.handleReceivedData(logLine(300 + batch * 5 + row, row == 0));
            }
            page.flushPendingLines();
        }
        QCOMPARE(document->blockCount(), 500);
        QCOMPARE(page.m_terminalSearchMatches.size(), 43);
        QVERIFY(!changes.isEmpty());
        for (const auto &change : changes) {
            const int removed = change.at(1).toInt();
            QVERIFY2(
                removed < existingCharacters / 2,
                qPrintable(
                    QStringLiteral("Live search removed %1 existing characters instead of appending.").arg(removed)));
        }
    }

    void nextAndPreviousNavigateAndWrapAfterNewMatches()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0010")));

        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0150")));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 2);
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        page.m_terminalSearchPrevButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 2);

        page.handleReceivedData(logLine(300, true));
        page.flushPendingLines();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 2);
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 3);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0300")));
        QCoreApplication::processEvents();
        QVERIFY(page.m_terminalView->viewport()->rect().intersects(page.m_terminalView->cursorRect()));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        page.m_terminalSearchPrevButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 3);
    }

    void clearDropsOldMatchesAndSearchesOnlyNewRecords()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
        page.m_clearButton->click();
        QVERIFY(page.m_terminalView->document()->isEmpty());
        QVERIFY(page.m_terminalSearchMatches.isEmpty());
        QCOMPARE(page.m_terminalCurrentSearchMatch, -1);
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        page.m_terminalSearchNextButton->click();
        QVERIFY(page.m_terminalView->document()->isEmpty());
        QVERIFY(page.m_terminalSearchMatches.isEmpty());

        page.handleReceivedData(logLine(300));
        page.handleReceivedData(logLine(301, true));
        page.flushPendingLines();
        QCOMPARE(page.m_terminalSearchMatches.size(), 1);
        QVERIFY(!page.m_terminalView->toPlainText().contains(QStringLiteral("record-0010")));
        QVERIFY(hasHighlight(page.m_terminalView->document(), page.m_terminalSearchMatches.first().position));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0301")));
        page.m_terminalSearchPrevButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
    }

    void pruningPreservesTheSelectedSurvivingMatchAndReadingLine()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        // Leave room for byte counters so their first update does not resize
        // the terminal while this test checks head pruning at a fixed width.
        page.resize(1440, 900);
        QCoreApplication::processEvents();
        page.m_terminalView->setLineWrapMode(QTextEdit::WidgetWidth);
        const QByteArray longPayload = QByteArray(" long payload continues across the viewport").repeated(8);
        for (int row = 0; row < 1000; ++row) {
            page.handleReceivedData(logLine(row, row == 10 || row == 500 || row == 990).chopped(1) + longPayload +
                                    QByteArrayLiteral("\n"));
        }
        page.flushPendingLines();
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0500")));

        auto *scroll = page.m_terminalView->verticalScrollBar();
        scroll->setValue(scroll->maximum() * 3 / 4);
        QCoreApplication::processEvents();
        const QString readingLine = firstVisibleLine(page);
        const int readingPosition = scroll->value();
        QTextCursor readingAnchor = page.m_terminalView->cursorForPosition(QPoint(8, 8));
        readingAnchor.setKeepPositionOnInsert(true);
        const int readingAnchorY = page.m_terminalView->cursorRect(readingAnchor).top();
        const QSize readingViewportSize = page.m_terminalView->viewport()->size();
        QVERIFY(!readingLine.contains(QStringLiteral("record-0500")));
        QVERIFY(readingAnchor.block().layout()->lineCount() > 1);

        for (int row = 1000; row < 1100; ++row) {
            page.handleReceivedData(logLine(row, row == 1010 || row == 1090).chopped(1) + longPayload +
                                    QByteArrayLiteral("\n"));
        }
        page.flushPendingLines();
        QCoreApplication::processEvents();

        QCOMPARE(page.m_terminalView->viewport()->size(), readingViewportSize);
        QCOMPARE(page.m_records.size(), 1000);
        QCOMPARE(page.m_terminalView->document()->blockCount(), 1000);
        QVERIFY(page.m_terminalView->document()->firstBlock().text().contains(QStringLiteral("record-0100")));
        QCOMPARE(firstVisibleLine(page), readingLine);
        QCOMPARE(page.m_terminalView->cursorRect(readingAnchor).top(), readingAnchorY);
        QVERIFY(scroll->value() < readingPosition);
        QCOMPARE(page.m_terminalSearchMatches.size(), 4);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0500")));
        QVERIFY(validHighlightedMatches(page));

        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0990")));
        page.m_terminalSearchPrevButton->click();
        page.m_terminalSearchPrevButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 3);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-1090")));
    }

    void pruningDropsTheRemovedSelectedMatchAndReadingAnchor()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        for (int row = 0; row < 1000; ++row) {
            page.handleReceivedData(logLine(row, row == 10 || row == 500 || row == 990));
        }
        page.flushPendingLines();
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0010")));
        page.m_terminalView->verticalScrollBar()->setValue(0);
        QCoreApplication::processEvents();

        for (int row = 1000; row < 1100; ++row) {
            page.handleReceivedData(logLine(row));
        }
        page.flushPendingLines();
        QCoreApplication::processEvents();

        QCOMPARE(page.m_terminalView->verticalScrollBar()->value(), 0);
        QVERIFY(firstVisibleLine(page).contains(QStringLiteral("record-0100")));
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        QCOMPARE(page.m_terminalCurrentSearchMatch, -1);
        QVERIFY(!page.m_terminalView->textCursor().hasSelection());
        QVERIFY(validHighlightedMatches(page));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QCOMPARE(page.m_terminalView->textCursor().selectedText(), QStringLiteral("needle"));
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0500")));
        page.m_terminalSearchPrevButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0990")));
    }

    void multilineRecordPruningKeepsMatchPositionsValidAfterAppendAndSearchChanges()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page, false);
        page.m_terminalSearchEdit->setText(QStringLiteral("needle"));
        QByteArray frame;
        for (int row = 0; row < 1100; ++row) {
            frame.append(logLine(row, row == 10 || row == 150 || row == 1050));
        }
        page.handleReceivedData(frame);
        page.flushPendingLines();

        QCOMPARE(page.m_records.size(), 1);
        QCOMPARE(page.m_terminalView->document()->blockCount(), 1000);
        QVERIFY(page.m_terminalView->document()->firstBlock().text().contains(QStringLiteral("record-0100")));
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        QVERIFY(validHighlightedMatches(page));
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0150")));
        page.m_terminalSearchPrevButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-1050")));

        // A search option rebuilds the same oversized record; its retained matches must stay valid.
        page.m_terminalSearchRegexCheck->setChecked(true);
        QCOMPARE(page.m_terminalView->document()->blockCount(), 1000);
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        QVERIFY(validHighlightedMatches(page));
        QCOMPARE(page.m_terminalCurrentSearchMatch, 0);
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-0150")));

        frame.clear();
        for (int row = 1100; row < 1200; ++row) {
            frame.append(logLine(row, row == 1150));
        }
        page.handleReceivedData(frame);
        page.flushPendingLines();
        QCOMPARE(page.m_records.size(), 2);
        QCOMPARE(page.m_terminalView->document()->blockCount(), 1000);
        QVERIFY(page.m_terminalView->document()->firstBlock().text().contains(QStringLiteral("record-0200")));
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        QCOMPARE(page.m_terminalCurrentSearchMatch, -1);
        QVERIFY(validHighlightedMatches(page));
        page.m_terminalSearchNextButton->click();
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-1050")));
        page.m_terminalSearchPrevButton->click();
        QVERIFY(page.m_terminalView->textCursor().block().text().contains(QStringLiteral("record-1150")));
    }
};

QTEST_MAIN(TerminalSearchTest)
#include "tst_terminal_search.moc"
