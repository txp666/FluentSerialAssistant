#pragma once

#include "app/view/data_table_model.h"

#include <QtCore/QTimer>
#include <QtWidgets/QWidget>

namespace FluentQt {
class CaptionLabel;
class ComboBox;
class PushButton;
class SearchLineEdit;
class TableView;
} // namespace FluentQt

class DataTableWindow : public QWidget
{
    Q_OBJECT

    friend class WorkbenchDataTableTest;

  public:
    explicit DataTableWindow(QWidget *parent = nullptr);

    void setRecords(const QVector<DataTableRecord> &records);
    void appendRecords(const QVector<DataTableRecord> &records, qint64 firstRecordIndex);

  signals:
    void locateRequested(qint64 recordIndex);
    void refreshRequested();

  private:
    void applyFilter();
    void updateStatus();
    void updateActionState();
    qint64 selectedRecordIndex() const;
    int selectedRow() const;
    QString selectedFrameText() const;
    QString selectedHexText() const;
    void copySelectedFrame();
    void copySelectedHex();
    void locateSelectedFrame();
    void showContextMenu(const QPoint &position);

    FluentQt::SearchLineEdit *m_filterEdit = nullptr;
    FluentQt::ComboBox *m_directionCombo = nullptr;
    FluentQt::TableView *m_table = nullptr;
    DataTableModel *m_model = nullptr;
    DataTableFilterModel *m_proxy = nullptr;
    QTimer m_filterTimer;
    FluentQt::PushButton *m_copyButton = nullptr;
    FluentQt::PushButton *m_copyHexButton = nullptr;
    FluentQt::PushButton *m_locateButton = nullptr;
    FluentQt::CaptionLabel *m_statusLabel = nullptr;
};
