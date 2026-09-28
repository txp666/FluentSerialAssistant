#include "app/core/app_settings.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QPointer>
#include <QtGui/QHelpEvent>
#include <QtGui/QPixmap>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtGui/QTextLayout>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QToolTip>

class TerminalSearchTest : public QObject
{
    Q_OBJECT

    static QByteArray logLine(int number, bool match = false)
    {
        return QStringLiteral("record-%1: %2")
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

    static bool captureSearchWindow(QWidget *window)
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
        return window->grab().save(QDir(directory).filePath(QStringLiteral("terminal-search-%1.png").arg(theme)));
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

    void searchWindowStaysOpenDuringTerminalAndDropdownInteraction()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        QPointer<QWidget> searchWindow = page.m_terminalSearchEdit->window();
        QVERIFY(searchWindow != &page);
        QCOMPARE(searchWindow->objectName(), QStringLiteral("terminalSearchWindow"));
        QTRY_VERIFY(searchWindow->isVisible());
        QCOMPARE(searchWindow->windowType(), Qt::Tool);
        QCOMPARE(searchWindow->windowModality(), Qt::NonModal);
        QVERIFY(searchWindow->windowFlags().testFlag(Qt::WindowTitleHint));
        QVERIFY(!searchWindow->windowFlags().testFlag(Qt::FramelessWindowHint));

        QTest::mouseClick(page.m_terminalView->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCoreApplication::processEvents();
        QVERIFY(searchWindow && searchWindow->isVisible());
        QTest::mouseClick(page.m_sendEdit->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(20, 20));
        QCoreApplication::processEvents();
        QVERIFY(searchWindow && searchWindow->isVisible());

        QTest::mouseClick(page.m_terminalFilterCombo, Qt::LeftButton);
        QTRY_VERIFY(page.m_terminalFilterCombo->dropMenu());
        QPointer<FluentQt::ComboBoxMenu> menu = page.m_terminalFilterCombo->dropMenu();
        QTRY_VERIFY(menu && menu->isVisible());
        QVERIFY(searchWindow && searchWindow->isVisible());
        const int receiveIndex = page.m_terminalFilterCombo->findData(QStringLiteral("rx"));
        QVERIFY(receiveIndex >= 0);
        auto *item = menu->view()->item(receiveIndex);
        QVERIFY(item);
        const QRect itemRect = menu->view()->visualItemRect(item);
        QVERIFY(!itemRect.isEmpty());
        QTest::mouseClick(menu->view()->viewport(), Qt::LeftButton, Qt::NoModifier, itemRect.center());
        QTRY_COMPARE(page.m_terminalFilterCombo->currentData().toString(), QStringLiteral("rx"));
        QTRY_VERIFY(!menu || !menu->isVisible());
        QVERIFY(searchWindow && searchWindow->isVisible());
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
    }

    void searchWindowReopensAtItsMovedPositionWithTheSameQuery()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        QPointer<QWidget> searchWindow = page.m_terminalSearchEdit->window();
        QVERIFY(searchWindow != &page);
        QTRY_VERIFY(searchWindow->isVisible());
        QVERIFY(!searchWindow->testAttribute(Qt::WA_DeleteOnClose));
        const QPoint originalPosition = searchWindow->pos();
        searchWindow->move(originalPosition + QPoint(37, 23));
        QCoreApplication::processEvents();
        const QPoint movedPosition = searchWindow->pos();
        QVERIFY(movedPosition != originalPosition);
        page.m_terminalSearchNextButton->click();
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);

        QVERIFY(searchWindow->close());
        QCoreApplication::processEvents();
        QVERIFY(searchWindow);
        QVERIFY(!searchWindow->isVisible());
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        searchButton->click();
        QTRY_VERIFY(searchWindow->isVisible());
        QCOMPARE(page.m_terminalSearchEdit->window(), searchWindow.data());
        QCOMPARE(searchWindow->pos(), movedPosition);
        QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
        QCOMPARE(page.m_terminalSearchMatches.size(), 3);
        QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
    }

    void searchWindowRowsAndEmbeddedIconsAreVerticallyCentered_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void searchWindowRowsAndEmbeddedIconsAreVerticallyCentered()
    {
        QFETCH(bool, dark);
        FluentQt::ThemeManager::instance()->setTheme(dark ? FluentQt::Theme::Dark : FluentQt::Theme::Light);
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        auto *searchWindow = page.m_terminalSearchEdit->window();
        QVERIFY(searchWindow != &page);
        QTRY_VERIFY(searchWindow->isVisible());
        searchWindow->activateWindow();
        page.m_terminalSearchEdit->setFocus();
        QCoreApplication::processEvents();
        QVERIFY(captureSearchWindow(searchWindow));

        const auto centerY = [searchWindow](QWidget *widget) {
            return widget->mapTo(searchWindow, widget->rect().center()).y();
        };
        const QList<QWidget *> searchRow{page.m_terminalSearchEdit, page.m_terminalSearchPrevButton,
                                         page.m_terminalSearchNextButton};
        const QList<QWidget *> optionsRow{page.m_terminalSearchCaseCheck, page.m_terminalSearchRegexCheck,
                                          page.m_terminalFilterCombo, page.m_terminalSummaryLabel};
        for (const auto &row : {searchRow, optionsRow}) {
            const int expectedCenter = centerY(row.first());
            for (auto *widget : row) {
                QVERIFY(widget->isVisible());
                QVERIFY2(qAbs(centerY(widget) - expectedCenter) <= 1,
                         qPrintable(QStringLiteral("%1 center Y=%2, row center Y=%3")
                                        .arg(widget->objectName())
                                        .arg(centerY(widget))
                                        .arg(expectedCenter)));
                const QRect bounds(widget->mapTo(searchWindow, QPoint()), widget->size());
                QVERIFY2(searchWindow->rect().contains(bounds), qPrintable(widget->objectName()));
            }
        }
        QVERIFY(centerY(optionsRow.first()) > centerY(searchRow.first()));

        const int editCenter = centerY(page.m_terminalSearchEdit);
        auto *embeddedSearch = page.m_terminalSearchEdit->searchButton();
        auto *embeddedClear = page.m_terminalSearchEdit->clearButton();
        QVERIFY(embeddedSearch->isVisible());
        QTRY_VERIFY(embeddedClear->isVisible());
        for (QWidget *button : {embeddedSearch, embeddedClear}) {
            QVERIFY2(qAbs(centerY(button) - editCenter) <= 1,
                     qPrintable(QStringLiteral("Embedded search icon center Y=%1, input center Y=%2")
                                    .arg(centerY(button))
                                    .arg(editCenter)));
            const QRect bounds(button->mapTo(page.m_terminalSearchEdit, QPoint()), button->size());
            QVERIFY(page.m_terminalSearchEdit->rect().contains(bounds));
        }
    }

    void searchWindowDoesNotShowOrLeaveBehindHoverTooltips()
    {
        WorkbenchPage page(nullptr, false, false);
        prepareHistory(page, false);
        auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
        QVERIFY(searchButton);
        searchButton->click();
        QPointer<QWidget> searchWindow = page.m_terminalSearchEdit->window();
        QVERIFY(searchWindow != &page);
        QTRY_VERIFY(searchWindow->isVisible());

        for (int state = 0; state < 3; ++state) {
            if (state == 1) {
                page.m_terminalSearchNextButton->click();
                QCOMPARE(page.m_terminalCurrentSearchMatch, 1);
            } else if (state == 2) {
                page.m_terminalSearchRegexCheck->setChecked(true);
                page.m_terminalSearchEdit->setText(QStringLiteral("["));
                QVERIFY(!page.terminalSearchQuery().valid);
            }
            auto widgets = searchWindow->findChildren<QWidget *>();
            widgets.prepend(searchWindow);
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
            for (auto *tooltip : searchWindow->findChildren<FluentQt::ToolTip *>()) {
                QVERIFY(!tooltip->isVisible());
            }
        }

        QVERIFY(searchWindow->close());
        QTest::qWait(350);
        QVERIFY(searchWindow && !searchWindow->isVisible());
        QVERIFY(!QToolTip::isVisible());
        for (auto *tooltip : searchWindow->findChildren<FluentQt::ToolTip *>()) {
            QVERIFY(!tooltip->isVisible());
        }
    }

    void searchWindowFollowsPageLifetimeAndClosesWithEscape()
    {
        QPointer<QWidget> searchWindow;
        {
            WorkbenchPage page(nullptr, false, false);
            prepareHistory(page, false);
            auto *searchButton = page.findChild<FluentQt::ToolButton *>(QStringLiteral("terminalSearchButton"));
            QVERIFY(searchButton);
            searchButton->click();
            searchWindow = page.m_terminalSearchEdit->window();
            QVERIFY(searchWindow != &page);
            QTRY_VERIFY(searchWindow->isVisible());
            searchWindow->move(searchWindow->pos() + QPoint(37, 23));
            QCoreApplication::processEvents();
            const QPoint movedPosition = searchWindow->pos();

            page.hide();
            QTRY_VERIFY(!page.isVisible());
            QTRY_VERIFY(searchWindow && !searchWindow->isVisible());
            page.show();
            QCoreApplication::processEvents();
            searchButton->click();
            QTRY_VERIFY(searchWindow->isVisible());
            QCOMPARE(page.m_terminalSearchEdit->window(), searchWindow.data());
            QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
            QCOMPARE(searchWindow->pos(), movedPosition);

            searchWindow->activateWindow();
            page.m_terminalSearchEdit->setFocus();
            QCoreApplication::processEvents();
            QTest::keyClick(page.m_terminalSearchEdit, Qt::Key_Escape);
            QTRY_VERIFY(searchWindow && !searchWindow->isVisible());
            QCOMPARE(page.m_terminalSearchEdit->text(), QStringLiteral("needle"));
            searchButton->click();
            QTRY_VERIFY(searchWindow->isVisible());
        }
        QTRY_VERIFY(searchWindow.isNull());
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
            page.handleReceivedData(logLine(row, row == 10 || row == 500 || row == 990) + longPayload);
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
            page.handleReceivedData(logLine(row, row == 1010 || row == 1090) + longPayload);
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
            if (!frame.isEmpty()) {
                frame.append('\n');
            }
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
            if (!frame.isEmpty()) {
                frame.append('\n');
            }
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
