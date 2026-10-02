#pragma once

#include "app/app.h"
#include "net/downloader.h"
#include "pipeline/deviceinfo.h"
#include "pkg/packagebackend.h"
#include "pkg/rpminspector.h"

#include <QObject>

#include <functional>
#include <memory>

namespace Harpoon {

struct InstallJob;

struct InstallResult
{
    App app;              // updated record (id may have changed from a temporary one)
    RpmInfo installed;    // main package as installed
    QStringList warnings; // e.g. vendor change
};

// Installs the latest release of an app:
//   download every selected asset (sha256-verified when published)
//   -> read the RPM headers
//   -> check arch, package name vs app id, EVR vs installed, vendor
//   -> install all files in one package-manager transaction
//   -> confirm with rpm that the expected EVR is now installed
//   -> record an install receipt and delete the downloads
class AppInstaller : public QObject
{
    Q_OBJECT
public:
    using Progress = std::function<void(const QString &stage, qint64 done, qint64 total)>;
    using Done = std::function<void(const Result<InstallResult> &)>;
    // Adjusts each asset download (credentials, headers); see Source::prepareDownload.
    using DownloadPreparer = std::function<void(const Asset &, DownloadRequest &)>;
    // Checks downloaded files before they are installed. Reports a blocking
    // error, warnings, and a short status recorded in the install receipt.
    struct Verification
    {
        Error error;
        QStringList warnings;
        QString status;
    };
    using Verifier = std::function<void(const App &, const QStringList &files, const QStringList &sha256s,
                                        std::function<void(const Verification &)>)>;

    AppInstaller(Downloader &downloader, RpmInspector &inspector, PackageBackend &backend,
                 const QString &downloadDir, const DeviceInfo &device, QObject *parent = nullptr);

    void install(const App &app, const InstallOptions &options, Progress progress, Done done);
    void setDownloadPreparer(DownloadPreparer preparer) { m_prepareDownload = std::move(preparer); }
    void setVerifier(Verifier verifier) { m_verify = std::move(verifier); }
    void uninstall(const App &app, std::function<void(const Error &)> done);

    // Picks the package that represents the app among a release's RPMs:
    // the one named appId if given, else a name that prefixes all others
    // (app + app-data), else one matching the repository name, else the first.
    static int mainPackageIndex(const QList<RpmInfo> &packages, const QString &appId, const QString &repoName);

private:
    void downloadNext(std::shared_ptr<InstallJob> job);
    void verifyAndInstall(std::shared_ptr<InstallJob> job);

    Downloader &m_downloader;
    RpmInspector &m_inspector;
    PackageBackend &m_backend;
    QString m_downloadDir;
    DeviceInfo m_device;
    DownloadPreparer m_prepareDownload;
    Verifier m_verify;
};

} // namespace Harpoon
