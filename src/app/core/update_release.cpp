#include "app/core/update_release.h"
#include "app/core/app_i18n.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QSysInfo>
#include <QtCore/QVersionNumber>

namespace AppUpdate {

QString currentPlatformKey()
{
    const QString architecture = QSysInfo::buildCpuArchitecture();
    const bool arm64 = architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64");
    const bool x64 = architecture == QStringLiteral("x86_64");
#if defined(Q_OS_WIN)
    return x64 ? QStringLiteral("windows-x64") : QString();
#elif defined(Q_OS_MACOS)
    return arm64 ? QStringLiteral("macos-arm64") : QString();
#elif defined(Q_OS_LINUX)
    return arm64 ? QStringLiteral("linux-arm64") : (x64 ? QStringLiteral("linux-x64") : QString());
#else
    Q_UNUSED(arm64)
    Q_UNUSED(x64)
    return {};
#endif
}

bool parseRelease(const QByteArray &json, const QString &platformKey, const QString &currentVersion,
                  ReleaseInfo *release, bool *updateAvailable, QString *error)
{
    *release = {};
    *updateAvailable = false;
    error->clear();
    const auto fail = [error](const QString &message) {
        *error = message;
        return false;
    };
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject()) {
        return fail(AppI18n::text("更新信息格式无效"));
    }
    const QJsonObject object = document.object();
    const QString tag = object.value(QStringLiteral("tag_name")).toString();
    const QRegularExpression versionPattern(QStringLiteral("^v?(\\d+\\.\\d+\\.\\d+)$"));
    const auto versionMatch = versionPattern.match(tag);
    if (!versionMatch.hasMatch() || object.value(QStringLiteral("draft")).toBool() ||
        object.value(QStringLiteral("prerelease")).toBool()) {
        return fail(AppI18n::text("未找到有效的正式发布版本"));
    }
    ReleaseInfo result;
    result.version = versionMatch.captured(1);
    qsizetype suffix = 0;
    const auto latest = QVersionNumber::fromString(result.version, &suffix);
    if (suffix != result.version.size() || latest.segmentCount() != 3) {
        return fail(AppI18n::text("更新版本号无效"));
    }
    QString normalizedCurrent = currentVersion.trimmed();
    if (normalizedCurrent.startsWith(QLatin1Char('v'))) {
        normalizedCurrent.remove(0, 1);
    }
    const auto current = QVersionNumber::fromString(normalizedCurrent, &suffix);
    if (current.isNull() || suffix != normalizedCurrent.size()) {
        return fail(AppI18n::text("当前应用版本号无效"));
    }
    result.releaseUrl = QUrl(object.value(QStringLiteral("html_url")).toString());
    const QUrl expectedRelease(
        QStringLiteral("https://github.com/txp666/FluentSerialAssistant/releases/tag/%1").arg(tag));
    if (result.releaseUrl != expectedRelease) {
        return fail(AppI18n::text("更新发布地址无效"));
    }
    result.notes = object.value(QStringLiteral("body")).toString();
    if (result.notes.isEmpty()) {
        result.notes = AppI18n::text("此版本未提供更新说明。");
    }
    if (QVersionNumber::compare(latest, current) <= 0) {
        *release = result;
        return true;
    }

    QString suffixName;
    if (platformKey == QStringLiteral("windows-x64")) {
        suffixName = QStringLiteral("windows-x64-setup.exe");
    } else if (platformKey == QStringLiteral("macos-arm64")) {
        suffixName = QStringLiteral("macos-arm64.dmg");
    } else if (platformKey == QStringLiteral("linux-x64") || platformKey == QStringLiteral("linux-arm64")) {
        suffixName = platformKey + QStringLiteral(".deb");
    } else {
        return fail(AppI18n::text("此系统或架构暂不支持自动安装更新"));
    }
    result.assetName = QStringLiteral("FluentSerialAssistant-%1-%2").arg(result.version, suffixName);
    const QUrl expectedDownload(
        QStringLiteral("https://github.com/txp666/FluentSerialAssistant/releases/download/%1/%2")
            .arg(tag, result.assetName));
    int matches = 0;
    for (const auto &value : object.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString() != result.assetName) {
            continue;
        }
        ++matches;
        result.downloadUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        result.size = asset.value(QStringLiteral("size")).toInteger();
        const auto digest = asset.value(QStringLiteral("digest")).toString();
        const QRegularExpression hashPattern(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
        const auto hashMatch = hashPattern.match(digest);
        if (asset.value(QStringLiteral("state")).toString() != QStringLiteral("uploaded") ||
            result.downloadUrl != expectedDownload || result.size <= 0 || result.size > 4LL * 1024 * 1024 * 1024 ||
            !hashMatch.hasMatch()) {
            return fail(AppI18n::text("安装包地址、大小或 SHA-256 校验信息无效"));
        }
        result.sha256 = hashMatch.captured(1).toLatin1().toLower();
    }
    if (matches != 1) {
        return fail(AppI18n::text("未找到适用于当前系统的唯一安装包"));
    }
    *release = result;
    *updateAvailable = true;
    return true;
}

} // namespace AppUpdate
