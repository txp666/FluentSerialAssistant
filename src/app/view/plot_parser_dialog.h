#pragma once

#include "app/core/plot_value_parser.h"
#include "app/core/protocol_template.h"

#include <QtWidgets/QDialog>

class QWidget;

namespace FluentQt {
class ComboBox;
class LineEdit;
class CaptionLabel;
class PlainTextEdit;
class PrimaryPushButton;
class TableWidget;
} // namespace FluentQt

class PlotParserDialog : public QDialog
{
    Q_OBJECT

  public:
    explicit PlotParserDialog(const AppPlot::ParserConfig &config, QWidget *parent = nullptr,
                              bool requireProtocolChoice = false);

    AppPlot::ParserConfig parserConfig() const;
    void setProtocolTemplates(const QList<AppProtocol::ProtocolTemplate> &protocolTemplates);
    void setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate);
    bool usesProtocolTemplate() const;
    AppProtocol::ProtocolTemplate protocolTemplate() const;

  protected:
    void accept() override;

  private:
    void addBinaryFieldRow(const AppPlot::BinaryField &field);
    void addDefaultBinaryField();
    void removeSelectedBinaryFields();
    bool collectBinaryFields(QVector<AppPlot::BinaryField> *fields, QString *error) const;
    bool collectConfig(AppPlot::ParserConfig *config, QString *error) const;
    void updateProtocolControls();
    void updatePreview();
    void loadExample();
    void useSelectedFields();

    AppPlot::ParserConfig m_config;
    QList<AppProtocol::ProtocolTemplate> m_protocolTemplates;
    FluentQt::ComboBox *m_protocolCombo = nullptr;
    FluentQt::LineEdit *m_fieldsEdit = nullptr;
    FluentQt::ComboBox *m_binarySourceCombo = nullptr;
    FluentQt::ComboBox *m_templateCombo = nullptr;
    FluentQt::ComboBox *m_sampleModeCombo = nullptr;
    FluentQt::TableWidget *m_binaryFieldsTable = nullptr;
    FluentQt::TableWidget *m_previewTable = nullptr;
    FluentQt::PlainTextEdit *m_sampleEdit = nullptr;
    FluentQt::CaptionLabel *m_protocolHint = nullptr;
    FluentQt::CaptionLabel *m_previewStatus = nullptr;
    FluentQt::PrimaryPushButton *m_applyButton = nullptr;
    QWidget *m_binaryContainer = nullptr;
    QWidget *m_templateContainer = nullptr;
    bool m_updatingPreview = false;
};
