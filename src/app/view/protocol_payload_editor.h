#pragma once
#include "app/core/protocol_template.h"
#include <QtWidgets/QWidget>
namespace FluentQt {
class CaptionLabel;
class ComboBox;
class DoubleSpinBox;
class LineEdit;
class SpinBox;
class StrongBodyLabel;
} // namespace FluentQt
class ProtocolPayloadEditor : public QWidget
{
    Q_OBJECT
  public:
    explicit ProtocolPayloadEditor(QWidget *parent = nullptr);
    void setProtocolTemplate(const AppProtocol::ProtocolTemplate &protocolTemplate);
    void applyTo(AppProtocol::ProtocolTemplate &protocolTemplate) const;
    void setPayloadOffset(int offset);
    void selectPayloadField(int index);
    void setAddressEditing(bool address);
    void addField(AppPlot::BinaryType type = AppPlot::BinaryType::UInt8);
    void removeField();
    bool moveFieldBefore(int field, int before);
    int selectedField() const;
    const QVector<AppPlot::BinaryField> &fields() const;
  signals:
    void changed();
    void fieldSelected(int index);

  private:
    void updateField();
    void updateControls();
    void updateFieldSummary();
    QVector<AppPlot::BinaryField> m_fields;
    FluentQt::LineEdit *m_name = nullptr;
    FluentQt::SpinBox *m_offset = nullptr;
    FluentQt::ComboBox *m_type = nullptr;
    FluentQt::ComboBox *m_order = nullptr;
    FluentQt::DoubleSpinBox *m_scale = nullptr;
    FluentQt::DoubleSpinBox *m_add = nullptr;
    FluentQt::SpinBox *m_addressOffset = nullptr;
    FluentQt::ComboBox *m_addressSize = nullptr;
    FluentQt::CaptionLabel *m_fieldSummary = nullptr;
    FluentQt::StrongBodyLabel *m_title = nullptr;
    QWidget *m_addressPanel = nullptr;
    QWidget *m_form = nullptr;
    int m_payloadOffset = 0;
    int m_selected = -1;
    bool m_addressEditing = false;
    bool m_loading = false;
};
