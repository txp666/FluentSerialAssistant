#pragma once
#include "app/core/protocol_template.h"
#include <QtCore/QHash>
#include <QtWidgets/QWidget>
class QPlainTextEdit;
class QStandardItemModel;
class QStackedWidget;
class ProtocolPayloadEditor;
namespace FluentQt {
class BodyLabel;
class CaptionLabel;
class ComboBox;
class PushButton;
class TableView;
class TextBrowser;
class SingleDirectionScrollArea;
} // namespace FluentQt
class ProtocolTemplateWindow : public QWidget
{
    Q_OBJECT
  public:
    explicit ProtocolTemplateWindow(QWidget *editor, QWidget *parent = nullptr,
                                    ProtocolPayloadEditor *payloadEditor = nullptr);
    void setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate);
    void setConfigurationError(const QString &error);
    void setSampleHex(const QString &hex);
    QString sampleHex() const;
    void setLastFrame(const QByteArray &frame);
    void generateMatchingSample();
    void refreshGeneratedSample();

  signals:
    void createPlotRequested();

  protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void updatePreview();
    void updateResultColumnWidths();
    void selectSection(const QString &kind, int field = -1);
    void updateInspector();
    AppProtocol::ProtocolTemplate m_protocolTemplate;
    QString m_configurationError;
    QByteArray m_lastFrame;
    QPlainTextEdit *m_sampleEdit = nullptr;
    FluentQt::TextBrowser *m_structureView = nullptr;
    FluentQt::TableView *m_resultTable = nullptr;
    QStandardItemModel *m_resultModel = nullptr;
    FluentQt::BodyLabel *m_templateTitle = nullptr;
    FluentQt::CaptionLabel *m_validationLabel = nullptr;
    FluentQt::CaptionLabel *m_parseStatusLabel = nullptr;
    FluentQt::PushButton *m_lastFrameButton = nullptr;
    FluentQt::PushButton *m_removeFieldButton = nullptr;
    FluentQt::PushButton *m_createPlotButton = nullptr;
    FluentQt::ComboBox *m_sectionCombo = nullptr;
    QList<FluentQt::PushButton *> m_addFieldButtons;
    ProtocolPayloadEditor *m_payloadEditor = nullptr;
    FluentQt::SingleDirectionScrollArea *m_editorScroll = nullptr;
    QStackedWidget *m_inspector = nullptr;
    QHash<QString, QWidget *> m_frameGroups;
    QString m_selectedKind = QStringLiteral("header");
    int m_selectedField = -1;
    bool m_internalPayloadEditor = false;
    bool m_refreshing = false;
    bool m_generatedSample = true;
};
