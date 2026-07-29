#pragma once

#include "app/core/plot_value_parser.h"

#include <QtWidgets/QDialog>

class QWidget;

namespace FluentQt {
class ComboBox;
class LineEdit;
class TableWidget;
} // namespace FluentQt

class PlotParserDialog : public QDialog
{
  public:
    explicit PlotParserDialog(const AppPlot::ParserConfig &config, QWidget *parent = nullptr);

    AppPlot::ParserConfig parserConfig() const;

  protected:
    void accept() override;

  private:
    void addBinaryFieldRow(const AppPlot::BinaryField &field);
    void addDefaultBinaryField();
    void removeSelectedBinaryFields();
    bool collectBinaryFields(QVector<AppPlot::BinaryField> *fields, QString *error) const;

    AppPlot::ParserConfig m_config;
    FluentQt::LineEdit *m_fieldsEdit = nullptr;
    FluentQt::ComboBox *m_binarySourceCombo = nullptr;
    FluentQt::TableWidget *m_binaryFieldsTable = nullptr;
    QWidget *m_binaryContainer = nullptr;
};
