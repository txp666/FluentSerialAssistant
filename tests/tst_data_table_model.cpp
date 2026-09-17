#include "app/view/data_table_model.h"

#include <QtCore/QPersistentModelIndex>
#include <QtTest/QAbstractItemModelTester>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {

DataTableRecord record(qint64 id, int length = 1, const QString &direction = QStringLiteral("rx"))
{
    DataTableRecord result;
    result.recordIndex = id;
    result.timestamp = QDateTime::fromMSecsSinceEpoch(1700000000000 + id);
    result.direction = direction;
    result.length = length;
    result.hex = QStringLiteral("01 AB");
    result.text = QStringLiteral("Line\r\nNext");
    result.checksum = QStringLiteral("CRC16: 34 12");
    return result;
}

class CountingModel : public DataTableModel
{
  public:
    mutable int searchRequests = 0;

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (role == SearchRole) {
            ++searchRequests;
        }
        return DataTableModel::data(index, role);
    }
};

} // namespace

class DataTableModelTest : public QObject
{
    Q_OBJECT

  private slots:
    void displayAndReset();
    void appendIsIncremental();
    void trimPreservesRecordIdentity();
    void filterAndNumericSort();
    void localTimestampsKeepNumericSortValues();
    void emptyFilterDoesNotBuildSearchText();
};

void DataTableModelTest::displayAndReset()
{
    DataTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.columnCount(), int(DataTableModel::ColumnCount));
    QVERIFY(!model.data(QModelIndex()).isValid());

    DataTableRecord row = record(3000000000LL, 12, QStringLiteral(" TX "));
    row.source = QStringLiteral("  Serial source  ");
    model.setRecords({row});
    QCOMPARE(resetSpy.count(), 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(model.columnCount(model.index(0, 0)), 0);
    QCOMPARE(model.index(0, DataTableModel::SourceColumn).data().toString(), QStringLiteral("Serial source"));
    QCOMPARE(model.index(0, DataTableModel::TextColumn).data().toString(), QStringLiteral("Line\\r\\nNext"));
    QCOMPARE(model.index(0, DataTableModel::LengthColumn).data().toString(), QStringLiteral("12"));
    QCOMPARE(model.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), row.recordIndex);
    QCOMPARE(model.index(0, 0).data(DataTableModel::DirectionKeyRole).toString(), QStringLiteral("tx"));
    for (int column = 0; column < model.columnCount(); ++column) {
        QCOMPARE(model.index(0, column).data(Qt::ToolTipRole), model.index(0, column).data());
        QVERIFY(!model.headerData(column, Qt::Horizontal).toString().isEmpty());
    }
    QVERIFY(model.index(0, 0).data(DataTableModel::SearchRole).toString().contains(QStringLiteral("serial source")));

    model.setRecords({record(0)});
    QCOMPARE(model.index(0, DataTableModel::SourceColumn).data().toString(), QStringLiteral("-"));
    model.setRecords({});
    QCOMPARE(resetSpy.count(), 3);
    QCOMPARE(model.rowCount(), 0);
}

void DataTableModelTest::appendIsIncremental()
{
    DataTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setRecords({record(0)});
    const QPersistentModelIndex selected(model.index(0, DataTableModel::HexColumn));
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    model.appendRecords({record(1), record(3), record(4)});
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(resetSpy.count(), 0);
    QCOMPARE(insertedSpy.count(), 1);
    QCOMPARE(insertedSpy.first().at(1).toInt(), 1);
    QCOMPARE(insertedSpy.first().at(2).toInt(), 3);
    QVERIFY(selected.isValid());
    QCOMPARE(selected.data(DataTableModel::RecordIndexRole).toLongLong(), 0LL);
    model.appendRecords({});
    QCOMPARE(insertedSpy.count(), 1);
}

void DataTableModelTest::trimPreservesRecordIdentity()
{
    DataTableModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    // Gaps represent terminal frame separators, which have no table row.
    model.setRecords({record(1), record(3), record(7), record(8)});
    const QPersistentModelIndex removed(model.index(0, 0));
    const QPersistentModelIndex retained(model.index(2, 0));
    QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);

    model.removeRecordsBefore(4);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(removedSpy.first().at(1).toInt(), 0);
    QCOMPARE(removedSpy.first().at(2).toInt(), 1);
    QVERIFY(!removed.isValid());
    QVERIFY(retained.isValid());
    QCOMPARE(retained.row(), 0);
    QCOMPARE(retained.data(DataTableModel::RecordIndexRole).toLongLong(), 7LL);
    QCOMPARE(model.rowForRecordIndex(7), 0);
    QCOMPARE(model.rowForRecordIndex(8), 1);
    QCOMPARE(model.rowForRecordIndex(6), -1);
    QCOMPARE(model.rowForRecordIndex(3), -1);

    model.removeRecordsBefore(7);
    QCOMPARE(removedSpy.count(), 1);
    model.appendRecords({record(10)});
    model.removeRecordsBefore(10);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 10LL);
    QVERIFY(!retained.isValid());
    model.removeRecordsBefore(11);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.rowForRecordIndex(10), -1);
    model.removeRecordsBefore(12);
    QCOMPARE(removedSpy.count(), 3);
    QCOMPARE(resetSpy.count(), 0);
}

void DataTableModelTest::filterAndNumericSort()
{
    DataTableModel model;
    DataTableFilterModel proxy;
    proxy.setSourceModel(&model);
    QAbstractItemModelTester tester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
    DataTableRecord first = record(0, 100, QStringLiteral("TX"));
    first.source = QStringLiteral("Alpha");
    first.timestamp = QDateTime::fromMSecsSinceEpoch(1700000003000);
    DataTableRecord second = record(2, 2);
    second.source = QStringLiteral("Beta");
    second.timestamp = QDateTime::fromMSecsSinceEpoch(1700000001000);
    DataTableRecord third = record(3, 10);
    third.source = QStringLiteral("ALPHA");
    third.timestamp = QDateTime::fromMSecsSinceEpoch(1700000002000);
    model.setRecords({first, second, third});

    proxy.sort(DataTableModel::LengthColumn, Qt::AscendingOrder);
    QCOMPARE(proxy.index(0, DataTableModel::LengthColumn).data().toString(), QStringLiteral("2"));
    QCOMPARE(proxy.index(1, DataTableModel::LengthColumn).data().toString(), QStringLiteral("10"));
    QCOMPARE(proxy.index(2, DataTableModel::LengthColumn).data().toString(), QStringLiteral("100"));
    proxy.sort(DataTableModel::TimeColumn, Qt::DescendingOrder);
    QCOMPARE(proxy.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 0LL);
    QCOMPARE(proxy.index(1, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 3LL);
    QCOMPARE(proxy.index(2, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 2LL);
    QVERIFY(proxy.isSortLocaleAware());

    proxy.setFilters(QStringLiteral("  aLpHa  "), QStringLiteral("all"));
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setFilters(QStringLiteral("alpha"), QStringLiteral("rx"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 3LL);
    proxy.setFilters(QStringLiteral("34 12"), QStringLiteral("tx"));
    QCOMPARE(proxy.rowCount(), 1);
    proxy.setFilters(QStringLiteral("\\r\\n"), QStringLiteral("all"));
    QCOMPARE(proxy.rowCount(), 3);
    proxy.setFilters(QStringLiteral("absent"), QStringLiteral("all"));
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setFilters({}, {});
    QCOMPARE(proxy.rowCount(), 3);

    proxy.setFilters({}, QStringLiteral("rx"));
    proxy.sort(DataTableModel::LengthColumn, Qt::AscendingOrder);
    const QPersistentModelIndex selected(proxy.index(1, 0));
    model.appendRecords({record(5, 1), record(6, 5, QStringLiteral("tx"))});
    QCOMPARE(proxy.rowCount(), 3);
    QCOMPARE(proxy.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 5LL);
    QCOMPARE(selected.data(DataTableModel::RecordIndexRole).toLongLong(), 3LL);
    model.removeRecordsBefore(3);
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(selected.data(DataTableModel::RecordIndexRole).toLongLong(), 3LL);
}

void DataTableModelTest::localTimestampsKeepNumericSortValues()
{
    constexpr qint64 localMSecs = 1700000000123;
    constexpr qint64 utcMSecs = localMSecs + 30000;
    constexpr qint64 earlyMSecs = -1234567890123;
    DataTableRecord local = record(0);
    local.timestamp = QDateTime::fromMSecsSinceEpoch(localMSecs).toLocalTime();
    DataTableRecord utc = record(1);
    utc.timestamp = QDateTime::fromMSecsSinceEpoch(utcMSecs).toUTC();
    DataTableRecord early = record(2);
    early.timestamp = QDateTime::fromMSecsSinceEpoch(earlyMSecs).toLocalTime();
    QCOMPARE(local.timestamp.timeSpec(), Qt::LocalTime);
    QCOMPARE(early.timestamp.timeSpec(), Qt::LocalTime);
    QCOMPARE(utc.timestamp.timeSpec(), Qt::UTC);

    DataTableModel model;
    DataTableFilterModel proxy;
    proxy.setSourceModel(&model);
    proxy.sort(DataTableModel::TimeColumn, Qt::AscendingOrder);
    model.setRecords({local, utc});
    model.appendRecords({early});
    QCOMPARE(model.index(0, 0).data(DataTableModel::SortRole).toLongLong(), localMSecs);
    QCOMPARE(model.index(1, 0).data(DataTableModel::SortRole).toLongLong(), utcMSecs);
    QCOMPARE(model.index(2, 0).data(DataTableModel::SortRole).toLongLong(), earlyMSecs);
    QCOMPARE(model.index(0, 0).data().toString(), local.timestamp.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")));
    QCOMPARE(proxy.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 2LL);
    QCOMPARE(proxy.index(1, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 0LL);
    QCOMPARE(proxy.index(2, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 1LL);
    proxy.sort(DataTableModel::TimeColumn, Qt::DescendingOrder);
    QCOMPARE(proxy.index(0, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 1LL);
    QCOMPARE(proxy.index(2, 0).data(DataTableModel::RecordIndexRole).toLongLong(), 2LL);
}

void DataTableModelTest::emptyFilterDoesNotBuildSearchText()
{
    CountingModel model;
    DataTableFilterModel proxy;
    proxy.setSourceModel(&model);
    model.setRecords({record(0), record(1, 2, QStringLiteral("tx"))});
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(model.searchRequests, 0);
    proxy.setFilters({}, QStringLiteral("rx"));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(model.searchRequests, 0);
    proxy.setFilters(QStringLiteral("   "), QStringLiteral("all"));
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(model.searchRequests, 0);

    proxy.setFilters(QStringLiteral("line"), QStringLiteral("all"));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(model.searchRequests > 0);
    const int previousRequests = model.searchRequests;
    proxy.setFilters(QStringLiteral(" LINE "), QStringLiteral("all"));
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(model.searchRequests, previousRequests);
}

QTEST_GUILESS_MAIN(DataTableModelTest)
#include "tst_data_table_model.moc"
