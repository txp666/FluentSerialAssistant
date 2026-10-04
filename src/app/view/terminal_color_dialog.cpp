#include "app/view/terminal_color_dialog.h"

#include "app/core/app_i18n.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QVBoxLayout>

namespace {

enum RuleColumn
{
    EnabledColumn,
    TypeColumn,
    PatternColumn,
    CaseColumn,
    ScopeColumn,
    ColorColumn,
    ColumnCount
};

template <typename T> T *controlAt(const QTableWidget *table, int row, int column)
{
    QWidget *cell = table->cellWidget(row, column);
    return cell ? cell->findChild<T *>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
}

QWidget *createCell(QWidget *control, QWidget *parent, bool centered = false)
{
    auto *cell = new QWidget(parent);
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(4, 4, 4, 4);
    if (centered) {
        layout->addStretch();
    }
    layout->addWidget(control);
    if (centered) {
        layout->addStretch();
    }
    return cell;
}

void fitComboColumn(FluentQt::ComboBox *combo, QTableWidget *table, int column)
{
    combo->ensurePolished();
    const QFontMetrics metrics(combo->font());
    int textWidth = 0;
    for (int index = 0; index < combo->count(); ++index) {
        textWidth = qMax(textWidth, metrics.horizontalAdvance(combo->itemText(index)));
    }
    const int width = combo->sizeHint().width() + textWidth - metrics.horizontalAdvance(combo->currentText());
    combo->setMinimumWidth(width);
    // Account for both cell margins and the table's grid/border allowance.
    table->horizontalHeader()->resizeSection(column, qMax(table->columnWidth(column), width + 12));
}

} // namespace

TerminalColorDialog::TerminalColorDialog(const AppTerminal::ColorConfig &config, QWidget *parent)
    : QDialog(parent), m_config(config)
{
    using namespace FluentQt;

    setObjectName(QStringLiteral("terminalColorDialog"));
    setWindowTitle(AppI18n::text("内容着色"));
    setModal(true);
    resize(840, 650);
    setMinimumSize(780, 580);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto *description = new BodyLabel(AppI18n::text("勾选示例即可启用对应配色，也可添加自定义规则。"), this);
    description->setWordWrap(true);
    root->addWidget(description);

    auto *presetsContainer = new QWidget(this);
    presetsContainer->setObjectName(QStringLiteral("terminalColorPresets"));
    auto *presetsLayout = new QVBoxLayout(presetsContainer);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->setSpacing(6);
    presetsLayout->addWidget(new StrongBodyLabel(AppI18n::text("示例预设"), presetsContainer));
    auto *presetsGrid = new QGridLayout;
    presetsGrid->setHorizontalSpacing(18);
    presetsGrid->setVerticalSpacing(4);
    presetsGrid->setColumnStretch(1, 1);
    int presetRow = 0;
    for (const AppTerminal::ColorPreset &preset : AppTerminal::colorPresets()) {
        auto *check = new CheckBox(preset.name, presetsContainer);
        const bool isEspIdf = preset.id == QStringLiteral("esp_idf");
        check->setObjectName(isEspIdf ? QStringLiteral("terminalColorEspIdfCheck")
                                      : QStringLiteral("terminalColorPreset_") + preset.id);
        check->setChecked(isEspIdf ? config.espIdfEnabled : config.enabledPresets.contains(preset.id));
        if (isEspIdf) {
            m_espIdfCheck = check;
        } else {
            m_presetChecks.append({preset.id, check});
        }
        auto *sample = new CaptionLabel(preset.example, presetsContainer);
        sample->setObjectName(QStringLiteral("terminalColorPresetExample_") + preset.id);
        sample->setTextFormat(Qt::PlainText);
        sample->setWordWrap(true);
        sample->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        presetsGrid->addWidget(check, presetRow, 0, Qt::AlignVCenter);
        presetsGrid->addWidget(sample, presetRow, 1, Qt::AlignVCenter);
        ++presetRow;
    }
    presetsLayout->addLayout(presetsGrid);
    root->addWidget(presetsContainer);

    m_enabledCheck = new CheckBox(AppI18n::text("启用自定义着色"), this);
    m_enabledCheck->setObjectName(QStringLiteral("terminalColorEnabledCheck"));
    m_enabledCheck->setChecked(config.enabled);
    root->addWidget(m_enabledCheck);

    auto *rulesContainer = new QWidget(this);
    auto *rulesLayout = new QVBoxLayout(rulesContainer);
    rulesLayout->setContentsMargins(0, 0, 0, 0);
    rulesLayout->setSpacing(10);

    m_rulesTable = new TableWidget(rulesContainer);
    m_rulesTable->setObjectName(QStringLiteral("terminalColorRulesTable"));
    m_rulesTable->setBorderVisible(true);
    m_rulesTable->setBorderRadius(8);
    m_rulesTable->setWordWrap(false);
    m_rulesTable->setColumnCount(ColumnCount);
    m_rulesTable->setHorizontalHeaderLabels({AppI18n::text("启用"), AppI18n::text("匹配方式"),
                                             AppI18n::text("匹配内容"), AppI18n::text("区分大小写"),
                                             AppI18n::text("着色范围"), AppI18n::text("颜色")});
    auto *header = m_rulesTable->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(PatternColumn, QHeaderView::Stretch);
    header->resizeSection(EnabledColumn, 66);
    header->resizeSection(TypeColumn, 120);
    header->resizeSection(CaseColumn, 108);
    header->resizeSection(ScopeColumn, 160);
    header->resizeSection(ColorColumn, 64);
    m_rulesTable->verticalHeader()->setVisible(false);
    m_rulesTable->verticalHeader()->setDefaultSectionSize(44);
    m_rulesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rulesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rulesTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed |
                                  QAbstractItemView::AnyKeyPressed);
    rulesLayout->addWidget(m_rulesTable, 1);

    auto *hint = new CaptionLabel(
        AppI18n::text("按行匹配正文；自定义规则优先于预设，上方规则优先。双击匹配内容可编辑。"), rulesContainer);
    hint->setWordWrap(true);
    rulesLayout->addWidget(hint);

    auto *ruleButtons = new QHBoxLayout;
    ruleButtons->setSpacing(8);
    m_addButton = new PushButton(AppI18n::text("添加规则"), rulesContainer);
    m_addButton->setObjectName(QStringLiteral("terminalColorAddButton"));
    m_removeButton = new PushButton(AppI18n::text("删除选中"), rulesContainer);
    m_removeButton->setObjectName(QStringLiteral("terminalColorRemoveButton"));
    m_upButton = new PushButton(AppI18n::text("上移"), rulesContainer);
    m_upButton->setObjectName(QStringLiteral("terminalColorUpButton"));
    m_downButton = new PushButton(AppI18n::text("下移"), rulesContainer);
    m_downButton->setObjectName(QStringLiteral("terminalColorDownButton"));
    for (PushButton *button : {m_addButton, m_removeButton, m_upButton, m_downButton}) {
        button->setAutoDefault(false);
    }
    ruleButtons->addWidget(m_addButton);
    ruleButtons->addStretch();
    ruleButtons->addWidget(m_removeButton);
    ruleButtons->addWidget(m_upButton);
    ruleButtons->addWidget(m_downButton);
    rulesLayout->addLayout(ruleButtons);
    root->addWidget(rulesContainer, 1);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancelButton = new PushButton(AppI18n::text("取消"), this);
    cancelButton->setObjectName(QStringLiteral("terminalColorCancelButton"));
    cancelButton->setAutoDefault(false);
    auto *applyButton = new PrimaryPushButton(AppI18n::text("应用"), this);
    applyButton->setObjectName(QStringLiteral("terminalColorApplyButton"));
    applyButton->setDefault(true);
    buttons->addWidget(cancelButton);
    buttons->addWidget(applyButton);
    root->addLayout(buttons);

    for (const AppTerminal::ColorRule &rule : config.rules) {
        if (m_rulesTable->rowCount() >= AppTerminal::MaximumColorRules) {
            break;
        }
        addRuleRow(rule);
    }

    connect(m_enabledCheck, &CheckBox::toggled, rulesContainer, &QWidget::setEnabled);
    rulesContainer->setEnabled(config.enabled);
    connect(m_addButton, &PushButton::clicked, this, &TerminalColorDialog::addRule);
    connect(m_removeButton, &PushButton::clicked, this, &TerminalColorDialog::removeSelectedRule);
    connect(m_upButton, &PushButton::clicked, this, [this] { moveSelectedRule(-1); });
    connect(m_downButton, &PushButton::clicked, this, [this] { moveSelectedRule(1); });
    connect(m_rulesTable, &TableWidget::itemSelectionChanged, this, &TerminalColorDialog::updateButtons);
    connect(cancelButton, &PushButton::clicked, this, &QDialog::reject);
    connect(applyButton, &PrimaryPushButton::clicked, this, &TerminalColorDialog::accept);
    updateButtons();
}

AppTerminal::ColorConfig TerminalColorDialog::config() const { return m_config; }

void TerminalColorDialog::accept()
{
    AppTerminal::ColorConfig next;
    next.enabled = m_enabledCheck->isChecked();
    next.espIdfEnabled = m_espIdfCheck->isChecked();
    for (const PresetCheck &preset : m_presetChecks) {
        if (preset.check->isChecked()) {
            next.enabledPresets.append(preset.id);
        }
    }
    for (int row = 0; row < m_rulesTable->rowCount(); ++row) {
        const AppTerminal::ColorRule rule = ruleAt(row);
        const QString error = next.enabled && rule.enabled ? AppTerminal::validationError(rule) : QString();
        if (!error.isEmpty()) {
            m_rulesTable->setCurrentCell(row, PatternColumn);
            m_rulesTable->scrollToItem(m_rulesTable->item(row, PatternColumn));
            m_rulesTable->setFocus();
            m_rulesTable->editItem(m_rulesTable->item(row, PatternColumn));
            FluentQt::InfoBar::error(AppI18n::text("配置无效"),
                                     AppI18n::text("第 %1 条规则：%2").arg(row + 1).arg(error), Qt::Horizontal, true,
                                     5000, FluentQt::InfoBarPosition::TopRight, this);
            return;
        }
        next.rules.append(rule);
    }
    m_config = next;
    QDialog::accept();
}

void TerminalColorDialog::addRuleRow(const AppTerminal::ColorRule &rule, int row)
{
    using namespace FluentQt;

    if (row < 0) {
        row = m_rulesTable->rowCount();
    }
    m_rulesTable->insertRow(row);
    m_rulesTable->setItem(row, PatternColumn, new QTableWidgetItem(rule.pattern));

    auto *enabled = new CheckBox(m_rulesTable);
    enabled->ensurePolished();
    enabled->setFixedSize(32, 32);
    enabled->setAccessibleName(AppI18n::text("启用"));
    enabled->setChecked(rule.enabled);
    QWidget *enabledCell = createCell(enabled, m_rulesTable, true);
    m_rulesTable->setCellWidget(row, EnabledColumn, enabledCell);
    connect(enabled, &CheckBox::clicked, this, [this, enabledCell] { selectCell(enabledCell); });

    auto *type = new ComboBox(m_rulesTable);
    type->setFixedHeight(32);
    type->addItem(AppI18n::text("关键词"));
    type->addItem(AppI18n::text("正则"));
    type->setCurrentIndex(rule.regularExpression ? 1 : 0);
    fitComboColumn(type, m_rulesTable, TypeColumn);
    QWidget *typeCell = createCell(type, m_rulesTable);
    m_rulesTable->setCellWidget(row, TypeColumn, typeCell);
    connect(type, qOverload<int>(&ComboBox::currentIndexChanged), this, [this, typeCell] { selectCell(typeCell); });

    auto *caseSensitive = new CheckBox(m_rulesTable);
    caseSensitive->ensurePolished();
    caseSensitive->setFixedSize(32, 32);
    caseSensitive->setAccessibleName(AppI18n::text("区分大小写"));
    caseSensitive->setChecked(rule.caseSensitive);
    QWidget *caseCell = createCell(caseSensitive, m_rulesTable, true);
    m_rulesTable->setCellWidget(row, CaseColumn, caseCell);
    connect(caseSensitive, &CheckBox::clicked, this, [this, caseCell] { selectCell(caseCell); });

    auto *scope = new ComboBox(m_rulesTable);
    scope->setFixedHeight(32);
    scope->addItem(AppI18n::text("匹配内容"));
    scope->addItem(AppI18n::text("整行"));
    scope->setCurrentIndex(rule.wholeLine ? 1 : 0);
    fitComboColumn(scope, m_rulesTable, ScopeColumn);
    QWidget *scopeCell = createCell(scope, m_rulesTable);
    m_rulesTable->setCellWidget(row, ScopeColumn, scopeCell);
    connect(scope, qOverload<int>(&ComboBox::currentIndexChanged), this, [this, scopeCell] { selectCell(scopeCell); });

    auto *color = new ColorPickerButton(rule.color, AppI18n::text("内容颜色"), m_rulesTable);
    color->setAccessibleName(AppI18n::text("内容颜色"));
    color->setFixedSize(32, 32);
    QWidget *colorCell = createCell(color, m_rulesTable, true);
    m_rulesTable->setCellWidget(row, ColorColumn, colorCell);
    connect(color, &ColorPickerButton::clicked, this, [this, colorCell] { selectCell(colorCell); });
}

AppTerminal::ColorRule TerminalColorDialog::ruleAt(int row) const
{
    AppTerminal::ColorRule rule;
    rule.pattern = m_rulesTable->item(row, PatternColumn)->text();
    rule.enabled = controlAt<FluentQt::CheckBox>(m_rulesTable, row, EnabledColumn)->isChecked();
    rule.regularExpression = controlAt<FluentQt::ComboBox>(m_rulesTable, row, TypeColumn)->currentIndex() == 1;
    rule.caseSensitive = controlAt<FluentQt::CheckBox>(m_rulesTable, row, CaseColumn)->isChecked();
    rule.wholeLine = controlAt<FluentQt::ComboBox>(m_rulesTable, row, ScopeColumn)->currentIndex() == 1;
    rule.color = controlAt<FluentQt::ColorPickerButton>(m_rulesTable, row, ColorColumn)->color();
    return rule;
}

void TerminalColorDialog::addRule()
{
    if (m_rulesTable->rowCount() >= AppTerminal::MaximumColorRules) {
        return;
    }
    addRuleRow(AppTerminal::ColorRule());
    const int row = m_rulesTable->rowCount() - 1;
    m_rulesTable->setCurrentCell(row, PatternColumn);
    m_rulesTable->scrollToItem(m_rulesTable->item(row, PatternColumn));
    m_rulesTable->editItem(m_rulesTable->item(row, PatternColumn));
    updateButtons();
}

void TerminalColorDialog::removeSelectedRule()
{
    const int row = m_rulesTable->currentRow();
    if (row < 0) {
        return;
    }
    m_rulesTable->removeRow(row);
    if (m_rulesTable->rowCount() > 0) {
        m_rulesTable->setCurrentCell(qMin(row, m_rulesTable->rowCount() - 1), PatternColumn);
    }
    updateButtons();
}

void TerminalColorDialog::moveSelectedRule(int offset)
{
    const int row = m_rulesTable->currentRow();
    const int destination = row + offset;
    if (row < 0 || destination < 0 || destination >= m_rulesTable->rowCount()) {
        return;
    }
    const AppTerminal::ColorRule rule = ruleAt(row);
    m_rulesTable->removeRow(row);
    addRuleRow(rule, destination);
    m_rulesTable->setCurrentCell(destination, PatternColumn);
    m_rulesTable->scrollToItem(m_rulesTable->item(destination, PatternColumn));
    updateButtons();
}

void TerminalColorDialog::selectCell(QWidget *cell)
{
    for (int row = 0; row < m_rulesTable->rowCount(); ++row) {
        for (int column = 0; column < ColumnCount; ++column) {
            if (m_rulesTable->cellWidget(row, column) == cell) {
                m_rulesTable->setCurrentCell(row, PatternColumn);
                return;
            }
        }
    }
}

void TerminalColorDialog::updateButtons()
{
    const int row = m_rulesTable->currentRow();
    const int count = m_rulesTable->rowCount();
    m_addButton->setEnabled(count < AppTerminal::MaximumColorRules);
    m_removeButton->setEnabled(row >= 0);
    m_upButton->setEnabled(row > 0);
    m_downButton->setEnabled(row >= 0 && row + 1 < count);
}
