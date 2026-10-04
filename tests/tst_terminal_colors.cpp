#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/terminal_color_rules.h"
#include "app/view/settings_page.h"
#include "app/view/terminal_color_dialog.h"
#include "app/view/workbench_page.h"
#include "app/view/workbench_sessions_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QScopeGuard>
#include <QtCore/QTimer>
#include <QtGui/QTextBlock>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QApplication>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QTableWidget>

class TerminalColorsTest : public QObject
{
    Q_OBJECT

    static AppTerminal::ColorRule rule(const QString &pattern, const QColor &color, bool regex = false,
                                       bool wholeLine = false, bool caseSensitive = false)
    {
        AppTerminal::ColorRule result;
        result.pattern = pattern;
        result.color = color;
        result.regularExpression = regex;
        result.wholeLine = wholeLine;
        result.caseSensitive = caseSensitive;
        return result;
    }

    static QColor matchedColor(const QVector<AppTerminal::ColorSpan> &spans, int position)
    {
        for (const auto &span : spans) {
            if (position >= span.start && position < span.start + span.length) {
                return span.color;
            }
        }
        return {};
    }

    static QTextCharFormat formatAt(QTextDocument *document, int position)
    {
        QTextCursor cursor(document);
        cursor.setPosition(position);
        cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
        return cursor.charFormat();
    }

    static void preparePage(WorkbenchPage &page)
    {
        page.m_flushTimer.stop();
        page.m_statsTimer.stop();
        page.m_autoFrameBreakCheck->setChecked(false);
        page.m_timestampCheck->setChecked(false);
        page.m_autoScrollCheck->setChecked(false);
        page.m_showTxCheck->setChecked(true);
        page.m_displayModeSegment->setCurrentItem(QStringLiteral("text"));
        page.m_terminalView->setLineWrapMode(QTextEdit::NoWrap);
        page.resize(1120, 900);
        page.show();
        QCoreApplication::processEvents();
    }

    static bool capture(QWidget &widget, const QString &name, const QRect &region = QRect())
    {
        const QString directory = qEnvironmentVariable("FLUENT_TERMINAL_COLORS_CAPTURE_DIR");
        if (directory.isEmpty()) {
            return true;
        }
        if (!QDir().mkpath(directory)) {
            return false;
        }
        QCoreApplication::processEvents();
        const QPixmap pixmap = region.isValid() ? widget.grab(region) : widget.grab();
        return pixmap.save(QDir(directory).filePath(name));
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

    void literalRegexAndUnicodeUseDocumentOffsets()
    {
        const QColor red(QStringLiteral("#e05252"));
        const QColor blue(QStringLiteral("#3478d4"));
        const QColor green(QStringLiteral("#008c60"));
        AppTerminal::ColorConfig config;
        config.rules = {rule(QStringLiteral("[warn]"), red), rule(QStringLiteral("ERR\\d+"), blue, true),
                        rule(QStringLiteral("Ok"), green, false, false, true)};
        AppTerminal::ColorMatcher matcher;
        matcher.setConfig(config);
        const QString text = QString::fromUtf8("😀 [WARN] err42 OK Ok");
        const auto spans = matcher.ranges(text);
        QCOMPARE(spans.size(), 3);
        QCOMPARE(spans.at(0).start, text.indexOf(QStringLiteral("[WARN]")));
        QCOMPARE(spans.at(0).length, 6);
        QCOMPARE(spans.at(1).start, text.indexOf(QStringLiteral("err42")));
        QCOMPARE(spans.at(1).length, 5);
        QCOMPARE(spans.at(2).start, text.indexOf(QStringLiteral("Ok")));
        QCOMPARE(spans.at(2).length, 2);
        QCOMPARE(matchedColor(spans, text.indexOf(QStringLiteral("OK"))), QColor());
    }

    void earlierRulesWinOnlyOverlappingCharacters()
    {
        const QColor red(QStringLiteral("#e05252"));
        const QColor blue(QStringLiteral("#3478d4"));
        AppTerminal::ColorConfig config;
        config.rules = {rule(QStringLiteral("ERROR"), red), rule(QStringLiteral("error"), blue, false, true)};
        AppTerminal::ColorMatcher matcher;
        matcher.setConfig(config);
        const QString text = QStringLiteral("prefix ERROR details\nordinary");
        const auto spans = matcher.ranges(text);
        for (int i = 0; i < text.indexOf(QLatin1Char('\n')); ++i) {
            QCOMPARE(matchedColor(spans, i), i >= 7 && i < 12 ? red : blue);
        }
        QVERIFY(!matchedColor(spans, text.indexOf(QStringLiteral("ordinary"))).isValid());
        for (int i = 1; i < spans.size(); ++i) {
            QVERIFY(spans.at(i).start >= spans.at(i - 1).start + spans.at(i - 1).length);
        }
    }

    void wholeLineAndAnchorsStayWithinEachContentLine()
    {
        const QColor orange(QStringLiteral("#b47400"));
        AppTerminal::ColorConfig config;
        config.rules = {rule(QStringLiteral("^WARN"), orange, true, true)};
        AppTerminal::ColorMatcher matcher;
        matcher.setConfig(config);
        const QString text = QStringLiteral("okay\nWARN first\r\nnext\nWARN last");
        const auto spans = matcher.ranges(text);
        QCOMPARE(matchedColor(spans, text.indexOf(QStringLiteral("first"))), orange);
        QCOMPARE(matchedColor(spans, text.indexOf(QStringLiteral("last"))), orange);
        QVERIFY(!matchedColor(spans, text.indexOf(QStringLiteral("next"))).isValid());
        QVERIFY(!matchedColor(spans, text.indexOf(QLatin1Char('\n'))).isValid());
        config.rules = {rule(QStringLiteral("first\\s+next"), orange, true)};
        matcher.setConfig(config);
        QVERIFY(matcher.ranges(text).isEmpty());
    }

    void disabledInvalidAndEmptyMatchesAreSafe()
    {
        AppTerminal::ColorConfig config;
        auto disabled = rule(QStringLiteral("ERROR"), QColor(Qt::red));
        disabled.enabled = false;
        auto invalidRegex = rule(QStringLiteral("["), QColor(Qt::green), true);
        auto invalidColor = rule(QStringLiteral("ERROR"), QColor());
        const auto empty = rule(QString(), QColor(Qt::blue));
        QVERIFY(!AppTerminal::validationError(invalidRegex).isEmpty());
        QVERIFY(!AppTerminal::validationError(invalidColor).isEmpty());
        QVERIFY(!AppTerminal::validationError(empty).isEmpty());
        config.rules = {disabled, invalidRegex, invalidColor, empty,
                        rule(QStringLiteral("(?=ERROR)"), QColor(Qt::cyan), true)};
        AppTerminal::ColorMatcher matcher;
        matcher.setConfig(config);
        QVERIFY(matcher.ranges(QStringLiteral("ERROR ERROR")).isEmpty());
        config.rules = {rule(QStringLiteral("ERROR"), QColor(Qt::red))};
        config.enabled = false;
        matcher.setConfig(config);
        QVERIFY(matcher.ranges(QStringLiteral("ERROR")).isEmpty());
    }

    void settingsJsonRoundTripsAllRuleOptions()
    {
        AppTerminal::ColorConfig config;
        config.enabled = false;
        config.espIdfEnabled = false;
        config.enabledPresets = {QStringLiteral("log_levels"), QStringLiteral("status")};
        config.rules = {rule(QStringLiteral("错误|WARN"), QColor(QStringLiteral("#a03d90")), true, true, true),
                        rule(QStringLiteral("ready"), QColor(QStringLiteral("#008c60")))};
        config.rules[1].enabled = false;
        const auto json = AppTerminal::toJson(config);
        const auto restored = AppTerminal::fromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
        QCOMPARE(AppTerminal::toJson(restored), json);
        const auto defaults = AppTerminal::fromJson({});
        QVERIFY(defaults.enabled);
        QVERIFY(defaults.espIdfEnabled);
        QVERIFY(defaults.rules.isEmpty());
        QVERIFY(defaults.enabledPresets.isEmpty());
    }

    void presetsWorkIndependentlyAndCustomRulesTakePriority()
    {
        const QColor custom(QStringLiteral("#9347c9"));
        AppTerminal::ColorConfig config;
        config.enabled = false;
        config.espIdfEnabled = false;
        config.enabledPresets = {QStringLiteral("log_levels")};
        config.rules = {rule(QStringLiteral("ERROR"), custom)};
        AppTerminal::ColorMatcher matcher;
        matcher.setConfig(config);
        const QString text = QStringLiteral("ERROR sensor timeout");
        const QColor preset = matchedColor(matcher.ranges(text), 0);
        QVERIFY(preset.isValid());
        QVERIFY(preset != custom);
        config.enabled = true;
        matcher.setConfig(config);
        QCOMPARE(matchedColor(matcher.ranges(text), 0), custom);
        config.enabledPresets.clear();
        matcher.setConfig(config);
        QCOMPARE(matchedColor(matcher.ranges(text), 0), custom);
        config.enabled = false;
        matcher.setConfig(config);
        QVERIFY(matcher.ranges(text).isEmpty());
        config.enabledPresets = {QStringLiteral("unknown_future_preset")};
        matcher.setConfig(config);
        QVERIFY(matcher.ranges(text).isEmpty());
    }

    void presetRegistryAndSavedSelectionsAreStable()
    {
        QStringList ids;
        for (const auto &preset : AppTerminal::colorPresets()) {
            ids.append(preset.id);
            QVERIFY(!preset.name.isEmpty());
            QVERIFY(!preset.example.isEmpty());
            for (const auto &item : preset.rules) {
                QVERIFY(AppTerminal::validationError(item).isEmpty());
            }
        }
        QCOMPARE(ids, QStringList({QStringLiteral("esp_idf"), QStringLiteral("log_levels"), QStringLiteral("status"),
                                   QStringLiteral("at_commands")}));
        QJsonObject stored;
        stored.insert(QStringLiteral("enabledPresets"),
                      QJsonArray{QStringLiteral("at_commands"), QStringLiteral("unknown"), 17, QStringLiteral("status"),
                                 QStringLiteral("status"), QStringLiteral("esp_idf"), QStringLiteral("log_levels")});
        stored.insert(QStringLiteral("espIdfEnabled"), false);
        const auto restored = AppTerminal::fromJson(stored);
        QCOMPARE(restored.enabledPresets,
                 QStringList({QStringLiteral("log_levels"), QStringLiteral("status"), QStringLiteral("at_commands")}));
        QVERIFY(!restored.espIdfEnabled);
    }

    void presetExamplesRespectWordAndLineBoundaries_data()
    {
        QTest::addColumn<QString>("preset");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("token");
        QTest::addColumn<bool>("wholeLine");
        QTest::newRow("log-error") << QStringLiteral("log_levels") << QStringLiteral("ERROR sensor timeout")
                                   << QStringLiteral("ERROR") << true;
        QTest::newRow("log-warning") << QStringLiteral("log_levels") << QStringLiteral("warning low voltage")
                                     << QStringLiteral("warning") << true;
        QTest::newRow("log-info") << QStringLiteral("log_levels") << QStringLiteral("[info] online")
                                  << QStringLiteral("info") << true;
        QTest::newRow("log-debug") << QStringLiteral("log_levels") << QStringLiteral("DEBUG value=1")
                                   << QStringLiteral("DEBUG") << true;
        QTest::newRow("log-embedded-word")
            << QStringLiteral("log_levels") << QStringLiteral("MYERROR field") << QString() << false;
        QTest::newRow("status-pass") << QStringLiteral("status") << QStringLiteral("result PASS complete")
                                     << QStringLiteral("PASS") << false;
        QTest::newRow("status-failed") << QStringLiteral("status") << QStringLiteral("result FAILED")
                                       << QStringLiteral("FAILED") << false;
        QTest::newRow("status-chinese") << QStringLiteral("status") << QStringLiteral("操作失败")
                                        << QStringLiteral("失败") << false;
        QTest::newRow("status-timeout") << QStringLiteral("status") << QStringLiteral("response TIMEOUT")
                                        << QStringLiteral("TIMEOUT") << false;
        QTest::newRow("status-embedded-word")
            << QStringLiteral("status") << QStringLiteral("BOOK BYPASS") << QString() << false;
        QTest::newRow("at-ok") << QStringLiteral("at_commands") << QStringLiteral("OK") << QStringLiteral("OK") << true;
        QTest::newRow("at-connect") << QStringLiteral("at_commands") << QStringLiteral("CONNECT 115200")
                                    << QStringLiteral("CONNECT") << true;
        QTest::newRow("at-error") << QStringLiteral("at_commands") << QStringLiteral("  +CME ERROR: 42")
                                  << QStringLiteral("ERROR") << true;
        QTest::newRow("at-carrier") << QStringLiteral("at_commands") << QStringLiteral("NO CARRIER")
                                    << QStringLiteral("NO CARRIER") << true;
        QTest::newRow("at-middle-of-line")
            << QStringLiteral("at_commands") << QStringLiteral("prefix OK suffix") << QString() << false;
        QTest::newRow("at-command-not-reply")
            << QStringLiteral("at_commands") << QStringLiteral("AT+ERROR") << QString() << false;
    }

    void presetExamplesRespectWordAndLineBoundaries()
    {
        QFETCH(QString, preset);
        QFETCH(QString, text);
        QFETCH(QString, token);
        QFETCH(bool, wholeLine);
        AppTerminal::ColorConfig config;
        config.enabled = false;
        config.espIdfEnabled = false;
        config.enabledPresets = {preset};
        AppTerminal::ColorMatcher matcher;
        matcher.setConfig(config);
        QVERIFY(matcher.hasRules());
        const auto spans = matcher.ranges(text);
        if (token.isEmpty()) {
            QVERIFY(spans.isEmpty());
            return;
        }
        const int tokenStart = text.indexOf(token);
        QVERIFY(tokenStart >= 0);
        const QColor color = matchedColor(spans, tokenStart);
        QVERIFY(color.isValid());
        for (int i = 0; i < text.size(); ++i) {
            const bool shouldMatch = wholeLine || (i >= tokenStart && i < tokenStart + token.size());
            QCOMPARE(matchedColor(spans, i), shouldMatch ? color : QColor());
        }
    }

    void presetsRenderWithoutCustomRules_data()
    {
        QTest::addColumn<bool>("customEnabled");
        QTest::newRow("empty-custom-rules") << true;
        QTest::newRow("custom-colors-disabled") << false;
    }

    void presetsRenderWithoutCustomRules()
    {
        QFETCH(bool, customEnabled);
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        AppTerminal::ColorConfig config;
        config.enabled = customEnabled;
        config.espIdfEnabled = false;
        config.enabledPresets = {QStringLiteral("log_levels")};
        page.applyTerminalColorConfig(config);
        page.handleReceivedData(QByteArray("ERROR sensor timeout\nordinary sample"));
        page.flushPendingLines();
        auto *document = page.m_terminalView->document();
        const QString text = document->toPlainText();
        const int errorPosition = text.indexOf(QStringLiteral("ERROR"));
        const int ordinaryPosition = text.indexOf(QStringLiteral("ordinary"));
        QVERIFY(errorPosition >= 0);
        QVERIFY(ordinaryPosition > errorPosition);
        const auto plainForeground = formatAt(document, ordinaryPosition).foreground();
        const auto presetForeground = formatAt(document, errorPosition).foreground();
        QVERIFY(presetForeground != plainForeground);
        page.m_terminalSearchEdit->setText(QStringLiteral("ERROR"));
        QCOMPARE(formatAt(document, errorPosition).foreground(), presetForeground);
        QCOMPARE(formatAt(document, errorPosition).background().color(), QColor(255, 214, 10, 96));
        config.enabledPresets.clear();
        page.applyTerminalColorConfig(config);
        QCOMPARE(formatAt(document, errorPosition).foreground(), plainForeground);
    }

    void contentColorsExcludeMetadataAndKeepSearchHighlights()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        page.m_timestampCheck->setChecked(true);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, QByteArray("ERROR failed"), false,
                          QStringLiteral("ERROR source"));
        page.flushPendingLines();
        auto *document = page.m_terminalView->document();
        const QString text = document->toPlainText();
        const int sourcePosition = text.indexOf(QStringLiteral("ERROR source"));
        const int payloadPosition = text.indexOf(QStringLiteral("ERROR failed"));
        QVERIFY(sourcePosition > 0);
        QVERIFY(payloadPosition > sourcePosition);
        const auto timestampFormat = formatAt(document, 0);
        const auto sourceFormat = formatAt(document, sourcePosition);
        const QColor red(QStringLiteral("#e05252"));
        AppTerminal::ColorConfig config;
        config.rules = {rule(QStringLiteral("ERROR"), red, false, true),
                        rule(QStringLiteral("\\d"), QColor(Qt::magenta), true)};
        page.applyTerminalColorConfig(config);
        page.m_terminalSearchEdit->setText(QStringLiteral("ERROR"));
        QCOMPARE(document->toPlainText(), text);
        QCOMPARE(formatAt(document, 0).foreground(), timestampFormat.foreground());
        QCOMPARE(formatAt(document, sourcePosition).foreground(), sourceFormat.foreground());
        QCOMPARE(formatAt(document, payloadPosition).foreground().color(), red);
        QCOMPARE(formatAt(document, text.size() - 1).foreground().color(), red);
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        QCOMPARE(formatAt(document, sourcePosition).background().color(), QColor(255, 214, 10, 96));
        QCOMPARE(formatAt(document, payloadPosition).background().color(), QColor(255, 214, 10, 96));
        QCOMPARE(formatAt(document, text.size() - 1).background().style(), Qt::NoBrush);
    }

    void leadingWhitespaceAndMultilinePayloadArePreserved()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        const QColor red(QStringLiteral("#e05252"));
        AppTerminal::ColorConfig config;
        config.espIdfEnabled = false;
        config.rules = {rule(QStringLiteral("^  ERROR"), red, true, true)};
        page.applyTerminalColorConfig(config);
        page.handleReceivedData(QByteArray("  ERROR details\nordinary\n  ERROR again"));
        page.flushPendingLines();
        auto *document = page.m_terminalView->document();
        const QString text = document->toPlainText();
        QCOMPARE(text, QStringLiteral("«   ERROR details\nordinary\n  ERROR again"));
        QCOMPARE(formatAt(document, text.indexOf(QStringLiteral("details"))).foreground().color(), red);
        QCOMPARE(formatAt(document, text.indexOf(QStringLiteral("again"))).foreground().color(), red);
        QVERIFY(formatAt(document, text.indexOf(QStringLiteral("ordinary"))).foreground().color() != red);
    }

    void espPrefixCanBeDisabledAndCustomRulesOverrideIt()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        page.handleReceivedData(QByteArray("E (123) failed"));
        page.flushPendingLines();
        auto *document = page.m_terminalView->document();
        const QString text = document->toPlainText();
        const int prefixPosition = text.indexOf(QStringLiteral("E (123)"));
        const int messagePosition = text.indexOf(QStringLiteral("failed"));
        const auto plainForeground = formatAt(document, messagePosition).foreground();
        const auto espForeground = formatAt(document, prefixPosition).foreground();
        QVERIFY(espForeground != plainForeground);
        AppTerminal::ColorConfig config;
        config.enabled = false;
        page.applyTerminalColorConfig(config);
        QCOMPARE(formatAt(document, prefixPosition).foreground(), espForeground);
        config.espIdfEnabled = false;
        page.applyTerminalColorConfig(config);
        QCOMPARE(formatAt(document, prefixPosition).foreground(), plainForeground);
        const QColor custom(QStringLiteral("#9c45ac"));
        config.enabled = true;
        config.espIdfEnabled = true;
        config.rules = {rule(QStringLiteral("E"), custom)};
        page.applyTerminalColorConfig(config);
        QCOMPARE(formatAt(document, prefixPosition).foreground().color(), custom);
        QCOMPARE(formatAt(document, prefixPosition + 3).foreground(), espForeground);
    }

    void customMatchesOverrideTxColorAndAppendWithoutRebuildingHistory()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        const QColor red(QStringLiteral("#e05252"));
        const QColor txColor(QStringLiteral("#2c74ac"));
        page.m_txColorButton->setColor(txColor);
        AppTerminal::ColorConfig config;
        config.espIdfEnabled = false;
        config.rules = {rule(QStringLiteral("ERROR"), red)};
        page.applyTerminalColorConfig(config);
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, QByteArray("send ERROR"), false);
        page.flushPendingLines();
        page.m_terminalSearchEdit->setText(QStringLiteral("ERROR"));
        auto *document = page.m_terminalView->document();
        const QString before = document->toPlainText();
        QCOMPARE(formatAt(document, before.indexOf(QStringLiteral("send"))).foreground().color(), txColor);
        QCOMPARE(formatAt(document, before.indexOf(QStringLiteral("ERROR"))).foreground().color(), red);
        QSignalSpy changes(document, &QTextDocument::contentsChange);
        page.handleReceivedData(QByteArray("receive ERROR"));
        page.flushPendingLines();
        QVERIFY(!changes.isEmpty());
        for (const auto &change : changes) {
            QCOMPARE(change.at(1).toInt(), 0);
        }
        const QString after = document->toPlainText();
        QVERIFY(after.startsWith(before));
        QCOMPARE(page.m_terminalSearchMatches.size(), 2);
        const int newError = after.lastIndexOf(QStringLiteral("ERROR"));
        QCOMPARE(formatAt(document, newError).foreground().color(), red);
        QCOMPARE(formatAt(document, newError).background().color(), QColor(255, 214, 10, 96));
    }

    void applyingRulesRetainsReadingPositionAndSelectedSearchMatch()
    {
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        for (int i = 0; i < 200; ++i) {
            page.handleReceivedData(QStringLiteral("record %1 ERROR details").arg(i).toUtf8());
        }
        page.flushPendingLines();
        page.m_terminalSearchEdit->setText(QStringLiteral("ERROR"));
        page.moveTerminalSearchMatch(1);
        const int selectedMatch = page.m_terminalCurrentSearchMatch;
        auto *scroll = page.m_terminalView->verticalScrollBar();
        QVERIFY(scroll->maximum() > 0);
        scroll->setValue(scroll->maximum() / 2);
        const int previousScroll = scroll->value();
        const QString previousText = page.m_terminalView->toPlainText();
        AppTerminal::ColorConfig config;
        config.rules = {rule(QStringLiteral("ERROR"), QColor(QStringLiteral("#e05252")), false, true)};
        page.applyTerminalColorConfig(config);
        QCOMPARE(scroll->value(), previousScroll);
        QCOMPARE(page.m_terminalCurrentSearchMatch, selectedMatch);
        QCOMPARE(page.m_terminalSearchMatches.size(), 200);
        QCOMPARE(page.m_terminalView->toPlainText(), previousText);
    }

    void globalSettingsLoadForEverySessionAndStaleSessionsCannotOverwriteThem()
    {
        AppTerminal::ColorConfig config;
        config.espIdfEnabled = false;
        config.enabledPresets = {QStringLiteral("status")};
        const QColor red(QStringLiteral("#e05252"));
        config.rules = {rule(QStringLiteral("ERROR"), red, false, true)};
        WorkbenchPage stale(nullptr, false, false);
        preparePage(stale);
        QVERIFY(stale.m_terminalColorConfig.rules.isEmpty());
        AppTerminal::saveColorConfig(config);
        AppSettings settings;
        const auto saved =
            QJsonDocument::fromJson(settings.value(QStringLiteral("terminal/contentColors")).toByteArray());
        QCOMPARE(saved.object(), AppTerminal::toJson(config));
        stale.saveSettings();
        QCOMPARE(AppTerminal::toJson(AppTerminal::loadColorConfig()), AppTerminal::toJson(config));
        WorkbenchPage restored(nullptr, true, false);
        preparePage(restored);
        QCOMPARE(AppTerminal::toJson(restored.m_terminalColorConfig), AppTerminal::toJson(config));
        restored.handleReceivedData(QByteArray("ERROR restored"));
        restored.flushPendingLines();
        QCOMPARE(formatAt(restored.m_terminalView->document(), 2).foreground().color(), red);
        WorkbenchPage copied(nullptr, false, false);
        preparePage(copied);
        QCOMPARE(AppTerminal::toJson(copied.m_terminalColorConfig), AppTerminal::toJson(config));
        copied.copySessionConfigFrom(stale);
        QCOMPARE(AppTerminal::toJson(copied.m_terminalColorConfig), AppTerminal::toJson(config));
        copied.handleReceivedData(QByteArray("ERROR copied"));
        copied.flushPendingLines();
        QCOMPARE(formatAt(copied.m_terminalView->document(), 2).foreground().color(), red);
        stale.reloadTerminalColors();
        QCOMPARE(AppTerminal::toJson(stale.m_terminalColorConfig), AppTerminal::toJson(config));
    }

    void settingsCardAppliesToOpenTabsAndCancelPreservesGlobalRules()
    {
        AppTerminal::ColorConfig config;
        config.enabledPresets = {QStringLiteral("status")};
        config.rules = {rule(QStringLiteral("ERROR"), QColor(QStringLiteral("#e05252")))};
        AppTerminal::saveColorConfig(config);
        WorkbenchSessionsPage sessions;
        auto *tabs = sessions.findChild<FluentQt::TabWidget *>();
        QVERIFY(tabs);
        QVERIFY(QMetaObject::invokeMethod(tabs, "tabAddRequested", Qt::DirectConnection));
        const auto pages = sessions.findChildren<WorkbenchPage *>();
        QCOMPARE(pages.size(), 2);
        for (auto *page : pages) {
            page->m_flushTimer.stop();
            page->m_statsTimer.stop();
            QCOMPARE(AppTerminal::toJson(page->m_terminalColorConfig), AppTerminal::toJson(config));
        }

        SettingsPage settings;
        connect(&settings, &SettingsPage::terminalColorsChanged, &sessions,
                &WorkbenchSessionsPage::reloadTerminalColors);
        QSignalSpy changed(&settings, &SettingsPage::terminalColorsChanged);
        auto *card = settings.findChild<FluentQt::PushSettingCard *>(QStringLiteral("terminalColorSettingsCard"));
        QVERIFY(card);
        settings.resize(1120, 900);
        settings.show();
        QCoreApplication::processEvents();

        bool dialogOpened = false;
        bool dialogEdited = false;
        bool dialogApplied = false;
        bool shouldApply = false;
        QTimer interaction;
        interaction.setSingleShot(true);
        connect(&interaction, &QTimer::timeout, &settings, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) {
                return;
            }
            dialogOpened = dialog->objectName() == QStringLiteral("terminalColorDialog");
            auto *table = dialog->findChild<QTableWidget *>(QStringLiteral("terminalColorRulesTable"));
            if (!table || table->rowCount() != 1 || !table->item(0, 2)) {
                dialog->reject();
                return;
            }
            table->item(0, 2)->setText(QStringLiteral("WARN"));
            dialogEdited = true;
            if (shouldApply) {
                auto *apply = dialog->findChild<QAbstractButton *>(QStringLiteral("terminalColorApplyButton"));
                if (apply) {
                    apply->click();
                    dialogApplied = dialog->result() == QDialog::Accepted;
                    return;
                }
            }
            dialog->reject();
        });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &settings, []() {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                dialog->reject();
            }
        });

        interaction.start(0);
        watchdog.start(2000);
        card->button()->click();
        watchdog.stop();
        QVERIFY(dialogOpened);
        QVERIFY(dialogEdited);
        QCOMPARE(changed.size(), 0);
        QCOMPARE(AppTerminal::toJson(AppTerminal::loadColorConfig()), AppTerminal::toJson(config));

        dialogOpened = false;
        dialogEdited = false;
        shouldApply = true;
        interaction.start(0);
        watchdog.start(2000);
        card->button()->click();
        watchdog.stop();
        QVERIFY(dialogOpened);
        QVERIFY(dialogEdited);
        QVERIFY(dialogApplied);
        QCOMPARE(changed.size(), 1);
        config.rules[0].pattern = QStringLiteral("WARN");
        QCOMPARE(AppTerminal::toJson(AppTerminal::loadColorConfig()), AppTerminal::toJson(config));
        for (auto *page : pages) {
            QCOMPARE(AppTerminal::toJson(page->m_terminalColorConfig), AppTerminal::toJson(config));
            page->handleReceivedData(QByteArray("WARN updated"));
            page->flushPendingLines();
            const QString text = page->m_terminalView->toPlainText();
            const int position = text.indexOf(QStringLiteral("WARN"));
            QVERIFY(position >= 0);
            QCOMPARE(formatAt(page->m_terminalView->document(), position).foreground().color(), config.rules[0].color);
        }
    }

    void dialogRejectsInvalidExpressionsAndAppliesValidEdits()
    {
        AppTerminal::ColorConfig config;
        config.rules = {rule(QStringLiteral("ERROR"), QColor(QStringLiteral("#e05252")))};
        TerminalColorDialog dialog(config);
        dialog.show();
        QCoreApplication::processEvents();
        auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("terminalColorRulesTable"));
        auto *apply = dialog.findChild<QAbstractButton *>(QStringLiteral("terminalColorApplyButton"));
        auto *esp = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorEspIdfCheck"));
        QVERIFY(table);
        QVERIFY(apply);
        QVERIFY(esp);
        QCOMPARE(table->rowCount(), 1);
        auto *mode = table->cellWidget(0, 1)->findChild<FluentQt::ComboBox *>();
        auto *scope = table->cellWidget(0, 4)->findChild<FluentQt::ComboBox *>();
        QVERIFY(mode);
        QVERIFY(scope);
        mode->setCurrentIndex(1);
        table->item(0, 2)->setText(QStringLiteral("["));
        apply->click();
        QVERIFY(dialog.isVisible());
        QCOMPARE(AppTerminal::toJson(dialog.config()), AppTerminal::toJson(config));
        table->item(0, 2)->setText(QStringLiteral("WARN|ERROR"));
        scope->setCurrentIndex(1);
        esp->setChecked(false);
        apply->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QVERIFY(!dialog.isVisible());
        const auto accepted = dialog.config();
        QVERIFY(!accepted.espIdfEnabled);
        QCOMPARE(accepted.rules.size(), 1);
        QCOMPARE(accepted.rules.first().pattern, QStringLiteral("WARN|ERROR"));
        QVERIFY(accepted.rules.first().regularExpression);
        QVERIFY(accepted.rules.first().wholeLine);
    }

    void dialogCancelDoesNotChangeConfiguration()
    {
        AppTerminal::ColorConfig config;
        config.enabledPresets = {QStringLiteral("status")};
        config.rules = {rule(QStringLiteral("ERROR"), QColor(QStringLiteral("#e05252")))};
        TerminalColorDialog dialog(config);
        auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("terminalColorRulesTable"));
        auto *cancel = dialog.findChild<QAbstractButton *>(QStringLiteral("terminalColorCancelButton"));
        auto *status = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorPreset_status"));
        auto *atCommands = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorPreset_at_commands"));
        QVERIFY(table);
        QVERIFY(cancel);
        QVERIFY(status);
        QVERIFY(atCommands);
        QVERIFY(status->isChecked());
        status->setChecked(false);
        atCommands->setChecked(true);
        table->item(0, 2)->setText(QStringLiteral("changed"));
        cancel->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QCOMPARE(AppTerminal::toJson(dialog.config()), AppTerminal::toJson(config));
    }

    void dialogCanSelectPresetsWhileCustomColorsAreDisabled()
    {
        AppTerminal::ColorConfig config;
        config.enabled = false;
        config.enabledPresets = {QStringLiteral("status")};
        TerminalColorDialog dialog(config);
        dialog.show();
        QCoreApplication::processEvents();
        auto *apply = dialog.findChild<QAbstractButton *>(QStringLiteral("terminalColorApplyButton"));
        auto *esp = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorEspIdfCheck"));
        auto *logs = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorPreset_log_levels"));
        auto *status = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorPreset_status"));
        auto *atCommands = dialog.findChild<FluentQt::CheckBox *>(QStringLiteral("terminalColorPreset_at_commands"));
        QVERIFY(apply);
        QVERIFY(esp);
        QVERIFY(logs);
        QVERIFY(status);
        QVERIFY(atCommands);
        for (auto *preset : {esp, logs, status, atCommands}) {
            QVERIFY(preset->isEnabled());
            QVERIFY(preset->isVisible());
        }
        QVERIFY(esp->isChecked());
        QVERIFY(!logs->isChecked());
        QVERIFY(status->isChecked());
        esp->setChecked(false);
        logs->setChecked(true);
        status->setChecked(false);
        atCommands->setChecked(true);
        apply->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        const auto accepted = dialog.config();
        QVERIFY(!accepted.enabled);
        QVERIFY(!accepted.espIdfEnabled);
        QCOMPARE(accepted.enabledPresets, QStringList({QStringLiteral("log_levels"), QStringLiteral("at_commands")}));
        AppTerminal::saveColorConfig(accepted);
        QCOMPARE(AppTerminal::toJson(AppTerminal::loadColorConfig()), AppTerminal::toJson(accepted));
    }

    void captureConfiguredTerminal_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<bool>("dark");
        QTest::newRow("chinese-light") << QStringLiteral("zh_CN") << false;
        QTest::newRow("english-light") << QStringLiteral("en_US") << false;
        QTest::newRow("chinese-dark") << QStringLiteral("zh_CN") << true;
        QTest::newRow("english-dark") << QStringLiteral("en_US") << true;
    }

    void captureConfiguredTerminal()
    {
        QFETCH(QString, locale);
        QFETCH(bool, dark);
        const QString originalLocale = FluentQt::FluentConfig::instance()->localeName();
        const auto originalTheme = FluentQt::FluentConfig::instance()->themeMode();
        const auto restorePreferences = qScopeGuard([originalLocale, originalTheme]() {
            AppI18n::applyLocale(originalLocale);
            FluentQt::FluentConfig::instance()->setThemeMode(originalTheme);
        });
        AppI18n::applyLocale(locale);
        FluentQt::FluentConfig::instance()->setThemeMode(dark ? FluentQt::Theme::Dark : FluentQt::Theme::Light);
        FluentQt::ThemeManager::instance()->setTheme(dark ? FluentQt::Theme::Dark : FluentQt::Theme::Light);
        const QString suffix = locale + (dark ? QStringLiteral("-dark") : QStringLiteral("-light"));
        WorkbenchPage page(nullptr, false, false);
        preparePage(page);
        AppTerminal::ColorConfig config;
        config.enabledPresets = {QStringLiteral("log_levels"), QStringLiteral("status"), QStringLiteral("at_commands")};
        config.rules = {rule(QStringLiteral("sensor timeout"), QColor(QStringLiteral("#9347c9"))),
                        rule(QStringLiteral("temp=\\d+\\.\\d+"), QColor(QStringLiteral("#3478d4")), true)};
        page.applyTerminalColorConfig(config);
        page.handleReceivedData(QByteArray("INFO device connected\nWARN input voltage low\nERROR sensor timeout\n"
                                           "sample temp=24.8 humidity=52%\nSUCCESS calibration complete\n"
                                           "AT+VERSION?\nOK\nE (123) legacy ESP-IDF log"));
        page.flushPendingLines();
        QVERIFY(capture(page, QStringLiteral("terminal-content-colors-page-%1.png").arg(suffix)));
        // Translucent settings cards need the production window backdrop for screenshots.
        // MSFluentWindow's native frame integration cannot run with Qt's headless backends.
        const QString platform = QGuiApplication::platformName();
        const bool nativeWindow = platform != QStringLiteral("offscreen") && platform != QStringLiteral("minimal");
        if (nativeWindow && !qEnvironmentVariableIsEmpty("FLUENT_TERMINAL_COLORS_CAPTURE_DIR")) {
            FluentQt::MSFluentWindow settingsWindow;
            SettingsPage settings(&settingsWindow);
            settingsWindow.addSubInterface(&settings, FluentQt::icon(FluentQt::FluentIcon::Setting),
                                           AppI18n::text("设置"));
            settingsWindow.navigationInterface()->setVisible(false);
            settingsWindow.navigationInterface()->setFixedWidth(0);
            QVERIFY(settingsWindow.switchTo(&settings));
            settingsWindow.resize(1120, 900);
            settingsWindow.show();
            QCoreApplication::processEvents();
            auto *card = settings.findChild<FluentQt::PushSettingCard *>(QStringLiteral("terminalColorSettingsCard"));
            QVERIFY(card);
            settings.ensureWidgetVisible(card, 0, 30);
            QCoreApplication::processEvents();
            QTRY_VERIFY(
                settings.viewport()->rect().contains(QRect(card->mapTo(settings.viewport(), QPoint()), card->size())));
            settingsWindow.repaint();
            settings.viewport()->repaint();
            QCoreApplication::processEvents();
            const QRect cardBounds(card->mapTo(&settingsWindow, QPoint()), card->size());
            QVERIFY(settingsWindow.rect().contains(cardBounds));
            QVERIFY(capture(settingsWindow, QStringLiteral("terminal-content-colors-settings-%1.png").arg(suffix),
                            cardBounds));
            QVERIFY(
                capture(settingsWindow, QStringLiteral("terminal-content-colors-settings-page-%1.png").arg(suffix)));
        }
        TerminalColorDialog dialog(config);
        dialog.show();
        QCoreApplication::processEvents();
        auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("terminalColorRulesTable"));
        QVERIFY(table);
        for (const int width : {840, 780}) {
            dialog.resize(width, 600);
            QCoreApplication::processEvents();
            QVERIFY(capture(dialog, QStringLiteral("terminal-content-colors-dialog-%1-%2.png").arg(suffix).arg(width)));
            QCOMPARE(dialog.width(), width);
            QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
            for (int row = 0; row < table->rowCount(); ++row) {
                for (const int column : {0, 1, 3, 4, 5}) {
                    QWidget *cell = table->cellWidget(row, column);
                    QVERIFY(cell);
                    QVERIFY(table->viewport()->rect().contains(cell->geometry()));
                    const auto controls = cell->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
                    QCOMPARE(controls.size(), 1);
                    QWidget *control = controls.first();
                    QVERIFY(control->isVisible());
                    QVERIFY2(cell->rect().contains(control->geometry()),
                             qPrintable(QStringLiteral("Cell %1,%2 clips its control at width %3")
                                            .arg(row)
                                            .arg(column)
                                            .arg(width)));
                    if (auto *combo = qobject_cast<FluentQt::ComboBox *>(control)) {
                        QVERIFY2(combo->width() >= combo->sizeHint().width(),
                                 qPrintable(QStringLiteral("%1: width %2 is below text size hint %3")
                                                .arg(combo->currentText())
                                                .arg(combo->width())
                                                .arg(combo->sizeHint().width())));
                    }
                }
            }
            QList<QRect> buttonBounds;
            for (const QString &name :
                 {QStringLiteral("terminalColorAddButton"), QStringLiteral("terminalColorRemoveButton"),
                  QStringLiteral("terminalColorUpButton"), QStringLiteral("terminalColorDownButton"),
                  QStringLiteral("terminalColorApplyButton"), QStringLiteral("terminalColorCancelButton"),
                  QStringLiteral("terminalColorEspIdfCheck"), QStringLiteral("terminalColorPreset_log_levels"),
                  QStringLiteral("terminalColorPreset_status"), QStringLiteral("terminalColorPreset_at_commands")}) {
                auto *button = dialog.findChild<QAbstractButton *>(name);
                QVERIFY(button);
                QVERIFY(button->isVisible());
                QVERIFY2(button->width() >= button->sizeHint().width(), qPrintable(name));
                const QRect bounds(button->mapTo(&dialog, QPoint()), button->size());
                QVERIFY(dialog.rect().contains(bounds));
                for (const QRect &previous : buttonBounds) {
                    QVERIFY(!previous.intersects(bounds));
                }
                buttonBounds.append(bounds);
            }
            for (const QString &id : {QStringLiteral("esp_idf"), QStringLiteral("log_levels"), QStringLiteral("status"),
                                      QStringLiteral("at_commands")}) {
                auto *example = dialog.findChild<QLabel *>(QStringLiteral("terminalColorPresetExample_") + id);
                auto *check = dialog.findChild<QAbstractButton *>(id == QStringLiteral("esp_idf")
                                                                      ? QStringLiteral("terminalColorEspIdfCheck")
                                                                      : QStringLiteral("terminalColorPreset_") + id);
                QVERIFY(example);
                QVERIFY(check);
                QVERIFY(example->isVisible());
                QVERIFY(!example->text().isEmpty());
                const QRect exampleBounds(example->mapTo(&dialog, QPoint()), example->size());
                const QRect checkBounds(check->mapTo(&dialog, QPoint()), check->size());
                QVERIFY(dialog.rect().contains(exampleBounds));
                QVERIFY(!exampleBounds.intersects(checkBounds));
            }
        }
    }
};

QTEST_MAIN(TerminalColorsTest)
#include "tst_terminal_colors.moc"
