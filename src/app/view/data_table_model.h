#pragma once

#include <QtCore/QAbstractTableModel>
#include <QtCore/QDateTime>
#include <QtCore/QSortFilterProxyModel>
#include <QtCore/QVector>

#include <deque>

struct DataTableRecord
{
    qint64 recordIndex = -1;
    QDateTime timestamp;
    QString direction;
    QString source;
    int length = 0;
    QString hex;
    QString text;
    QString checksum;
};

class DataTableModel : public QAbstractTableModel
{
    Q_OBJECT

  public:
    enum Column
    {
        TimeColumn,
        DirectionColumn,
        SourceColumn,
        LengthColumn,
        HexColumn,
        TextColumn,
        ChecksumColumn,
        ColumnCount
    };

    enum Role
    {
        SortRole = Qt::UserRole + 1,
        SearchRole,
        RecordIndexRole,
        DirectionKeyRole
    };

    explicit DataTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setRecords(const QVector<DataTableRecord> &records);
    void appendRecords(const QVector<DataTableRecord> &records);
    void removeRecordsBefore(qint64 firstRecordIndex);
    int rowForRecordIndex(qint64 recordIndex) const;

  private:
    struct Row
    {
        DataTableRecord record;
        qint64 timestampMSecs;
        QString directionKey;
        mutable QString timeText;
        mutable QString sourceText;
        mutable QString escapedText;
        mutable QString searchText;
        mutable bool displayCached = false;
        mutable bool searchCached = false;

        explicit Row(const DataTableRecord &value);
        void cacheDisplay() const;
        QString displayText(int column) const;
    };

    std::deque<Row> m_rows;
};

class DataTableFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    explicit DataTableFilterModel(QObject *parent = nullptr);
    void setFilters(const QString &text, const QString &direction);

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

  private:
    QString m_filterText;
    QString m_direction = QStringLiteral("all");
};
