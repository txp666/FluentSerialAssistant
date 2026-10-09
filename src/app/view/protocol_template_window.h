#pragma once

#include "app/core/protocol_template.h"

#include <QtWidgets/QWidget>

class QPlainTextEdit;
class QStandardItemModel;

namespace FluentQt {
class BodyLabel;
class CaptionLabel;
class PushButton;
class TableView;
class TextBrowser;
} // namespace FluentQt

class ProtocolTemplateWindow : public QWidget
{
    Q_OBJECT

  public:
    explicit ProtocolTemplateWindow(QWidget *editor, QWidget *parent = nullptr);

    void setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate);
    void setConfigurationError(const QString &error);
    void setSampleHex(const QString &hex);
    QString sampleHex() const;
    void setLastFrame(const QByteArray &frame);

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    void updatePreview();
    void useExampleFrame();

    AppProtocol::ProtocolTemplate m_protocolTemplate;
    QString m_configurationError;
    QByteArray m_lastFrame;
    bool m_manualSample = false;
    QPlainTextEdit *m_sampleEdit = nullptr;
    FluentQt::TextBrowser *m_structureView = nullptr;
    FluentQt::TableView *m_resultTable = nullptr;
    QStandardItemModel *m_resultModel = nullptr;
    FluentQt::BodyLabel *m_templateTitle = nullptr;
    FluentQt::CaptionLabel *m_validationLabel = nullptr;
    FluentQt::CaptionLabel *m_parseStatusLabel = nullptr;
    FluentQt::PushButton *m_lastFrameButton = nullptr;
};
