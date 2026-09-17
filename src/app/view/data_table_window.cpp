#include "app/view/data_table_window.h"

#include "app/core/app_i18n.h"

#include <FluentQtWidgets/FluentQtWidgets.h>

#include <QtCore/QItemSelectionModel>
#include <QtGui/QClipboard>
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QMenu>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QVBoxLayout>

using Column = DataTableModel;

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

    m_model = new DataTableModel(this);
    m_proxy = new DataTableFilterModel(this);
    m_proxy->setSourceModel(m_model);
    m_table = new TableView(this);
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(true);
    m_table->setWordWrap(false);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setBorderRadius(8);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionsClickable(true);
    // Content-based sizing scans historical rows whenever new data arrives.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(Column::TimeColumn, 200);
    m_table->setColumnWidth(Column::DirectionColumn, 70);
    m_table->setColumnWidth(Column::SourceColumn, 100);
    m_table->setColumnWidth(Column::LengthColumn, 70);
    m_table->setColumnWidth(Column::ChecksumColumn, 160);
    m_table->horizontalHeader()->setSectionResizeMode(Column::HexColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(Column::TextColumn, QHeaderView::Stretch);
    m_table->sortByColumn(Column::TimeColumn, Qt::AscendingOrder);
    root->addWidget(m_table, 1);

    m_filterTimer.setSingleShot(true);
    m_filterTimer.setInterval(150);
    connect(&m_filterTimer, &QTimer::timeout, this, &DataTableWindow::applyFilter);
    connect(m_filterEdit, &SearchLineEdit::textChanged, this, [this]() { m_filterTimer.start(); });
    connect(m_filterEdit, &SearchLineEdit::clearSignal, this, [this]() { applyFilter(); });
    connect(m_directionCombo, &ComboBox::currentIndexChanged, this, [this](int) { applyFilter(); });
    connect(refreshButton, &PushButton::clicked, this, &DataTableWindow::refreshRequested);
    connect(m_copyButton, &PushButton::clicked, this, &DataTableWindow::copySelectedFrame);
    connect(m_copyHexButton, &PushButton::clicked, this, &DataTableWindow::copySelectedHex);
    connect(m_locateButton, &PushButton::clicked, this, &DataTableWindow::locateSelectedFrame);
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &DataTableWindow::updateActionState);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, &DataTableWindow::updateActionState);
    connect(m_table, &TableView::doubleClicked, this, [this](const QModelIndex &) { locateSelectedFrame(); });
    connect(m_table, &TableView::customContextMenuRequested, this, &DataTableWindow::showContextMenu);

    updateStatus();
    updateActionState();
}

void DataTableWindow::setRecords(const QVector<DataTableRecord> &records)
{
    const qint64 previousRecordIndex = selectedRecordIndex();
    m_model->setRecords(records);
    if (previousRecordIndex >= 0) {
        const int row = m_model->rowForRecordIndex(previousRecordIndex);
        const QModelIndex index = m_proxy->mapFromSource(m_model->index(row, 0));
        if (index.isValid()) {
            m_table->selectRow(index.row());
        }
    }
    updateStatus();
    updateActionState();
}

void DataTableWindow::appendRecords(const QVector<DataTableRecord> &records, qint64 firstRecordIndex)
{
    m_model->removeRecordsBefore(firstRecordIndex);
    m_model->appendRecords(records);
    updateStatus();
    updateActionState();
}

void DataTableWindow::applyFilter()
{
    m_filterTimer.stop();
    m_proxy->setFilters(m_filterEdit->text(), m_directionCombo->currentData().toString());
    updateStatus();
    updateActionState();
}

void DataTableWindow::updateStatus()
{
    m_statusLabel->setText(AppI18n::text("显示 %1/%2 条").arg(m_proxy->rowCount()).arg(m_model->rowCount()));
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

qint64 DataTableWindow::selectedRecordIndex() const
{
    const int row = selectedRow();
    return row >= 0 ? m_proxy->index(row, 0).data(DataTableModel::RecordIndexRole).toLongLong() : -1;
}

int DataTableWindow::selectedRow() const
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (!rows.isEmpty()) {
        return rows.first().row();
    }
    return m_table->currentIndex().isValid() ? m_table->currentIndex().row() : -1;
}

QString DataTableWindow::selectedFrameText() const
{
    const int row = selectedRow();
    if (row < 0 || !m_table) {
        return {};
    }

    QStringList lines;
    for (int column = 0; column < Column::ColumnCount; ++column) {
        const QString header = m_proxy->headerData(column, Qt::Horizontal).toString();
        const QString value = m_proxy->index(row, column).data().toString();
        lines.append(QStringLiteral("%1: %2").arg(header, value));
    }
    return lines.join(QLatin1Char('\n'));
}

QString DataTableWindow::selectedHexText() const
{
    const int row = selectedRow();
    if (row < 0) {
        return {};
    }
    return m_proxy->index(row, Column::HexColumn).data().toString();
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
    const qint64 recordIndex = selectedRecordIndex();
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
