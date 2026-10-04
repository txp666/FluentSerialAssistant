#pragma once

#include "app/core/terminal_color_rules.h"

#include <QtWidgets/QDialog>

namespace FluentQt {
class CheckBox;
class PushButton;
class TableWidget;
} // namespace FluentQt

class TerminalColorDialog : public QDialog
{
  public:
    explicit TerminalColorDialog(const AppTerminal::ColorConfig &config, QWidget *parent = nullptr);

    AppTerminal::ColorConfig config() const;

  protected:
    void accept() override;

  private:
    void addRuleRow(const AppTerminal::ColorRule &rule, int row = -1);
    AppTerminal::ColorRule ruleAt(int row) const;
    void addRule();
    void removeSelectedRule();
    void moveSelectedRule(int offset);
    void selectCell(QWidget *cell);
    void updateButtons();

    struct PresetCheck
    {
        QString id;
        FluentQt::CheckBox *check = nullptr;
    };

    AppTerminal::ColorConfig m_config;
    FluentQt::CheckBox *m_enabledCheck = nullptr;
    FluentQt::CheckBox *m_espIdfCheck = nullptr;
    QVector<PresetCheck> m_presetChecks;
    FluentQt::TableWidget *m_rulesTable = nullptr;
    FluentQt::PushButton *m_addButton = nullptr;
    FluentQt::PushButton *m_removeButton = nullptr;
    FluentQt::PushButton *m_upButton = nullptr;
    FluentQt::PushButton *m_downButton = nullptr;
};
