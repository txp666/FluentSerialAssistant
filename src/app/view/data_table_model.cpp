#include "app/view/data_table_model.h"

#include "app/core/app_i18n.h"

#include <algorithm>

namespace {

QString directionKey(const QString &direction)
{
    const QString trimmed = direction.trimmed().toLower();
    if (trimmed == QStringLiteral("tx") || trimmed == AppI18n::text("发送").toLower()) {
        return QStringLiteral("tx");
    }
    return QStringLiteral("rx");
}

} // namespace

DataTableModel::Row::Row(const DataTableRecord &value)
    : record(value), timestampMSecs(value.timestamp.toMSecsSinceEpoch()), directionKey(::directionKey(value.direction))
{
}

void DataTableModel::Row::cacheDisplay() const
{
    if (displayCached) {
        return;
    }
    timeText = record.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    sourceText = record.source.trimmed();
    if (sourceText.isEmpty()) {
        sourceText = QStringLiteral("-");
    }
    escapedText = record.text;
    escapedText.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    escapedText.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    displayCached = true;
}

QString DataTableModel::Row::displayText(int column) const
{
    switch (column) {
    case TimeColumn:
        cacheDisplay();
        return timeText;
    case DirectionColumn:
        return record.direction;
    case SourceColumn:
        cacheDisplay();
        return sourceText;
    case LengthColumn:
        return QString::number(record.length);
    case HexColumn:
        return record.hex;
    case TextColumn:
        cacheDisplay();
        return escapedText;
    case ChecksumColumn:
        return record.checksum;
    default:
        return {};
    }
}

DataTableModel::DataTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int DataTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int DataTableModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant DataTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() < 0 ||
        index.column() >= ColumnCount) {
        return {};
    }

    const Row &row = m_rows.at(static_cast<size_t>(index.row()));
    switch (role) {
    case Qt::DisplayRole:
    case Qt::ToolTipRole:
        return row.displayText(index.column());
    case Qt::TextAlignmentRole:
        if (index.column() == LengthColumn) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return {};
    case SortRole:
        if (index.column() == TimeColumn) {
            return row.timestampMSecs;
        }
        if (index.column() == LengthColumn) {
            return row.record.length;
        }
        return row.displayText(index.column());
    case RecordIndexRole:
        return row.record.recordIndex;
    case DirectionKeyRole:
        return row.directionKey;
    case SearchRole:
        if (!row.searchCached) {
            row.cacheDisplay();
            row.searchText =
                QStringLiteral("%1 %2 %3 %4 %5 %6 %7")
                    .arg(row.timeText, row.record.direction, row.sourceText, QString::number(row.record.length),
                         row.record.hex, row.escapedText, row.record.checksum)
                    .toLower();
            row.searchCached = true;
        }
        return row.searchText;
    default:
        return {};
    }
}

QVariant DataTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
    case TimeColumn:
        return AppI18n::text("时间");
    case DirectionColumn:
        return AppI18n::text("方向");
    case SourceColumn:
        return AppI18n::text("来源");
    case LengthColumn:
        return AppI18n::text("长度");
    case HexColumn:
        return QStringLiteral("HEX");
    case TextColumn:
        return AppI18n::text("文本");
    case ChecksumColumn:
        return AppI18n::text("校验");
    default:
        return {};
    }
}

void DataTableModel::setRecords(const QVector<DataTableRecord> &records)
{
    beginResetModel();
    m_rows.clear();
    for (const DataTableRecord &record : records) {
        m_rows.emplace_back(record);
    }
    endResetModel();
}

void DataTableModel::appendRecords(const QVector<DataTableRecord> &records)
{
    if (records.isEmpty()) {
        return;
    }
    const int first = rowCount();
    beginInsertRows(QModelIndex(), first, first + static_cast<int>(records.size()) - 1);
    for (const DataTableRecord &record : records) {
        m_rows.emplace_back(record);
    }
    endInsertRows();
}

void DataTableModel::removeRecordsBefore(qint64 firstRecordIndex)
{
    const auto firstRetained =
        std::lower_bound(m_rows.cbegin(), m_rows.cend(), firstRecordIndex,
                         [](const Row &row, qint64 recordIndex) { return row.record.recordIndex < recordIndex; });
    const int removed = static_cast<int>(std::distance(m_rows.cbegin(), firstRetained));
    if (removed == 0) {
        return;
    }
    beginRemoveRows(QModelIndex(), 0, removed - 1);
    m_rows.erase(m_rows.begin(), m_rows.begin() + removed);
    endRemoveRows();
}

int DataTableModel::rowForRecordIndex(qint64 recordIndex) const
{
    const auto found = std::lower_bound(m_rows.cbegin(), m_rows.cend(), recordIndex,
                                        [](const Row &row, qint64 value) { return row.record.recordIndex < value; });
    return found != m_rows.cend() && found->record.recordIndex == recordIndex
               ? static_cast<int>(std::distance(m_rows.cbegin(), found))
               : -1;
}

DataTableFilterModel::DataTableFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    setSortRole(DataTableModel::SortRole);
    setSortLocaleAware(true);
    setDynamicSortFilter(true);
}

void DataTableFilterModel::setFilters(const QString &text, const QString &direction)
{
    const QString filterText = text.trimmed().toLower();
    const QString directionFilter =
        direction.trimmed().isEmpty() ? QStringLiteral("all") : direction.trimmed().toLower();
    if (m_filterText == filterText && m_direction == directionFilter) {
        return;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_filterText = filterText;
    m_direction = directionFilter;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}

bool DataTableFilterModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, DataTableModel::TimeColumn, sourceParent);
    if (m_direction != QStringLiteral("all") &&
        index.data(DataTableModel::DirectionKeyRole).toString() != m_direction) {
        return false;
    }
    return m_filterText.isEmpty() || index.data(DataTableModel::SearchRole).toString().contains(m_filterText);
}
