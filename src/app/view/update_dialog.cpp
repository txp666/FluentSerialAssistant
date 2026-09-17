#include "app/view/update_dialog.h"

#include "app/core/app_i18n.h"
#include "app/core/update_manager.h"

#include <FluentQtWidgets/Config.h>
#include <FluentQtWidgets/Widgets/Label.h>
#include <FluentQtWidgets/Widgets/LineEdit.h>
#include <FluentQtWidgets/Widgets/ProgressBar.h>

#include <QtCore/QCoreApplication>
#include <QtCore/QLocale>
#include <QtGui/QCloseEvent>

using namespace FluentQt;
using UpdateState = AppUpdate::UpdateManager::State;

namespace {

bool canDownload(UpdateState state)
{
    return state == UpdateState::Available || state == UpdateState::Failed || state == UpdateState::Cancelled;
}

} // namespace

UpdateDialog::UpdateDialog(AppUpdate::UpdateManager *manager, QWidget *parent)
    : MessageBoxBase(parent), m_manager(manager)
{
    setObjectName(QStringLiteral("applicationUpdateDialog"));
    setWindowTitle(AppI18n::text("应用更新"));
    setClosableOnMaskClicked(false);
    // The base layout's SetMinimumSize would replace the explicit reading width
    // with the text browser's much narrower minimum after each layout pass.
    vBoxLayout()->setSizeConstraint(QLayout::SetNoConstraint);
    view()->setMinimumWidth(560);
    view()->setMaximumWidth(680);

    m_titleLabel = new SubtitleLabel(AppI18n::text("发现新版本"), view());
    m_titleLabel->setWordWrap(true);
    m_versionLabel = new BodyLabel(view());
    m_versionLabel->setWordWrap(true);
    m_versionLabel->setObjectName(QStringLiteral("updateVersionLabel"));
    m_releaseNotes = new TextBrowser(view());
    m_releaseNotes->setObjectName(QStringLiteral("updateReleaseNotes"));
    m_releaseNotes->setOpenExternalLinks(true);
    m_releaseNotes->setMinimumHeight(160);
    m_releaseNotes->setMaximumHeight(260);
    m_releaseNotes->setReadOnly(true);

    auto *notice = new BodyLabel(AppI18n::text("下载完成并校验通过后，应用将退出并安装更新。"), view());
    notice->setObjectName(QStringLiteral("updateInstallNotice"));
    notice->setWordWrap(true);
    m_statusLabel = new BodyLabel(view());
    m_statusLabel->setObjectName(QStringLiteral("updateStatusLabel"));
    m_statusLabel->setWordWrap(true);
    m_progressBar = new ProgressBar(view(), false);
    m_progressBar->setObjectName(QStringLiteral("updateDownloadProgress"));
    m_progressBar->setRange(0, 100);
    m_progressLabel = new CaptionLabel(view());
    m_progressLabel->setObjectName(QStringLiteral("updateDownloadBytes"));
    m_progressLabel->setWordWrap(true);

    viewLayout()->addWidget(m_titleLabel);
    viewLayout()->addWidget(m_versionLabel);
    viewLayout()->addWidget(m_releaseNotes, 1);
    viewLayout()->addWidget(notice);
    viewLayout()->addWidget(m_statusLabel);
    viewLayout()->addWidget(m_progressBar);
    viewLayout()->addWidget(m_progressLabel);
    yesButton()->setObjectName(QStringLiteral("updateDownloadButton"));
    cancelButton()->setObjectName(QStringLiteral("updateCancelButton"));

    if (m_manager) {
        connect(m_manager, &AppUpdate::UpdateManager::stateChanged, this, &UpdateDialog::refreshState);
        connect(m_manager, &AppUpdate::UpdateManager::progressChanged, this, &UpdateDialog::refreshProgress);
        connect(this, &UpdateDialog::downloadRequested, m_manager, &AppUpdate::UpdateManager::downloadUpdate);
    }
    connect(FluentConfig::instance(), &FluentConfig::localeNameChanged, this, &UpdateDialog::refreshState);
    refreshState();
}

UpdateDialog::~UpdateDialog() { cancelActiveDownload(); }

void UpdateDialog::accept()
{
    if (m_manager && canDownload(m_manager->state()) && !m_manager->release().version.isEmpty()) {
        emit downloadRequested();
    }
}

void UpdateDialog::reject()
{
    if (m_manager && m_manager->state() == UpdateState::Installing) {
        return;
    }
    cancelActiveDownload();
    MessageBoxBase::reject();
}

void UpdateDialog::closeEvent(QCloseEvent *event)
{
    if (m_manager && m_manager->state() == UpdateState::Installing) {
        event->ignore();
        return;
    }
    cancelActiveDownload();
    MessageBoxBase::closeEvent(event);
}

void UpdateDialog::cancelActiveDownload()
{
    if (m_manager && (m_manager->state() == UpdateState::Downloading || m_manager->state() == UpdateState::Verifying)) {
        m_manager->cancelDownload();
    }
}

void UpdateDialog::refreshState()
{
    if (!m_manager) {
        yesButton()->setEnabled(false);
        return;
    }

    const auto state = m_manager->state();
    const auto &release = m_manager->release();
    m_titleLabel->setText(state == UpdateState::Failed ? AppI18n::text("更新失败")
                                                       : AppI18n::text("发现新版本 %1").arg(release.version));
    m_versionLabel->setText(
        AppI18n::text("当前 %1，最新 %2").arg(QCoreApplication::applicationVersion(), release.version));
    if (m_releaseNotes->property("releaseNotes").toString() != release.notes) {
        m_releaseNotes->setProperty("releaseNotes", release.notes);
        m_releaseNotes->setMarkdown(release.notes.isEmpty() ? AppI18n::text("此版本未提供更新说明。") : release.notes);
    } else if (release.notes.isEmpty()) {
        m_releaseNotes->setPlainText(AppI18n::text("此版本未提供更新说明。"));
    }

    yesButton()->setEnabled(canDownload(state) && !release.version.isEmpty());
    yesButton()->setText(state == UpdateState::Failed || state == UpdateState::Cancelled
                             ? AppI18n::text("重试下载并安装")
                             : AppI18n::text("下载并安装"));
    cancelButton()->setEnabled(state != UpdateState::Installing);
    cancelButton()->setText(state == UpdateState::Downloading || state == UpdateState::Verifying
                                ? AppI18n::text("取消更新")
                                : AppI18n::text("稍后"));
    m_statusLabel->setText(m_manager->statusMessage());
    m_statusLabel->setVisible(!m_manager->statusMessage().isEmpty());
    const bool showProgress = state == UpdateState::Downloading || state == UpdateState::Verifying ||
                              state == UpdateState::Installing || state == UpdateState::Failed ||
                              state == UpdateState::Cancelled;
    m_progressBar->setVisible(showProgress);
    m_progressLabel->setVisible(showProgress);
    m_progressBar->setError(state == UpdateState::Failed);
    m_progressBar->setPaused(state == UpdateState::Cancelled);
    refreshProgress(m_manager->receivedBytes(), m_manager->totalBytes());
}

void UpdateDialog::refreshProgress(qint64 received, qint64 total)
{
    const qint64 boundedReceived = qMax<qint64>(0, received);
    const int percentage = total > 0 ? static_cast<int>(100.0 * qMin(boundedReceived, total) / total) : 0;
    m_progressBar->setValue(percentage);
    const QLocale locale;
    m_progressLabel->setText(AppI18n::text("%1% · %2 / %3")
                                 .arg(percentage)
                                 .arg(locale.formattedDataSize(boundedReceived),
                                      total > 0 ? locale.formattedDataSize(total) : AppI18n::text("未知")));
    m_progressLabel->setToolTip(
        AppI18n::text("%1 / %2 字节").arg(locale.toString(boundedReceived), locale.toString(qMax<qint64>(0, total))));
}
