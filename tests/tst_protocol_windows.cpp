#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"
#include "app/view/plot_parser_dialog.h"
#include "app/view/protocol_template_window.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QVariantAnimation>
#include <QtGui/QPixmap>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QVBoxLayout>

class ProtocolWindowsTest : public QObject
{
    Q_OBJECT

    static void capture(QWidget &widget, const QString &name)
    {
        const QString directory = qEnvironmentVariable("FLUENT_PROTOCOL_CAPTURE_DIR");
        if (directory.isEmpty()) {
            return;
        }
        QDir().mkpath(directory);
        QTest::qWait(80);
        widget.grab().save(QDir(directory).filePath(name + QStringLiteral(".png")));
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
    }

    void sidebarExpansionKeepsHeadersAnchored_data()
    {
        QTest::addColumn<QString>("targetTitle");
        QTest::addColumn<bool>("scrolled");
        QTest::newRow("protocol-at-top") << AppI18n::text("协议模板") << false;
        QTest::newRow("protocol-scrolled") << AppI18n::text("协议模板") << true;
        QTest::newRow("send-at-top") << AppI18n::text("发送设置") << false;
        QTest::newRow("send-scrolled") << AppI18n::text("发送设置") << true;
    }

    void sidebarExpansionKeepsHeadersAnchored()
    {
        QFETCH(QString, targetTitle);
        QFETCH(bool, scrolled);

        WorkbenchPage page(nullptr, false, false);
        page.resize(1120, 840);
        page.show();
        QVERIFY(page.m_sidePanel && page.m_sideScroll);
        auto *sideLayout = qobject_cast<QVBoxLayout *>(page.m_sidePanel->layout());
        QVERIFY(sideLayout);

        QList<FluentQt::ExpandSettingCard *> cards;
        FluentQt::ExpandSettingCard *protocolCard = nullptr;
        FluentQt::ExpandSettingCard *target = nullptr;
        for (int index = 0; index < sideLayout->count(); ++index) {
            auto *card = qobject_cast<FluentQt::ExpandSettingCard *>(sideLayout->itemAt(index)->widget());
            if (!card) {
                continue;
            }
            cards.append(card);
            if (card->titleLabel()->text() == AppI18n::text("协议模板")) {
                protocolCard = card;
            }
            if (card->titleLabel()->text() == targetTitle) {
                target = card;
            }
        }
        QVERIFY(protocolCard && target);
        QVERIFY(!cards.isEmpty());
        QVERIFY(cards.first()->isExpanded());
        auto *connectionAnimation = cards.first()->findChild<QVariantAnimation *>(
            QStringLiteral("expandSettingCardAnimation"));
        auto *protocolAnimation = protocolCard->findChild<QVariantAnimation *>(
            QStringLiteral("expandSettingCardAnimation"));
        QVERIFY(connectionAnimation && protocolAnimation);
        QTRY_COMPARE(connectionAnimation->state(), QAbstractAnimation::Stopped);
        if (target != protocolCard) {
            protocolCard->setExpanded(true);
            QTRY_COMPARE(protocolAnimation->state(), QAbstractAnimation::Stopped);
        }
        QCoreApplication::processEvents();

        auto *scrollBar = page.m_sideScroll->verticalScrollBar();
        QTRY_VERIFY(scrollBar->maximum() > 0);
        const int scrollOffset = scrolled ? qMax(1, scrollBar->maximum() / 2) : 0;
        scrollBar->setValue(scrollOffset);
        QCoreApplication::processEvents();
        QCOMPARE(scrollBar->value(), scrollOffset);

        const int targetIndex = cards.indexOf(target);
        QVERIFY(targetIndex > 0 && targetIndex < cards.size() - 1);
        QList<QPoint> previousCardPositions;
        for (int index = 0; index < targetIndex; ++index) {
            previousCardPositions.append(cards.at(index)->pos());
        }
        const QPoint headerPosition = target->card()->mapTo(page.m_sidePanel, QPoint());
        const QPoint viewportHeaderPosition = target->card()->mapTo(page.m_sideScroll->viewport(), QPoint());

        auto *animation = target->findChild<QVariantAnimation *>(QStringLiteral("expandSettingCardAnimation"));
        QVERIFY(animation);
        QVERIFY(!target->isExpanded());
        target->setExpanded(true);
        QCOMPARE(animation->state(), QAbstractAnimation::Running);
        animation->pause();

        // Inspect each size change before queued layout events can repair a
        // compressed scroll panel and conceal a transient overlapping frame.
        for (const int time : {16, 32, 48, 80, 128, 176, 200}) {
            animation->setCurrentTime(qMin(time, animation->duration()));
            QCOMPARE(scrollBar->value(), scrollOffset);
            for (int index = 0; index < targetIndex; ++index) {
                QCOMPARE(cards.at(index)->pos(), previousCardPositions.at(index));
            }
            QCOMPARE(target->card()->mapTo(page.m_sidePanel, QPoint()), headerPosition);
            QCOMPARE(target->card()->mapTo(page.m_sideScroll->viewport(), QPoint()), viewportHeaderPosition);
            for (int index = 0; index + 1 < cards.size(); ++index) {
                const auto *upper = cards.at(index);
                const auto *lower = cards.at(index + 1);
                QVERIFY(lower->y() >= upper->y() + upper->height() + sideLayout->spacing());
            }
            QVERIFY(page.m_sidePanel->rect().contains(cards.last()->geometry()));
        }
    }

    void newPlotRequiresExplicitProtocolSelection()
    {
        AppPlot::ParserConfig previous;
        previous.protocol = AppPlot::Protocol::KeyValue;
        previous.fields = {QStringLiteral("temperature")};
        PlotParserDialog dialog(previous, nullptr, true);
        dialog.show();
        auto *combo = dialog.findChild<FluentQt::ComboBox *>(QStringLiteral("plotProtocolCombo"));
        auto *apply = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotApplyButton"));
        QVERIFY(combo && apply);
        QCOMPARE(combo->currentData().toString(), QString());
        QVERIFY(!apply->isEnabled());
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        apply->click();
        QCOMPARE(accepted.size(), 0);
        QVERIFY(dialog.isVisible());
        capture(dialog, QStringLiteral("plot-protocol-choice"));

        combo->setCurrentIndex(combo->findData(QStringLiteral("keyValue")));
        QVERIFY(apply->isEnabled());
        apply->click();
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(dialog.parserConfig().protocol, AppPlot::Protocol::KeyValue);
        QCOMPARE(dialog.parserConfig().fields, previous.fields);
    }

    void visualPreviewSupportsEveryParserType_data()
    {
        QTest::addColumn<int>("protocol");
        QTest::addColumn<double>("expectedValue");
        QTest::newRow("numbers") << int(AppPlot::Protocol::Numbers) << 24.8;
        QTest::newRow("delimited") << int(AppPlot::Protocol::Delimited) << 24.8;
        QTest::newRow("key-value") << int(AppPlot::Protocol::KeyValue) << 24.8;
        QTest::newRow("json") << int(AppPlot::Protocol::Json) << 24.8;
        QTest::newRow("binary") << int(AppPlot::Protocol::Binary) << 100.0;
    }

    void visualPreviewSupportsEveryParserType()
    {
        QFETCH(int, protocol);
        QFETCH(double, expectedValue);
        AppPlot::ParserConfig config;
        config.protocol = static_cast<AppPlot::Protocol>(protocol);
        PlotParserDialog dialog(config);
        dialog.show();
        auto *sample = dialog.findChild<FluentQt::PlainTextEdit *>(QStringLiteral("plotSampleEdit"));
        auto *example = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotLoadExampleButton"));
        auto *preview = dialog.findChild<FluentQt::TableWidget *>(QStringLiteral("plotPreviewTable"));
        auto *apply = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotApplyButton"));
        QVERIFY(sample && example && preview && apply);
        example->click();
        QVERIFY(!sample->toPlainText().isEmpty());
        QVERIFY(preview->rowCount() > 0);
        bool foundExpectedValue = false;
        for (int row = 0; row < preview->rowCount(); ++row) {
            bool validNumber = false;
            const double value = preview->item(row, 2)->text().toDouble(&validNumber);
            QVERIFY(validNumber);
            QVERIFY(!preview->item(row, 1)->text().isEmpty());
            foundExpectedValue |= qAbs(value - expectedValue) < 0.00001;
        }
        QVERIFY(foundExpectedValue);
        QVERIFY(apply->isEnabled());
        capture(dialog, QStringLiteral("plot-protocol-") + AppPlot::protocolKey(config.protocol));
        apply->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.parserConfig().protocol, config.protocol);
    }

    void fieldSelectionAndSampleEditsUpdatePreview()
    {
        AppPlot::ParserConfig config;
        config.protocol = AppPlot::Protocol::KeyValue;
        PlotParserDialog dialog(config);
        auto *sample = dialog.findChild<FluentQt::PlainTextEdit *>(QStringLiteral("plotSampleEdit"));
        auto *fields = dialog.findChild<FluentQt::LineEdit *>(QStringLiteral("plotFieldsEdit"));
        auto *preview = dialog.findChild<FluentQt::TableWidget *>(QStringLiteral("plotPreviewTable"));
        auto *useFields = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotUseFieldsButton"));
        QVERIFY(sample && fields && preview && useFields);
        sample->setPlainText(QStringLiteral("temperature=20 pressure=7"));
        QCOMPARE(preview->rowCount(), 2);
        int pressureRow = -1;
        for (int row = 0; row < preview->rowCount(); ++row) {
            if (preview->item(row, 1)->text() == QStringLiteral("pressure")) {
                pressureRow = row;
            }
        }
        QVERIFY(pressureRow >= 0);
        preview->selectRow(pressureRow);
        useFields->click();
        QCOMPARE(fields->text(), QStringLiteral("pressure"));
        QCOMPARE(preview->rowCount(), 1);
        QCOMPARE(preview->item(0, 1)->text(), QStringLiteral("pressure"));
        QCOMPARE(preview->item(0, 2)->text(), QStringLiteral("7"));
        sample->setPlainText(QStringLiteral("temperature=21 pressure=8"));
        QCOMPARE(preview->rowCount(), 1);
        QCOMPARE(preview->item(0, 2)->text(), QStringLiteral("8"));
    }

    void binaryPayloadPreviewRequiresTemplateAndRejectsBadFrames()
    {
        AppPlot::ParserConfig config;
        config.protocol = AppPlot::Protocol::Binary;
        config.binarySource = AppPlot::BinarySource::Payload;
        AppPlot::BinaryField field;
        field.name = QStringLiteral("value");
        config.binaryFields = {field};
        PlotParserDialog dialog(config);
        dialog.setProtocolTemplates(AppProtocol::defaultTemplates());
        dialog.show();
        auto *templates = dialog.findChild<FluentQt::ComboBox *>(QStringLiteral("plotFrameTemplateCombo"));
        auto *sample = dialog.findChild<FluentQt::PlainTextEdit *>(QStringLiteral("plotSampleEdit"));
        auto *preview = dialog.findChild<FluentQt::TableWidget *>(QStringLiteral("plotPreviewTable"));
        auto *apply = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotApplyButton"));
        auto *status = dialog.findChild<FluentQt::FluentLabelBase *>(QStringLiteral("plotPreviewStatus"));
        QVERIFY(templates && sample && preview && apply && status);
        QVERIFY(!apply->isEnabled());
        templates->setCurrentIndex(templates->findData(0));
        sample->setPlainText(QStringLiteral("AA 55 03 10 01 02 03 4D 6E"));
        QVERIFY(apply->isEnabled());
        QVERIFY(dialog.usesProtocolTemplate());
        QCOMPARE(dialog.protocolTemplate().name, AppProtocol::defaultTemplate().name);
        QCOMPARE(preview->rowCount(), 1);
        QCOMPARE(preview->item(0, 1)->text(), QStringLiteral("value"));
        QCOMPARE(preview->item(0, 2)->text(), QStringLiteral("1"));
        capture(dialog, QStringLiteral("plot-protocol-payload"));

        sample->setPlainText(QStringLiteral("AA 55 03 10 01 02 03 4D 6F"));
        QVERIFY(!apply->isEnabled());
        QCOMPARE(preview->rowCount(), 0);
        QVERIFY(status->text().contains(AppI18n::text("帧校验错误，请检查样例或模板。")));
        sample->setPlainText(QStringLiteral("AA 55 03 10 01"));
        QVERIFY(!apply->isEnabled());
        QCOMPARE(preview->rowCount(), 0);
        sample->setPlainText(QStringLiteral("AA ZZ"));
        QVERIFY(!apply->isEnabled());
        QVERIFY(status->text().contains(QStringLiteral("HEX")));
        sample->setPlainText(QStringLiteral("AA 55 03 10 01 02 03 4D 6E"));
        QVERIFY(apply->isEnabled());
        apply->click();
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
        QCOMPARE(dialog.parserConfig().binarySource, AppPlot::BinarySource::Payload);
    }

    void invalidBinaryFieldPreventsConfirmation()
    {
        AppPlot::ParserConfig config;
        config.protocol = AppPlot::Protocol::Binary;
        config.binaryFields = {AppPlot::BinaryField{QStringLiteral("temperature"), 0,
                                                  AppPlot::BinaryType::UInt16}};
        PlotParserDialog dialog(config);
        auto *fields = dialog.findChild<FluentQt::TableWidget *>(QStringLiteral("plotBinaryFieldsTable"));
        auto *apply = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotApplyButton"));
        QVERIFY(fields && apply);
        QVERIFY(apply->isEnabled());
        fields->item(0, 1)->setText(QStringLiteral("-1"));
        QVERIFY(!apply->isEnabled());
        fields->item(0, 1)->setText(QStringLiteral("2"));
        QVERIFY(apply->isEnabled());
        fields->item(0, 4)->setText(QStringLiteral("not a number"));
        QVERIFY(!apply->isEnabled());
    }

    void frameWindowVisualizesProtocolAndReportsMalformedSamples()
    {
        WorkbenchPage page(nullptr, false, false);
        page.showProtocolTemplateWindow();
        auto *window = page.m_protocolTemplateWindow;
        QVERIFY(window && window->isWindow() && window->isVisible());
        auto *structure = window->findChild<FluentQt::TextBrowser *>(QStringLiteral("protocolTemplateStructureView"));
        auto *table = window->findChild<FluentQt::TableView *>(QStringLiteral("protocolTemplateResultsTable"));
        auto *status = window->findChild<FluentQt::FluentLabelBase *>(QStringLiteral("protocolTemplateParseStatusLabel"));
        auto *sample = window->findChild<QPlainTextEdit *>(QStringLiteral("protocolTemplateSampleEdit"));
        QVERIFY(structure && table && status && sample);
        QCOMPARE(table->model()->rowCount(), 6);
        QCOMPARE(table->model()->data(table->model()->index(0, 2)).toString(), QStringLiteral("AA 55"));
        QCOMPARE(table->model()->data(table->model()->index(2, 2)).toString(), QStringLiteral("10"));
        QCOMPARE(table->model()->data(table->model()->index(3, 2)).toString(), QStringLiteral("01 02 03"));
        QVERIFY(status->text().contains(AppI18n::text("校验正确")));
        QVERIFY(structure->toPlainText().contains(QStringLiteral("AA")));
        QVERIFY(structure->toPlainText().contains(QStringLiteral("55")));
        capture(*window, QStringLiteral("frame-protocol-valid"));

        sample->setPlainText(QStringLiteral("AA 55 03 10 01 02 03 4D 6F"));
        QVERIFY(status->text().contains(AppI18n::text("校验错误")));
        capture(*window, QStringLiteral("frame-protocol-invalid-checksum"));
        sample->setPlainText(QStringLiteral("AA ZZ"));
        QCOMPARE(table->model()->rowCount(), 0);
        QVERIFY(status->text().contains(QStringLiteral("HEX")));
        QVERIFY(structure->toPlainText().isEmpty());
        sample->setPlainText(QStringLiteral("AA 55 03 10 01"));
        QCOMPARE(status->text(), AppProtocol::parseFrame(QByteArray::fromHex("AA55031001"),
                                                        AppProtocol::defaultTemplate()).errorMessage);
    }

    void frameWindowEditsSavesAndRestoresTemplateAndSample()
    {
        WorkbenchPage page(nullptr, false, false);
        page.showProtocolTemplateWindow();
        auto *window = page.m_protocolTemplateWindow;
        QVERIFY(window);
        auto *newButton = window->findChild<FluentQt::PushButton *>(QStringLiteral("protocolTemplateNewButton"));
        auto *name = window->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolTemplateNameEdit"));
        auto *header = window->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolTemplateHeaderEdit"));
        auto *offset = window->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolTemplatePayloadOffsetEdit"));
        auto *length = window->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolTemplatePayloadLengthEdit"));
        auto *save = window->findChild<FluentQt::PushButton *>(QStringLiteral("protocolTemplateSaveButton"));
        QVERIFY(newButton && name && header && offset && length && save);
        const int oldCount = page.m_protocolTemplates.size();
        newButton->click();
        QCOMPARE(page.m_protocolTemplates.size(), oldCount);
        name->setText(QStringLiteral("Saved visual protocol"));
        header->setText(QStringLiteral("AA BB"));
        offset->setText(QStringLiteral("2"));
        length->setText(QStringLiteral("2"));
        window->setSampleHex(QStringLiteral("AA BB 01 02"));
        QVERIFY(save->isEnabled());
        save->click();
        QCOMPARE(page.m_protocolTemplates.size(), oldCount + 1);
        QCOMPARE(page.m_protocolTemplates.last().name, QStringLiteral("Saved visual protocol"));
        QCOMPARE(page.m_protocolTemplates.last().header, QByteArray::fromHex("AABB"));
        QCOMPARE(page.m_protocolTemplates.last().payloadOffset, 2);
        QCOMPARE(page.m_protocolTemplates.last().payloadLength, 2);
        const int selected = page.m_protocolTemplateCombo->currentIndex();
        page.m_protocolTemplateCombo->setCurrentIndex(0);
        page.m_protocolTemplateCombo->setCurrentIndex(selected);
        QCOMPARE(window->sampleHex(), QStringLiteral("AA BB 01 02"));
        AppSettings settings;
        const QJsonDocument templates = QJsonDocument::fromJson(
            settings.value(QStringLiteral("protocolTemplate/templates")).toString().toUtf8());
        QCOMPARE(AppProtocol::listFromJson(templates.array()).last().name, QStringLiteral("Saved visual protocol"));
        const QJsonDocument examples = QJsonDocument::fromJson(
            settings.value(QStringLiteral("protocolTemplate/exampleFrames")).toString().toUtf8());
        QCOMPARE(examples.object().value(QStringLiteral("Saved visual protocol")).toString(),
                 QStringLiteral("AA BB 01 02"));
        WorkbenchPage restored(nullptr, false, false);
        restored.showProtocolTemplateWindow();
        QCOMPARE(restored.m_protocolNameEdit->text(), QStringLiteral("Saved visual protocol"));
        QCOMPARE(restored.m_protocolTemplateWindow->sampleHex(), QStringLiteral("AA BB 01 02"));
    }

    void invalidFrameDefinitionCannotBeSaved()
    {
        WorkbenchPage page(nullptr, false, false);
        page.showProtocolTemplateWindow();
        auto *window = page.m_protocolTemplateWindow;
        QVERIFY(window);
        const QJsonArray before = AppProtocol::listToJson(page.m_protocolTemplates);
        page.m_protocolHeaderEdit->setText(QStringLiteral("AA ZZ"));
        QVERIFY(!page.m_protocolSaveButton->isEnabled());
        page.saveCurrentProtocolTemplate();
        QCOMPARE(AppProtocol::listToJson(page.m_protocolTemplates), before);
        page.m_protocolHeaderEdit->setText(QStringLiteral("AA 55"));
        page.m_protocolPayloadOffsetEdit->setText(QStringLiteral("-1"));
        QVERIFY(!page.m_protocolSaveButton->isEnabled());
        page.saveCurrentProtocolTemplate();
        QCOMPARE(AppProtocol::listToJson(page.m_protocolTemplates), before);
        page.m_protocolPayloadOffsetEdit->setText(QStringLiteral("4"));
        QVERIFY(page.m_protocolSaveButton->isEnabled());
    }

    void excessiveFourByteLengthIsRejectedByBothProtocolPreviews_data()
    {
        QTest::addColumn<QByteArray>("frame");
        QTest::newRow("signed-boundary") << QByteArray::fromHex("AA557FFFFFFF");
        QTest::newRow("unsigned-boundary") << QByteArray::fromHex("AA55FFFFFFFF");
    }

    void excessiveFourByteLengthIsRejectedByBothProtocolPreviews()
    {
        QFETCH(QByteArray, frame);
        auto protocol = AppProtocol::defaultTemplate();
        protocol.lengthOffset = 2;
        protocol.lengthSize = 4;
        protocol.lengthByteOrder = AppChecksum::ByteOrder::BigEndian;
        protocol.payloadOffset = 6;
        protocol.commandSize = 0;
        protocol.checksumAlgorithm = QStringLiteral("none");
        const auto parsed = AppProtocol::parseFrame(frame, protocol);
        QVERIFY(!parsed.ok);
        QCOMPARE(parsed.errorMessage, AppI18n::text("解析出的帧长度无效"));

        ProtocolTemplateWindow window(new QWidget);
        window.setProtocolTemplate(protocol);
        window.setSampleHex(bytesToHex(frame));
        window.show();
        auto *status = window.findChild<FluentQt::FluentLabelBase *>(QStringLiteral("protocolTemplateParseStatusLabel"));
        QVERIFY(status);
        QCOMPARE(status->text(), parsed.errorMessage);

        AppPlot::ParserConfig config;
        config.protocol = AppPlot::Protocol::Binary;
        config.binarySource = AppPlot::BinarySource::Payload;
        PlotParserDialog dialog(config);
        dialog.setProtocolTemplate(protocol);
        auto *sample = dialog.findChild<FluentQt::PlainTextEdit *>(QStringLiteral("plotSampleEdit"));
        auto *apply = dialog.findChild<FluentQt::PushButton *>(QStringLiteral("plotApplyButton"));
        auto *plotStatus = dialog.findChild<FluentQt::FluentLabelBase *>(QStringLiteral("plotPreviewStatus"));
        QVERIFY(sample && apply && plotStatus);
        sample->setPlainText(bytesToHex(frame));
        QVERIFY(!apply->isEnabled());
        QCOMPARE(plotStatus->text(), parsed.errorMessage);
    }
};

QTEST_MAIN(ProtocolWindowsTest)
#include "tst_protocol_windows.moc"
