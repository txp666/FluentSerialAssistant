#include "app/view/data_table_window.h"

#include "app/core/app_i18n.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QMetaType>
#include <QtCore/QSignalBlocker>
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>

namespace {

constexpr int SortRole = Qt::UserRole + 1;
constexpr int SearchRole = Qt::UserRole + 2;
constexpr int RecordIndexRole = Qt::UserRole + 3;
constexpr int DirectionKeyRole = Qt::UserRole + 4;

class SortableTableItem : public QTableWidgetItem
{
  public:
    explicit SortableTableItem(const QString &text) : QTableWidgetItem(text) {}

    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant left = data(SortRole);
        const QVariant right = other.data(SortRole);
        const bool numeric = (left.metaType().id() == QMetaType::LongLong || left.metaType().id() == QMetaType::Int) &&
                             (right.metaType().id() == QMetaType::LongLong || right.metaType().id() == QMetaType::Int);
        if (numeric) {
            return left.toLongLong() < right.toLongLong();
        }
        return text().localeAwareCompare(other.text()) < 0;
    }
};

QString directionKey(const QString &direction)
{
    const QString trimmed = direction.trimmed().toLower();
    if (trimmed == QStringLiteral("tx") || trimmed == AppI18n::text("发送").toLower()) {
        return QStringLiteral("tx");
    }
    return QStringLiteral("rx");
}

QString escapedPlainText(QString text)
{
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return text;
}

} // namespace

DataTableWindow::DataTableWindow(QWidget *parent) : QWidget(parent, Qt::Window)
{
    using namespace FluentQt;

    setWindowTitle(AppI18n::text("数据表格"));
    setMinimumSize(860, 520);
    resize(1080, 680);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(10);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);

    m_filterEdit = new SearchLineEdit(this);
    m_filterEdit->setPlaceholderText(AppI18n::text("过滤时间、HEX、文本或来源"));
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setFixedHeight(32);
    m_filterEdit->setMinimumWidth(260);
    m_filterEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_directionCombo = new ComboBox(this);
    m_directionCombo->addItem(AppI18n::text("全部"), QIcon(), QStringLiteral("all"));
    m_directionCombo->addItem(AppI18n::text("仅接收"), QIcon(), QStringLiteral("rx"));
    m_directionCombo->addItem(AppI18n::text("仅发送"), QIcon(), QStringLiteral("tx"));
    m_directionCombo->setFixedSize(112, 32);

    auto *refreshButton = new PushButton(icon(FluentIcon::Sync), AppI18n::text("刷新"), this);
    refreshButton->setFixedHeight(32);
    m_copyButton = new PushButton(icon(FluentIcon::Copy), AppI18n::text("复制帧"), this);
    m_copyButton->setFixedHeight(32);
    m_copyHexButton = new PushButton(icon(FluentIcon::Code), AppI18n::text("复制 HEX"), this);
    m_copyHexButton->setFixedHeight(32);
    m_locateButton = new PushButton(icon(FluentIcon::SearchMirror), AppI18n::text("定位终端"), this);
    m_locateButton->setFixedHeight(32);

    m_statusLabel = new CaptionLabel(QString(), this);
    m_statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statusLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);

    toolbar->addWidget(m_filterEdit, 1);
    toolbar->addWidget(m_directionCombo);
    toolbar->addWidget(refreshButton);
    toolbar->addSpacing(4);
    toolbar->addWidget(m_copyButton);
    toolbar->addWidget(m_copyHexButton);
    toolbar->addWidget(m_locateButton);
    toolbar->addWidget(m_statusLabel);
    root->addLayout(toolbar);

    m_table = new TableWidget(this);
    m_table->setColumnCount(ColumnCount);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->setWordWrap(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setBorderRadius(8);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    m_table->horizontalHeader()->setSectionResizeMode(TimeColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DirectionColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(SourceColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(LengthColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(HexColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(TextColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ChecksumColumn, QHeaderView::ResizeToContents);
    setupHeaders();
    root->addWidget(m_table, 1);

    connect(m_filterEdit, &SearchLineEdit::textChanged, this, [this]() { applyFilter(); });
    connect(m_filterEdit, &SearchLineEdit::clearSignal, this, [this]() { applyFilter(); });
    connect(m_directionCombo, &ComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
    connect(refreshButton, &PushButton::clicked, this, &DataTableWindow::refreshRequested);
    connect(m_copyButton, &PushButton::clicked, this, &DataTableWindow::copySelectedFrame);
    connect(m_copyHexButton, &PushButton::clicked, this, &DataTableWindow::copySelectedHex);
    connect(m_locateButton, &PushButton::clicked, this, &DataTableWindow::locateSelectedFrame);
    connect(m_table, &TableWidget::itemSelectionChanged, this, &DataTableWindow::updateActionState);
    connect(m_table, &TableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *) { locateSelectedFrame(); });
    connect(m_table, &TableWidget::customContextMenuRequested, this, &DataTableWindow::showContextMenu);

    updateStatus();
    updateActionState();
}

void DataTableWindow::setRecords(const QVector<DataTableRecord> &records)
{
    if (!m_table) {
        return;
    }

    const int previousRecordIndex = selectedRecordIndex();
    const int sortColumn = m_table->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_table->horizontalHeader()->sortIndicatorOrder();

    const QSignalBlocker blocker(m_table);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    m_totalRows = 0;
    for (const DataTableRecord &record : records) {
        addRecordRow(record);
    }
    m_table->setSortingEnabled(true);
    m_table->sortItems(sortColumn >= 0 ? sortColumn : TimeColumn, sortColumn >= 0 ? sortOrder : Qt::AscendingOrder);
    applyFilter();
    if (previousRecordIndex >= 0) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            QTableWidgetItem *item = m_table->item(row, TimeColumn);
            if (item && item->data(RecordIndexRole).toInt() == previousRecordIndex) {
                m_table->selectRow(row);
                break;
            }
        }
    }
    updateActionState();
}

void DataTableWindow::appendRecord(const DataTableRecord &record)
{
    if (!m_table) {
        return;
    }

    const bool sorting = m_table->isSortingEnabled();
    m_table->setSortingEnabled(false);
    addRecordRow(record);
    m_table->setSortingEnabled(sorting);
    applyFilter();
}

void DataTableWindow::setupHeaders()
{
    const QStringList labels = {
        AppI18n::text("时间"),
        AppI18n::text("方向"),
        AppI18n::text("来源"),
        AppI18n::text("长度"),
        QStringLiteral("HEX"),
        AppI18n::text("文本"),
        AppI18n::text("校验"),
    };
    m_table->setHorizontalHeaderLabels(labels);
}

void DataTableWindow::addRecordRow(const DataTableRecord &record)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    const QString timeText = record.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString direction = record.direction;
    const QString source = record.source.trimmed().isEmpty() ? QStringLiteral("-") : record.source.trimmed();
    const QString length = QString::number(record.length);
    const QString text = escapedPlainText(record.text);
    const QString key = directionKey(direction);
    const QString searchText =
        QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
            .arg(timeText, direction, source, length, record.hex, text, record.checksum)
            .toLower();

    const QStringList values = {timeText, direction, source, length, record.hex, text, record.checksum};
    for (int column = 0; column < values.size(); ++column) {
        auto *item = new SortableTableItem(values.at(column));
        item->setToolTip(values.at(column));
        item->setData(SearchRole, searchText);
        item->setData(RecordIndexRole, record.recordIndex);
        item->setData(DirectionKeyRole, key);
        if (column == TimeColumn) {
            item->setData(SortRole, record.timestamp.toMSecsSinceEpoch());
        } else if (column == LengthColumn) {
            item->setData(SortRole, record.length);
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        } else {
            item->setData(SortRole, values.at(column));
        }
        m_table->setItem(row, column, item);
    }
    ++m_totalRows;
}

void DataTableWindow::applyFilter()
{
    if (!m_table) {
        return;
    }

    const QString directionFilter = m_directionCombo ? m_directionCombo->currentData().toString() : QStringLiteral("all");
    const QString filterText = m_filterEdit ? m_filterEdit->text().trimmed().toLower() : QString();
    int visibleRows = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *item = m_table->item(row, TimeColumn);
        if (!item) {
            m_table->setRowHidden(row, true);
            continue;
        }

        const QString rowDirection = item->data(DirectionKeyRole).toString();
        const QString rowSearch = item->data(SearchRole).toString();
        const bool directionOk = directionFilter == QStringLiteral("all") || directionFilter == rowDirection;
        const bool textOk = filterText.isEmpty() || rowSearch.contains(filterText);
        const bool visible = directionOk && textOk;
        m_table->setRowHidden(row, !visible);
        if (visible) {
            ++visibleRows;
        }
    }

    updateStatus();
    updateActionState();
}

void DataTableWindow::updateStatus()
{
    if (!m_statusLabel || !m_table) {
        return;
    }

    int visibleRows = 0;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (!m_table->isRowHidden(row)) {
            ++visibleRows;
        }
    }
    m_statusLabel->setText(AppI18n::text("显示 %1/%2 条").arg(visibleRows).arg(m_totalRows));
}

void DataTableWindow::updateActionState()
{
    const bool hasSelection = selectedRow() >= 0;
    if (m_copyButton) {
        m_copyButton->setEnabled(hasSelection);
    }
    if (m_copyHexButton) {
        m_copyHexButton->setEnabled(hasSelection);
    }
    if (m_locateButton) {
        m_locateButton->setEnabled(hasSelection);
    }
}

int DataTableWindow::selectedRecordIndex() const
{
    const int row = selectedRow();
    if (row < 0 || !m_table) {
        return -1;
    }
    QTableWidgetItem *item = m_table->item(row, TimeColumn);
    return item ? item->data(RecordIndexRole).toInt() : -1;
}

int DataTableWindow::selectedRow() const
{
    if (!m_table) {
        return -1;
    }
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (!rows.isEmpty()) {
        return rows.first().row();
    }
    const int current = m_table->currentRow();
    return current >= 0 && !m_table->isRowHidden(current) ? current : -1;
}

QString DataTableWindow::selectedFrameText() const
{
    const int row = selectedRow();
    if (row < 0 || !m_table) {
        return {};
    }

    QStringList lines;
    for (int column = 0; column < ColumnCount; ++column) {
        const QString header = m_table->horizontalHeaderItem(column) ? m_table->horizontalHeaderItem(column)->text()
                                                                     : QString();
        const QString value = m_table->item(row, column) ? m_table->item(row, column)->text() : QString();
        lines.append(QStringLiteral("%1: %2").arg(header, value));
    }
    return lines.join(QLatin1Char('\n'));
}

QString DataTableWindow::selectedHexText() const
{
    const int row = selectedRow();
    if (row < 0 || !m_table || !m_table->item(row, HexColumn)) {
        return {};
    }
    return m_table->item(row, HexColumn)->text();
}

void DataTableWindow::copySelectedFrame()
{
    const QString text = selectedFrameText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void DataTableWindow::copySelectedHex()
{
    const QString text = selectedHexText();
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void DataTableWindow::locateSelectedFrame()
{
    const int recordIndex = selectedRecordIndex();
    if (recordIndex >= 0) {
        emit locateRequested(recordIndex);
    }
}

void DataTableWindow::showContextMenu(const QPoint &position)
{
    if (!m_table) {
        return;
    }

    const QModelIndex index = m_table->indexAt(position);
    if (index.isValid()) {
        m_table->selectRow(index.row());
    }

    QMenu menu(this);
    QAction *copyAction = menu.addAction(AppI18n::text("复制帧"));
    QAction *copyHexAction = menu.addAction(AppI18n::text("复制 HEX"));
    menu.addSeparator();
    QAction *locateAction = menu.addAction(AppI18n::text("定位终端"));
    const bool enabled = selectedRow() >= 0;
    copyAction->setEnabled(enabled);
    copyHexAction->setEnabled(enabled);
    locateAction->setEnabled(enabled);

    QAction *selected = menu.exec(m_table->viewport()->mapToGlobal(position));
    if (selected == copyAction) {
        copySelectedFrame();
    } else if (selected == copyHexAction) {
        copySelectedHex();
    } else if (selected == locateAction) {
        locateSelectedFrame();
    }
}
