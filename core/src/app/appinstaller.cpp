#include "app/appinstaller.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUrl>

#include <memory>

namespace Harpoon {

struct InstallJob
{
    App app;
    InstallOptions options;
    AppInstaller::Progress progress;
    AppInstaller::Done done;
    int next = 0;          // next asset to download
    QStringList files;     // downloaded paths, same order as app.latestAssets
};

namespace {

Result<InstallResult> fail(Error::Kind kind, const QString &message)
{
    return Result<InstallResult>::failure(Error::make(kind, message));
}

void cleanUp(const QStringList &files)
{
    for (const QString &f : files)
        QFile::remove(f);
}

} // namespace

AppInstaller::AppInstaller(Downloader &downloader, RpmInspector &inspector, PackageBackend &backend,
                           const QString &downloadDir, const DeviceInfo &device, QObject *parent)
    : QObject(parent)
    , m_downloader(downloader)
    , m_inspector(inspector)
    , m_backend(backend)
    , m_downloadDir(downloadDir)
    , m_device(device)
{
}

int AppInstaller::mainPackageIndex(const QList<RpmInfo> &packages, const QString &appId, const QString &repoName)
{
    if (packages.isEmpty())
        return -1;
    if (!appId.isEmpty()) {
        for (int i = 0; i < packages.size(); ++i)
            if (packages.at(i).name == appId)
                return i;
        return -1;
    }
    for (int i = 0; i < packages.size(); ++i) {
        bool prefixOfAll = true;
        for (const RpmInfo &other : packages)
            if (!other.name.startsWith(packages.at(i).name))
                prefixOfAll = false;
        if (prefixOfAll)
            return i;
    }
    for (int i = 0; i < packages.size(); ++i)
        if (packages.at(i).name.compare(repoName, Qt::CaseInsensitive) == 0)
            return i;
    return 0;
}

void AppInstaller::install(const App &app, const InstallOptions &options, Progress progress, Done done)
{
    if (app.settings.getBool(Keys::trackOnly)) {
        done(fail(Error::InvalidSetting, QStringLiteral("%1 is track-only; there is nothing to install").arg(app.name)));
        return;
    }
    if (app.latestAssets.isEmpty()) {
        done(fail(Error::NoAsset, QStringLiteral("No installable package known for %1; check for updates first").arg(app.name)));
        return;
    }
    auto job = std::make_shared<InstallJob>();
    job->app = app;
    job->options = options;
    job->progress = std::move(progress);
    job->done = std::move(done);
    downloadNext(job);
}

void AppInstaller::downloadNext(std::shared_ptr<InstallJob> job)
{
    if (job->next >= job->app.latestAssets.size()) {
        verifyAndInstall(job);
        return;
    }
    const Asset &asset = job->app.latestAssets.at(job->next);
    // Unique per download URL so different releases never share a file.
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(asset.url.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
    const QString safeName = QFileInfo(asset.name).fileName();

    DownloadRequest request;
    request.url = asset.url;
    request.targetPath = m_downloadDir + QLatin1Char('/') + key + QLatin1Char('-') + safeName;
    request.expectedSize = asset.size;
    request.expectedSha256 = asset.sha256;
    if (m_prepareDownload)
        m_prepareDownload(asset, request);

    const QString stage = QStringLiteral("Downloading %1").arg(asset.name);
    m_downloader.download(
        request,
        [job, stage](qint64 received, qint64 total) {
            if (job->progress)
                job->progress(stage, received, total);
        },
        [this, job](const Result<QString> &path) {
            if (!path.ok()) {
                cleanUp(job->files);
                job->done(Result<InstallResult>::failure(path.error));
                return;
            }
            job->files << path.value;
            ++job->next;
            downloadNext(job);
        });
}

void AppInstaller::verifyAndInstall(std::shared_ptr<InstallJob> job)
{
    auto abort = [job](const Result<InstallResult> &r) {
        cleanUp(job->files);
        job->done(r);
    };
    if (job->progress)
        job->progress(QStringLiteral("Checking packages"), 0, -1);

    QList<RpmInfo> packages;
    QSet<QString> names;
    for (const QString &file : job->files) {
        const auto info = m_inspector.inspectFile(file);
        if (!info.ok()) {
            abort(Result<InstallResult>::failure(info.error));
            return;
        }
        if (info.value.arch != QLatin1String("noarch") && !m_device.arch.isEmpty() && info.value.arch != m_device.arch) {
            abort(fail(Error::WrongArch, QStringLiteral("%1 is built for %2, this device is %3")
                                             .arg(QFileInfo(file).fileName(), info.value.arch, m_device.arch)));
            return;
        }
        if (names.contains(info.value.name)) {
            abort(fail(Error::InvalidSetting,
                       QStringLiteral("The release has several packages named %1; set an asset filter to pick one")
                           .arg(info.value.name)));
            return;
        }
        names.insert(info.value.name);
        packages << info.value;
    }

    const App &app = job->app;
    const QString repoName = QUrl(app.url).path().section(QLatin1Char('/'), -1);
    int mainIndex = mainPackageIndex(packages, app.temporaryId ? QString() : app.id, repoName);
    if (mainIndex < 0) {
        if (!app.settings.getBool(Keys::allowIdChange)) {
            QStringList found;
            for (const RpmInfo &p : packages)
                found << p.name;
            abort(fail(Error::IdChanged, QStringLiteral("The release contains %1, not %2. Set allowIdChange to accept.")
                                             .arg(found.join(QStringLiteral(", ")), app.id)));
            return;
        }
        mainIndex = mainPackageIndex(packages, QString(), repoName);
    }
    const RpmInfo main = packages.at(mainIndex);

    const auto installed = m_inspector.installedPackage(main.name);
    if (!installed.ok()) {
        abort(Result<InstallResult>::failure(installed.error));
        return;
    }
    QStringList warnings;
    if (!installed.value.name.isEmpty()) {
        const int c = compareEvr(main.evr, installed.value.evr);
        if (c < 0 && !job->options.allowDowngrade) {
            abort(fail(Error::Downgrade, QStringLiteral("%1 %2 is older than the installed %3")
                                             .arg(main.name, main.evr.toString(), installed.value.evr.toString())));
            return;
        }
        if (c == 0 && !job->options.allowReinstall) {
            abort(fail(Error::AlreadyInstalled,
                       QStringLiteral("%1 %2 is already installed").arg(main.name, main.evr.toString())));
            return;
        }
        if (!installed.value.vendor.isEmpty() && installed.value.vendor != main.vendor)
            warnings << QStringLiteral("Vendor changes from \"%1\" to \"%2\"; the package manager may refuse the update")
                            .arg(installed.value.vendor, main.vendor);
    }

    QStringList sha256s;
    for (const QString &file : job->files)
        sha256s << Downloader::sha256OfFile(file);

    auto install = [this, job, main, abort, sha256s](QStringList warnings, const QString &verification) {
        if (job->progress)
            job->progress(QStringLiteral("Installing %1").arg(main.nevra()), 0, -1);

        m_backend.installFiles(job->files, job->options,
                               [this, job, main, warnings, abort, sha256s, verification](const Error &error) {
            if (!error.ok()) {
                abort(Result<InstallResult>::failure(error));
                return;
            }
            const auto now = m_inspector.installedPackage(main.name);
            if (!now.ok() || now.value.name.isEmpty() || compareEvr(now.value.evr, main.evr) != 0) {
                abort(fail(Error::Install, QStringLiteral("The package manager reported success, but %1 %2 is not installed")
                                               .arg(main.name, main.evr.toString())));
                return;
            }

            InstallResult result;
            result.installed = now.value;
            result.warnings = warnings;
            result.app = job->app;
            result.app.id = main.name;
            result.app.temporaryId = false;
            result.app.receipt.version = job->app.latestVersion;
            result.app.receipt.tag = job->app.latestTag;
            result.app.receipt.evr = now.value.evr.toString();
            result.app.receipt.verification = verification;
            result.app.receipt.assetNames.clear();
            for (int i = 0; i < job->files.size(); ++i)
                result.app.receipt.assetNames << job->app.latestAssets.at(i).name;
            result.app.receipt.sha256s = sha256s;
            result.app.receipt.installedAt = QDateTime::currentDateTimeUtc();
            cleanUp(job->files);
            job->done(Result<InstallResult>::success(result));
        });
    };

    if (!m_verify) {
        install(warnings, QString());
        return;
    }
    if (job->progress)
        job->progress(QStringLiteral("Verifying %1").arg(main.nevra()), 0, -1);
    m_verify(job->app, job->files, sha256s, [install, warnings, abort](const Verification &v) {
        if (!v.error.ok()) {
            abort(Result<InstallResult>::failure(v.error));
            return;
        }
        install(warnings + v.warnings, v.status);
    });
}

void AppInstaller::uninstall(const App &app, std::function<void(const Error &)> done)
{
    if (app.temporaryId) {
        done(Error::make(Error::Package, QStringLiteral("%1 has never been installed by Harpoon").arg(app.name)));
        return;
    }
    m_backend.removePackage(app.id, done);
}

} // namespace Harpoon
