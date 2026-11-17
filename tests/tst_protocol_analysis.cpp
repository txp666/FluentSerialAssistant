#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/core/hex_utils.h"
#include "app/view/protocol_analysis_window.h"
#include "app/view/protocol_payload_editor.h"
#include "app/view/protocol_template_window.h"
#include "app/view/workbench_page.h"

#include <FluentQtWidgets/FluentQtWidgets.h>
#include <QtCore/QAbstractItemModel>
#include <QtCore/QDir>
#include <QtCore/QMimeData>
#include <QtTest/QTest>
#include <memory>

class ProtocolAnalysisTest : public QObject
{
    Q_OBJECT

    static AppProtocol::ProtocolTemplate motorTemplate()
    {
        auto item = AppProtocol::defaultTemplate();
        item.name = QStringLiteral("Motors");
        item.commandOffset = 4;
        item.payloadOffset = 5;
        item.addressOffset = 3;
        item.addressSize = 1;
        item.payloadFields = {
            {QStringLiteral("Angle"), 0, AppPlot::BinaryType::Int16, AppPlot::ByteOrder::LittleEndian, 0.01, 0},
            {QStringLiteral("Speed"), 2, AppPlot::BinaryType::UInt16},
            {QStringLiteral("Status"), 4, AppPlot::BinaryType::UInt8}};
        return item;
    }

    static QByteArray motorFrame(int address, qint16 angle = 7033, quint16 speed = 120,
                                 const QByteArray &header = QByteArray::fromHex("AA55"))
    {
        QByteArray frame = header;
        frame.append(char(5));
        frame.append(char(address));
        frame.append(char(0x10));
        frame.append(char(angle & 0xff));
        frame.append(char((quint16(angle) >> 8) & 0xff));
        frame.append(char(speed & 0xff));
        frame.append(char(speed >> 8));
        frame.append(char(1));
        frame +=
            AppChecksum::calculate(frame, QStringLiteral("crc16-modbus"), AppChecksum::ByteOrder::LittleEndian).bytes;
        return frame;
    }

    static FluentQt::TableWidget *values(ProtocolAnalysisWindow &window)
    {
        return window.findChild<FluentQt::TableWidget *>(QStringLiteral("protocolAnalysisValues"));
    }

    static FluentQt::RealtimePlotWidget *plot(ProtocolAnalysisWindow &window)
    {
        return window.findChild<FluentQt::RealtimePlotWidget *>(QStringLiteral("protocolAnalysisPlot"));
    }

    static void start(ProtocolAnalysisWindow &window)
    {
        window.setProtocolTemplates({motorTemplate()}, QStringLiteral("Motors"));
        window.setParsingEnabled(true);
        window.show();
    }

  private slots:
    void initTestCase()
    {
        Q_INIT_RESOURCE(app);
        Q_INIT_RESOURCE(fluentqtwidgets);
        FluentQt::FluentConfig::instance()->setFileName(
            QDir(AppSettings::directoryPath()).filePath(QStringLiteral("fluent.json")));
        AppI18n::installTranslators(qApp, QStringLiteral("zh_CN"));
        FluentQt::ThemeManager::instance()->setTheme(FluentQt::Theme::Light);
    }

    void init()
    {
        AppSettings settings;
        settings.clear();
    }

    void templatesKeepFieldsAndRemainBackwardCompatible()
    {
        const auto item = motorTemplate();
        const auto restored = AppProtocol::fromJson(AppProtocol::toJson(item));
        QVERIFY(restored == item);
        auto modified = restored;
        modified.payloadFields[0].scale = 0.1;
        QVERIFY(!(modified == item));
        auto oldJson = AppProtocol::toJson(AppProtocol::defaultTemplate());
        oldJson.remove(QStringLiteral("payloadFields"));
        oldJson.remove(QStringLiteral("addressOffset"));
        oldJson.remove(QStringLiteral("addressSize"));
        QVERIFY(AppProtocol::fromJson(oldJson) == AppProtocol::defaultTemplate());
        const auto frame = motorFrame(2, -5904);
        const auto parsed = AppProtocol::parseFrame(frame, restored);
        QVERIFY(parsed.ok && parsed.checksumValid);
        const auto decoded =
            AppPlot::extractSamples(AppProtocol::payloadParserConfig(restored), {}, frame, parsed.payload);
        QCOMPARE(decoded.size(), 1);
        QCOMPARE(decoded.first().size(), 3);
        QCOMPARE(decoded.first().first().value, -59.04);
    }

    void rejectsAmbiguousNamesAndInvalidFieldRanges()
    {
        auto item = motorTemplate();
        QVERIFY(AppProtocol::payloadConfigurationError(item).isEmpty());
        item.payloadFields[1].name = item.payloadFields.first().name;
        QVERIFY(!AppProtocol::payloadConfigurationError(item).isEmpty());
        item.payloadFields[1].name.clear();
        QVERIFY(!AppProtocol::payloadConfigurationError(item).isEmpty());
        item = motorTemplate();
        item.payloadLength = 4;
        QVERIFY(!AppProtocol::payloadConfigurationError(item).isEmpty());
        item.payloadFields.removeLast();
        QVERIFY(AppProtocol::payloadConfigurationError(item).isEmpty());
        item.addressOffset = -1;
        QVERIFY(!AppProtocol::payloadConfigurationError(item).isEmpty());
    }

    void editorSavesAndRestoresFieldDefinitions()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_protocolTemplates = {motorTemplate()};
        page.updateProtocolTemplateCombo(0);
        auto *editor = page.m_protocolPayloadEditor;
        auto *name = editor->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolPayloadFieldName"));
        auto *scale = editor->findChild<FluentQt::DoubleSpinBox *>(QStringLiteral("protocolPayloadFieldScale"));
        QVERIFY(name && scale);
        name->setText(QStringLiteral("Position"));
        scale->setValue(0.02);
        page.saveCurrentProtocolTemplate();
        WorkbenchPage restored(nullptr, false, false);
        restored.loadProtocolTemplates();
        const auto saved = restored.currentProtocolTemplateFromUi();
        QCOMPARE(saved.name, QStringLiteral("Motors"));
        QCOMPARE(saved.payloadFields.size(), 3);
        QCOMPARE(saved.payloadFields.first().name, QStringLiteral("Position"));
        QCOMPARE(saved.payloadFields.first().scale, 0.02);
        QCOMPARE(saved.addressOffset, 3);
        QCOMPARE(saved.addressSize, 1);
    }

    void draggingFieldsReordersPayloadAndPreservesGaps()
    {
        WorkbenchPage page(nullptr, false, false);
        auto definition = motorTemplate();
        definition.payloadFields[0].byteOffset = 1;
        definition.payloadFields[1].byteOffset = 5;
        definition.payloadFields[1].type = AppPlot::BinaryType::UInt32;
        definition.payloadFields[2].byteOffset = 10;
        page.m_protocolTemplates = {definition};
        page.updateProtocolTemplateCombo(0);
        page.showProtocolTemplateWindow();
        auto *window = page.m_protocolTemplateWindow;
        auto *table = window->findChild<FluentQt::TableView *>(QStringLiteral("protocolTemplateResultsTable"));
        auto *model = table->model();
        const auto fieldRow = [model](const QString &name) {
            for (int row = 0; row < model->rowCount(); ++row)
                if (model->index(row, 0).data().toString() == name)
                    return row;
            return -1;
        };
        const int angleRow = fieldRow(QStringLiteral("Angle"));
        const int statusRow = fieldRow(QStringLiteral("Status"));
        QVERIFY(angleRow >= 0 && statusRow >= 0);
        QVERIFY(!(model->flags(model->index(0, 0)) & Qt::ItemIsDragEnabled));
        std::unique_ptr<QMimeData> drag(model->mimeData({model->index(statusRow, 0)}));
        QVERIFY(!model->canDropMimeData(drag.get(), Qt::MoveAction, 0, 0, {}));
        QVERIFY(model->dropMimeData(drag.get(), Qt::MoveAction, angleRow, 0, {}));
        const auto reordered = page.currentProtocolTemplateFromUi();
        QCOMPARE(reordered.payloadFields[0].name, QStringLiteral("Status"));
        QCOMPARE(reordered.payloadFields[0].byteOffset, 1);
        QCOMPARE(reordered.payloadFields[1].name, QStringLiteral("Angle"));
        QCOMPARE(reordered.payloadFields[1].byteOffset, 4);
        QCOMPARE(reordered.payloadFields[1].scale, 0.01);
        QCOMPARE(reordered.payloadFields[2].name, QStringLiteral("Speed"));
        QCOMPARE(reordered.payloadFields[2].byteOffset, 7);
        QCOMPARE(table->currentIndex().siblingAtColumn(0).data().toString(), QStringLiteral("Status"));
        const auto parsed = AppProtocol::parseFrame(parseHexPayload(window->sampleHex()).bytes, reordered);
        QVERIFY(parsed.ok && parsed.checksumValid);
        QCOMPARE(parsed.payload.size(), 11);

        drag.reset(model->mimeData({model->index(fieldRow(QStringLiteral("Status")), 0)}));
        QVERIFY(model->dropMimeData(drag.get(), Qt::MoveAction, fieldRow(QStringLiteral("Speed")) + 1, 0, {}));
        QVERIFY(page.currentProtocolTemplateFromUi() == definition);
        QCOMPARE(table->currentIndex().siblingAtColumn(0).data().toString(), QStringLiteral("Status"));
    }

    void enablingProtocolOpensAnalysisAndUsesSelectedTemplate()
    {
        WorkbenchPage page(nullptr, false, false);
        page.m_protocolTemplates = {motorTemplate()};
        page.updateProtocolTemplateCombo(0);
        page.m_protocolEnabledCheck->setChecked(true);
        auto *window = page.m_protocolAnalysisWindow;
        QVERIFY(window && window->isVisible());
        QVERIFY(!QApplication::activeModalWidget());
        page.appendRecord(WorkbenchPage::RecordDirection::Tx, motorFrame(3), false);
        QCOMPARE(values(*window)->rowCount(), 0);
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, motorFrame(1), false);
        QCOMPARE(values(*window)->rowCount(), 3);
        QTRY_COMPARE(values(*window)->item(0, 3)->text(), QStringLiteral("70.33"));
        page.showProtocolAnalysisWindow();
        QCOMPARE(page.m_protocolAnalysisWindow, window);
        QCOMPARE(values(*window)->rowCount(), 3);
        auto *enabled = window->findChild<FluentQt::CheckBox *>(QStringLiteral("protocolAnalysisEnabled"));
        enabled->setChecked(false);
        QVERIFY(!page.m_protocolEnabledCheck->isChecked());
        page.appendRecord(WorkbenchPage::RecordDirection::Rx, motorFrame(2), false);
        QCOMPARE(values(*window)->rowCount(), 3);
    }

    void separatesAddressesAndPlotsOnOneTimeAxis()
    {
        ProtocolAnalysisWindow window;
        start(window);
        const auto now = QDateTime::currentDateTime();
        window.appendRecord(now, motorFrame(1, 7033));
        window.appendRecord(now.addMSecs(10), motorFrame(2, -5904));
        auto *table = values(window);
        QCOMPARE(table->rowCount(), 6);
        QTRY_COMPARE(table->item(0, 3)->text(), QStringLiteral("70.33"));
        QTRY_COMPARE(table->item(3, 3)->text(), QStringLiteral("-59.04"));
        QVERIFY(table->item(0, 1)->text().endsWith(QStringLiteral("01")));
        QVERIFY(table->item(3, 1)->text().endsWith(QStringLiteral("02")));
        table->item(0, 0)->setCheckState(Qt::Checked);
        table->item(3, 0)->setCheckState(Qt::Checked);
        QCOMPARE(plot(window)->seriesCount(), 2);
        QCOMPARE(plot(window)->points(0).first().y(), 70.33);
        QCOMPARE(plot(window)->points(1).first().y(), -59.04);
        window.appendRecord(now.addSecs(1), motorFrame(1, 7100));
        window.appendRecord(now.addMSecs(1010), motorFrame(2, -5800));
        QCOMPARE(plot(window)->points(0).size(), 2);
        QCOMPARE(plot(window)->points(1).size(), 2);
        QCOMPARE(plot(window)->points(0).last(), QPointF(1.0, 71.0));
        QCOMPARE(plot(window)->points(1).last(), QPointF(1.01, -58.0));
        table->item(3, 0)->setCheckState(Qt::Unchecked);
        QVERIFY(!plot(window)->isSeriesVisible(1));
        table->item(3, 0)->setCheckState(Qt::Checked);
        QCOMPARE(plot(window)->points(1).size(), 2);
    }

    void supportsSeveralTemplatesWithoutCrossMatching()
    {
        ProtocolAnalysisWindow window;
        auto second = motorTemplate();
        second.name = QStringLiteral("Sensors");
        second.header = QByteArray::fromHex("BB66");
        window.setProtocolTemplates({motorTemplate(), second}, QStringLiteral("Motors"));
        window.setParsingEnabled(true);
        window.show();
        auto *templates = window.findChild<FluentQt::TableWidget *>(QStringLiteral("protocolAnalysisTemplates"));
        templates->item(1, 0)->setCheckState(Qt::Checked);
        const auto now = QDateTime::currentDateTime();
        window.appendRecord(now, motorFrame(1, 123));
        window.appendRecord(now, motorFrame(1, 456, 120, second.header));
        QCOMPARE(values(window)->rowCount(), 6);
        QTRY_COMPARE(values(window)->item(0, 3)->text(), QStringLiteral("1.23"));
        QTRY_COMPARE(values(window)->item(3, 3)->text(), QStringLiteral("4.56"));
        templates->item(0, 0)->setCheckState(Qt::Unchecked);
        window.appendRecord(now, motorFrame(1));
        QCOMPARE(values(window)->rowCount(), 0);
        window.appendRecord(now, motorFrame(1, 456, 120, second.header));
        QCOMPARE(values(window)->rowCount(), 3);
    }

    void reassemblesRawReadsAndDiscardsBadChecksums()
    {
        ProtocolAnalysisWindow window;
        start(window);
        const auto now = QDateTime::currentDateTime();
        const auto first = motorFrame(1);
        const auto second = motorFrame(2, -5904);
        window.appendRecord(now, QByteArray::fromHex("0000") + first.left(1), true);
        window.appendRecord(now, first.mid(1, 6), true);
        QCOMPARE(values(window)->rowCount(), 0);
        window.appendRecord(now, first.mid(7) + second, true);
        QCOMPARE(values(window)->rowCount(), 6);
        auto bad = motorFrame(3);
        bad[bad.size() - 1] ^= 0x01;
        window.appendRecord(now, bad + motorFrame(4), true);
        QCOMPARE(values(window)->rowCount(), 9);
        QVERIFY(values(window)->item(6, 1)->text().endsWith(QStringLiteral("04")));
    }

    void endedRecordsRecoverAfterAnIncompleteLengthCandidate()
    {
        auto item = motorTemplate();
        item.lengthSize = 2;
        item.commandOffset = 4;
        item.addressOffset = 5;
        item.payloadOffset = 6;
        const auto good = AppProtocol::exampleFrame(item);
        auto damaged = good;
        damaged[2] = char(0xff);
        damaged[3] = char(0xff);
        const auto bytes = damaged + good + good;
        QByteArray buffer;
        // Without a boundary, this could still be the payload of a longer frame.
        QVERIFY(AppProtocol::takeFrames(buffer, bytes, item, true).isEmpty());
        QCOMPARE(buffer, bytes);
        const auto decoded = AppProtocol::takeFrames(buffer, bytes, item, false);
        QCOMPARE(decoded.size(), 2);
        QCOMPARE(decoded[0].bytes, good);
        QCOMPARE(decoded[1].bytes, good);
        QVERIFY(buffer.isEmpty());
    }

    void partialOuterFrameKeepsAValidFrameInsideItsPayload()
    {
        const auto item = motorTemplate();
        const auto inner = motorFrame(1);
        auto outer = QByteArray::fromHex("AA55000110") + inner + QByteArray::fromHex("1234");
        outer[2] = char(inner.size() + 2);
        outer += AppChecksum::calculate(outer, item.checksumAlgorithm, item.checksumByteOrder).bytes;
        const int split = item.payloadOffset + inner.size();
        QByteArray buffer;
        QVERIFY(AppProtocol::takeFrames(buffer, outer.left(split), item, true).isEmpty());
        const auto decoded = AppProtocol::takeFrames(buffer, outer.mid(split), item, true);
        QCOMPARE(decoded.size(), 1);
        QCOMPARE(decoded.first().bytes, outer);
        const auto ended = AppProtocol::takeFrames(buffer, outer, item, false);
        QCOMPARE(ended.size(), 1);
        QCOMPARE(ended.first().bytes, outer);
    }

    void pausesHiddenWindowsAndClearsPartialReads()
    {
        ProtocolAnalysisWindow window;
        start(window);
        const auto now = QDateTime::currentDateTime();
        const auto first = motorFrame(1);
        window.appendRecord(now, first.left(6), true);
        auto *pause = window.findChild<FluentQt::PushButton *>(QStringLiteral("protocolAnalysisPause"));
        pause->click();
        window.appendRecord(now, first, true);
        QCOMPARE(values(window)->rowCount(), 0);
        QCOMPARE(plot(window)->refreshRate(), 0);
        pause->click();
        window.appendRecord(now, first.mid(6), true);
        QCOMPARE(values(window)->rowCount(), 0);
        window.hide();
        window.appendRecord(now, first);
        QCOMPARE(values(window)->rowCount(), 0);
        window.show();
        window.appendRecord(now, first);
        QCOMPARE(values(window)->rowCount(), 3);
        values(window)->item(0, 0)->setCheckState(Qt::Checked);
        window.clearData();
        QCOMPARE(values(window)->item(0, 3)->text(), QStringLiteral("—"));
        QCOMPARE(values(window)->item(0, 0)->checkState(), Qt::Checked);
        QCOMPARE(plot(window)->points(0).size(), 0);
    }

    void reloadsCurveChoicesAndSavedTemplateChanges()
    {
        ProtocolAnalysisWindow first;
        start(first);
        const auto now = QDateTime::currentDateTime();
        first.appendRecord(now, motorFrame(1));
        values(first)->item(0, 0)->setCheckState(Qt::Checked);
        first.close();
        ProtocolAnalysisWindow second;
        start(second);
        second.appendRecord(now, motorFrame(1));
        QCOMPARE(values(second)->item(0, 0)->checkState(), Qt::Checked);
        QCOMPARE(plot(second)->points(0).size(), 1);
        auto changed = motorTemplate();
        changed.payloadFields[0].scale = 0.02;
        second.setProtocolTemplates({changed});
        QCOMPARE(values(second)->rowCount(), 0);
        second.appendRecord(now, motorFrame(1));
        QTRY_COMPARE(values(second)->item(0, 3)->text(), QStringLiteral("140.66"));
        QCOMPARE(plot(second)->points(0).size(), 1);
    }

    void missingPayloadFieldsDoNotKeepOldMeasurements()
    {
        ProtocolAnalysisWindow window;
        start(window);
        const auto now = QDateTime::currentDateTime();
        window.appendRecord(now, motorFrame(1));
        auto shortFrame = motorFrame(1).left(7);
        shortFrame[2] = char(2);
        shortFrame +=
            AppChecksum::calculate(shortFrame, QStringLiteral("crc16-modbus"), AppChecksum::ByteOrder::LittleEndian)
                .bytes;
        window.appendRecord(now, shortFrame);
        QTRY_COMPARE(values(window)->item(1, 3)->text(), QStringLiteral("—"));
        QTRY_COMPARE(values(window)->item(2, 3)->text(), QStringLiteral("—"));
        QTRY_COMPARE(values(window)->item(0, 3)->text(), QStringLiteral("70.33"));
    }

    void boundsDeviceRowsAndSimultaneousCurves()
    {
        ProtocolAnalysisWindow window;
        start(window);
        const auto now = QDateTime::currentDateTime();
        for (int address = 0; address < 90; ++address) {
            window.appendRecord(now, motorFrame(address));
        }
        QCOMPARE(values(window)->rowCount(), 256);
        for (int row = 0; row <= AppPlot::MaximumChannels; ++row) {
            values(window)->item(row, 0)->setCheckState(Qt::Checked);
        }
        QCOMPARE(values(window)->item(AppPlot::MaximumChannels, 0)->checkState(), Qt::Unchecked);
        QCOMPARE(plot(window)->seriesCount(), AppPlot::MaximumChannels);
    }

    void editorReportsMissingBytesAndUpdatesSelectedField()
    {
        WorkbenchPage page(nullptr, false, false);
        auto definition = motorTemplate();
        definition.name = QStringLiteral("虚拟总线");
        definition.commandSize = 0;
        definition.payloadOffset = 4;
        definition.payloadFields[0].name = QStringLiteral("角度");
        definition.payloadFields[1].name = QStringLiteral("转速");
        definition.payloadFields[2].name = QStringLiteral("状态");
        page.m_protocolTemplates = {definition};
        page.updateProtocolTemplateCombo(0);
        auto *window = page.m_protocolTemplateWindow;
        window->resize(1100, 760);
        page.showProtocolTemplateWindow();
        window->setSampleHex(QStringLiteral("AA 55 03 10 01 02 03 4D 6E"));
        auto *validation =
            window->findChild<FluentQt::FluentLabelBase *>(QStringLiteral("protocolTemplateValidationLabel"));
        auto *table = window->findChild<FluentQt::TableView *>(QStringLiteral("protocolTemplateResultsTable"));
        auto *name = window->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolPayloadFieldName"));
        auto *scale = window->findChild<FluentQt::DoubleSpinBox *>(QStringLiteral("protocolPayloadFieldScale"));
        auto *type = window->findChild<FluentQt::ComboBox *>(QStringLiteral("protocolPayloadFieldType"));
        QVERIFY(validation && table && name && scale && type);
        QCOMPARE(validation->text(), AppI18n::text("样例载荷不足：当前 %1 B，数据字段至少需要 %2 B。").arg(3).arg(5));
        QCOMPARE(table->model()->rowCount(), 7);
        const auto fieldRow = [table](int field) {
            for (int row = 0; row < table->model()->rowCount(); ++row) {
                const auto index = table->model()->index(row, 0);
                if (index.data(Qt::UserRole).toString() == QStringLiteral("field") &&
                    index.data(Qt::UserRole + 1).toInt() == field)
                    return row;
            }
            return -1;
        };
        const int speedRow = fieldRow(1);
        QVERIFY(speedRow >= 0);
        QCOMPARE(table->model()->index(speedRow, 4).data().toString(), AppI18n::text("无法解析"));
        QCOMPARE(table->model()->index(speedRow, 5).data().toString(), AppI18n::text("缺少 %1 B").arg(1));
        table->selectRow(speedRow);
        QCOMPARE(name->text(), QStringLiteral("转速"));
        QVERIFY(type->isVisible() && scale->isVisible());
        QVERIFY(window->rect().contains(scale->mapTo(window, scale->rect().bottomRight())));
        table->selectRow(fieldRow(0));
        QCOMPARE(name->text(), QStringLiteral("角度"));
        QCOMPARE(scale->value(), 0.01);
        const QString capturedSample = window->sampleHex();
        name->setText(QStringLiteral("角度改名"));
        QCOMPARE(window->sampleHex(), capturedSample);
        name->setText(QStringLiteral("角度"));
        scale->setValue(0.02);
        QCOMPARE(table->model()->index(fieldRow(0), 4).data().toString(), QStringLiteral("10.26"));
        QCOMPARE(window->sampleHex(), capturedSample);
        window->findChild<FluentQt::PlainTextEdit *>(QStringLiteral("protocolTemplateSampleEdit"))->clear();
        scale->setValue(0.01);
        const auto frame = parseHexPayload(window->sampleHex());
        const auto parsed = AppProtocol::parseFrame(frame.bytes, definition);
        QVERIFY(frame.ok && parsed.ok && parsed.checksumValid);
        QCOMPARE(parsed.payload.size(), 5);
        QCOMPARE(table->model()->index(fieldRow(0), 4).data().toString(), QStringLiteral("12.34"));
        QCOMPARE(table->model()->index(fieldRow(1), 4).data().toString(), QStringLiteral("200"));
        QCOMPARE(table->model()->index(fieldRow(2), 4).data().toString(), QStringLiteral("1"));
        QCOMPARE(table->model()->index(2, 4).data().toString(), QStringLiteral("01"));
        auto *templateName = window->findChild<FluentQt::LineEdit *>(QStringLiteral("protocolTemplateNameEdit"));
        QVERIFY(templateName && templateName->isVisible());
        templateName->setText(QStringLiteral("临时名称"));
        QVERIFY(templateName->isVisible());
        templateName->setText(definition.name);
        table->selectRow(fieldRow(0));
        auto *add = window->findChild<FluentQt::PushButton *>(QStringLiteral("protocolPayloadAdd-u16"));
        auto *remove = window->findChild<FluentQt::PushButton *>(QStringLiteral("protocolPayloadRemoveField"));
        QVERIFY(add && remove);
        add->click();
        QCOMPARE(page.currentProtocolTemplateFromUi().payloadFields.last().type, AppPlot::BinaryType::UInt16);
        QCOMPARE(page.currentProtocolTemplateFromUi().payloadFields.last().byteOffset, 5);
        QCOMPARE(
            AppProtocol::parseFrame(parseHexPayload(window->sampleHex()).bytes, page.currentProtocolTemplateFromUi())
                .payload.size(),
            7);
        remove->click();
        QCOMPARE(page.currentProtocolTemplateFromUi().payloadFields.size(), 3);
        QCOMPARE(
            AppProtocol::parseFrame(parseHexPayload(window->sampleHex()).bytes, page.currentProtocolTemplateFromUi())
                .payload.size(),
            5);
    }

    void generatedSampleIncludesBigEndianFloatAfterAGap()
    {
        auto definition = motorTemplate();
        definition.payloadFields = {
            {QStringLiteral("value"), 8, AppPlot::BinaryType::Float32, AppPlot::ByteOrder::BigEndian}};
        const auto frame = AppProtocol::exampleFrame(definition);
        const auto parsed = AppProtocol::parseFrame(frame, definition);
        QVERIFY(parsed.ok && parsed.checksumValid);
        QCOMPARE(parsed.payload.size(), 12);
        QCOMPARE(parsed.payload.mid(8), QByteArray::fromHex("41480000"));
        const auto samples =
            AppPlot::extractSamples(AppProtocol::payloadParserConfig(definition), {}, frame, parsed.payload);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples.first().size(), 1);
        QCOMPARE(samples.first().first().value, 12.5);
    }
};

QTEST_MAIN(ProtocolAnalysisTest)
#include "tst_protocol_analysis.moc"
