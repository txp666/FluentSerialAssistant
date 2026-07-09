#pragma once

#include <QtCore/QDateTime>
#include <QtCore/QVector>
#include <QtWidgets/QWidget>

namespace FluentQt {
class CaptionLabel;
class ComboBox;
class PushButton;
class SearchLineEdit;
class TableWidget;
} // namespace FluentQt

struct DataTableRecord
{
    int recordIndex = -1;
    QDateTime timestamp;
    QString direction;
    QString source;
    int length = 0;
    QString hex;
    QString text;
    QString checksum;
};

class DataTableWindow : public QWidget
{
    Q_OBJECT

  public:
    explicit DataTableWindow(QWidget *parent = nullptr);

    void setRecords(const QVector<DataTableRecord> &records);
    void appendRecord(const DataTableRecord &record);

  signals:
    void locateRequested(int recordIndex);
    void refreshRequested();

  private:
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

    void setupHeaders();
    void addRecordRow(const DataTableRecord &record);
    void applyFilter();
    void updateStatus();
    void updateActionState();
    int selectedRecordIndex() const;
    int selectedRow() const;
    QString selectedFrameText() const;
    QString selectedHexText() const;
    void copySelectedFrame();
    void copySelectedHex();
    void locateSelectedFrame();
    void showContextMenu(const QPoint &position);

    FluentQt::SearchLineEdit *m_filterEdit = nullptr;
    FluentQt::ComboBox *m_directionCombo = nullptr;
    FluentQt::TableWidget *m_table = nullptr;
    FluentQt::PushButton *m_copyButton = nullptr;
    FluentQt::PushButton *m_copyHexButton = nullptr;
    FluentQt::PushButton *m_locateButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;
    int m_totalRows = 0;
};
