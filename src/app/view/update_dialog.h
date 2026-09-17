#pragma once

#include <FluentQtWidgets/Dialogs/Dialog.h>

#include <QtCore/QPointer>

namespace AppUpdate {
class UpdateManager;
}

namespace FluentQt {
class BodyLabel;
class CaptionLabel;
class ProgressBar;
class SubtitleLabel;
class TextBrowser;
} // namespace FluentQt

class UpdateDialog : public FluentQt::MessageBoxBase
{
    Q_OBJECT

  public:
    explicit UpdateDialog(AppUpdate::UpdateManager *manager, QWidget *parent = nullptr);
    ~UpdateDialog() override;

  public slots:
    void accept() override;
    void reject() override;

  signals:
    void downloadRequested();

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void refreshState();
    void refreshProgress(qint64 received, qint64 total);
    void cancelActiveDownload();

    QPointer<AppUpdate::UpdateManager> m_manager;
    FluentQt::SubtitleLabel *m_titleLabel = nullptr;
    FluentQt::BodyLabel *m_versionLabel = nullptr;
    FluentQt::TextBrowser *m_releaseNotes = nullptr;
    FluentQt::BodyLabel *m_statusLabel = nullptr;
    FluentQt::CaptionLabel *m_progressLabel = nullptr;
    FluentQt::ProgressBar *m_progressBar = nullptr;
};
