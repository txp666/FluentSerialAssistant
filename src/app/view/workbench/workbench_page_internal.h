#pragma once

#include "app/view/workbench_page.h"

#include "app/core/font_preferences.h"
#include "app/core/hex_utils.h"
#include "app/core/text_encoding.h"

#include <FluentQtWidgets/Dialogs/FolderListDialog.h>
#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/StyleSheet.h>
#include <FluentQtWidgets/Theme.h>

#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QSignalBlocker>
#include <QtCore/QStandardPaths>
#include <QtGui/QIcon>
#include <QtGui/QKeyEvent>
#include <QtGui/QTextCharFormat>
#include <QtGui/QTextCursor>
#include <QtGui/QTextOption>
#include <QtSerialPort/QSerialPortInfo>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListWidgetItem>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>

namespace WorkbenchPagePrivate {

using namespace FluentQt;

constexpr int FlushIntervalMs = 33;
constexpr int DefaultMaxTerminalRecords = 20000;
constexpr int MaxSendHistoryItems = 20;
constexpr int MaxSendPacketItems = 100;
constexpr int SidePanelWidth = 324;
constexpr int TerminalPanelMinWidth = 560;
constexpr int ReconnectIntervalMs = 2000;
constexpr int CompactControlHeight = 32;
constexpr int DefaultFileChunkSize = 256;
constexpr int DefaultFileChunkIntervalMs = 10;

inline QColor terminalTimestampColor()
{
    return ThemeManager::instance()->effectiveTheme() == Theme::Dark ? QColor(132, 192, 214) : QColor(75, 103, 128);
}

inline QColor terminalDirectionMarkerColor(bool tx)
{
    const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
    if (tx) {
        return dark ? QColor(255, 185, 115) : QColor(177, 86, 15);
    }
    return dark ? QColor(79, 214, 191) : QColor(0, 121, 107);
}

inline QColor terminalEspIdfLogLevelColor(QChar level)
{
    const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
    switch (level.toUpper().toLatin1()) {
    case 'V':
        return dark ? QColor(187, 154, 255) : QColor(108, 84, 190);
    case 'D':
        return dark ? QColor(117, 190, 255) : QColor(33, 118, 188);
    case 'I':
        return dark ? QColor(103, 219, 123) : QColor(0, 126, 67);
    case 'W':
        return dark ? QColor(255, 209, 102) : QColor(171, 103, 0);
    case 'E':
        return dark ? QColor(255, 132, 132) : QColor(190, 35, 35);
    default:
        return QColor();
    }
}

inline int espIdfLogPrefixEnd(const QString &line, int prefixStart)
{
    if (prefixStart + 4 >= line.size()) {
        return -1;
    }

    const QColor levelColor = terminalEspIdfLogLevelColor(line.at(prefixStart));
    if (!levelColor.isValid() || line.at(prefixStart + 1) != QLatin1Char(' ') ||
        line.at(prefixStart + 2) != QLatin1Char('(')) {
        return -1;
    }

    int cursor = prefixStart + 3;
    const int digitStart = cursor;
    while (cursor < line.size() && line.at(cursor).isDigit()) {
        ++cursor;
    }

    if (cursor == digitStart || cursor >= line.size() || line.at(cursor) != QLatin1Char(')')) {
        return -1;
    }

    return cursor + 1;
}

inline void setFixedControlWidth(QWidget *widget, int width)
{
    widget->setMinimumWidth(width);
    widget->setMaximumWidth(width);
}

inline void hideCardHeader(HeaderCardWidget *card)
{
    card->setTitle(QString());
    if (card->titleLabel()) {
        card->titleLabel()->hide();
    }
    if (card->headerView()) {
        card->headerView()->hide();
    }
    if (card->separator()) {
        card->separator()->hide();
    }
}

inline void hideCardTitle(HeaderCardWidget *card)
{
    card->setTitle(QString());
    if (card->titleLabel()) {
        card->titleLabel()->hide();
    }
}

inline QVBoxLayout *cardBody(HeaderCardWidget *card, int margin = 12)
{
    card->setBorderRadius(8);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    card->viewLayout()->setContentsMargins(0, 0, 0, 0);

    auto *body = new QWidget(card->view());
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(8);
    card->viewLayout()->addWidget(body, 1);
    return layout;
}

inline void makeCompactControl(QWidget *control)
{
    control->setMinimumWidth(0);
    control->setFixedHeight(CompactControlHeight);
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
}

inline void setButtonRowControlPolicy(QWidget *control)
{
    control->setMinimumWidth(0);
    control->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

inline void addFormRow(QVBoxLayout *root, const QString &labelText, QWidget *control, QWidget *extra = nullptr)
{
    auto *parent = root->parentWidget();
    auto *row = new QHBoxLayout;
    row->setSpacing(8);

    auto *label = new BodyLabel(labelText, parent);
    setFixedControlWidth(label, 52);
    row->addWidget(label, 0, Qt::AlignVCenter);

    control->setMinimumWidth(0);
    control->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    row->addWidget(control, 1, Qt::AlignVCenter);

    if (extra) {
        row->addWidget(extra, 0, Qt::AlignVCenter);
    }

    root->addLayout(row);
}

inline QSerialPort::DataBits dataBitsFromText(const QString &text)
{
    if (text == QStringLiteral("5")) {
        return QSerialPort::Data5;
    }
    if (text == QStringLiteral("6")) {
        return QSerialPort::Data6;
    }
    if (text == QStringLiteral("7")) {
        return QSerialPort::Data7;
    }
    return QSerialPort::Data8;
}

inline QSerialPort::Parity parityFromData(const QVariant &data)
{
    return static_cast<QSerialPort::Parity>(data.toInt());
}

inline QSerialPort::StopBits stopBitsFromData(const QVariant &data)
{
    return static_cast<QSerialPort::StopBits>(data.toInt());
}

inline QSerialPort::FlowControl flowControlFromData(const QVariant &data)
{
    return static_cast<QSerialPort::FlowControl>(data.toInt());
}

inline QString defaultExportFolder()
{
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

inline QString modeLabel(const QString &mode)
{
    return mode == QStringLiteral("hex") ? QStringLiteral("HEX") : QStringLiteral("文本");
}

inline QString lineEndingLabel(const QString &lineEnding)
{
    if (lineEnding == QStringLiteral("cr")) {
        return QStringLiteral("CR");
    }
    if (lineEnding == QStringLiteral("lf")) {
        return QStringLiteral("LF");
    }
    if (lineEnding == QStringLiteral("crlf")) {
        return QStringLiteral("CRLF");
    }
    return QStringLiteral("None");
}

inline QByteArray lineEndingBytes(const QString &lineEnding)
{
    if (lineEnding == QStringLiteral("cr")) {
        return QByteArray("\r", 1);
    }
    if (lineEnding == QStringLiteral("lf")) {
        return QByteArray("\n", 1);
    }
    if (lineEnding == QStringLiteral("crlf")) {
        return QByteArray("\r\n", 2);
    }
    return {};
}

inline QString formatBytes(qint64 bytes)
{
    const double value = static_cast<double>(bytes);
    if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return QStringLiteral("%1 KB").arg(value / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(value / (1024.0 * 1024.0), 0, 'f', 1);
}

inline QString formatBytesPerSecond(qint64 bytes) { return QStringLiteral("%1/s").arg(formatBytes(bytes)); }

inline QString formatDuration(qint64 seconds)
{
    const qint64 hours = seconds / 3600;
    const qint64 minutes = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(secs, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(secs, 2, 10, QLatin1Char('0'));
}

inline QStringList commonBaudRateTexts()
{
    QList<qint32> rates = QSerialPortInfo::standardBaudRates();
    const QList<qint32> additionalRates = {110,    300,    600,    1200,    2400,    4800,    9600,    14400,  19200,
                                           38400,  56000,  57600,  74880,   115200,  128000,  230400,  250000, 256000,
                                           460800, 500000, 921600, 1000000, 1500000, 2000000, 3000000, 4000000};
    for (qint32 rate : additionalRates) {
        if (!rates.contains(rate)) {
            rates.append(rate);
        }
    }

    std::sort(rates.begin(), rates.end());
    QStringList texts;
    qint32 previous = 0;
    for (qint32 rate : rates) {
        if (rate <= 0 || rate == previous) {
            continue;
        }
        texts.append(QString::number(rate));
        previous = rate;
    }
    return texts;
}

inline QColor defaultTxColor() { return QColor(QStringLiteral("#ff9f0a")); }

inline void addEncodingOptions(ComboBox *combo)
{
    for (const AppTextEncoding::EncodingOption &option : AppTextEncoding::options()) {
        combo->addItem(option.label, QIcon(), option.key);
    }
}

inline void selectEncodingOption(ComboBox *combo, const QString &key)
{
    const int index = combo->findData(AppTextEncoding::normalizedKey(key));
    combo->setCurrentIndex(index >= 0 ? index : combo->findData(AppTextEncoding::defaultKey()));
}

inline QString qssString(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(value);
}

inline QString csvEscape(QString text)
{
    text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"%1\"").arg(text);
}

} // namespace WorkbenchPagePrivate
