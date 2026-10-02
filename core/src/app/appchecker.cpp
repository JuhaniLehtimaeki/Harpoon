#include "app/appchecker.h"

#include "pipeline/releasepipeline.h"
#include "sources/githubattestation.h"
#include "sources/githubsource.h"

#include <memory>

namespace Harpoon {

AppChecker::AppChecker(const SourceRegistry &registry, HttpTransport &transport, const DeviceInfo &device,
                       QObject *parent)
    : QObject(parent), m_registry(registry), m_transport(transport), m_device(device)
{
}

void AppChecker::check(const App &app, AppDone done)
{
    m_pending.push_back([this, app, done]() {
        auto finish = [this, done](const App &updated, const Error &error) {
            --m_running;
            done(updated, error);
            pump();
        };

        const auto match = m_registry.match(app.url, app.sourceId);
        if (!match.ok()) {
            App updated = app;
            updated.lastCheck = QDateTime::currentDateTimeUtc();
            updated.lastError = match.error.message;
            finish(updated, match.error);
            return;
        }
        std::shared_ptr<Source> source = match.value.source;
        source->setConfig(m_sourceConfig.value(source->tokenKey()));

        fetchLatestRelease(*source, m_transport, match.value.standardUrl, app.settings, m_device,
                           [app, source, finish](const Result<LatestRelease> &result) {
                               App updated = app;
                               updated.lastCheck = QDateTime::currentDateTimeUtc();
                               if (!result.ok()) {
                                   updated.lastError = result.error.message;
                                   finish(updated, result.error);
                                   return;
                               }
                               const LatestRelease &l = result.value;
                               updated.lastError.clear();
                               updated.latestVersion = l.version;
                               updated.latestTag = l.release.tag;
                               updated.latestTitle = l.release.title;
                               updated.latestDate = l.release.date;
                               updated.changelog = l.release.changelog;
                               updated.releasePageUrl = l.release.pageUrl;
                               updated.latestPrerelease = l.release.prerelease;
                               updated.latestAssets = l.assets;
                               finish(updated, Error());
                           });
    });
    pump();
}

std::function<void(const Asset &, DownloadRequest &)> AppChecker::downloadPreparer(const App &app) const
{
    const auto match = m_registry.match(app.url, app.sourceId);
    if (!match.ok())
        return nullptr;
    std::shared_ptr<Source> source = match.value.source;
    source->setConfig(m_sourceConfig.value(source->tokenKey()));
    const AppSettings settings = app.settings;
    const QString standardUrl = match.value.standardUrl;
    return [source, settings, standardUrl](const Asset &asset, DownloadRequest &request) {
        source->prepareDownload(asset, settings, standardUrl, request);
    };
}

AppInstaller::Verifier AppChecker::verifier(const App &app) const
{
    const QString mode = app.settings.getString(Keys::githubBuildVerificationMode, QStringLiteral("off"));
    if (mode != QLatin1String("audit") && mode != QLatin1String("enforce"))
        return nullptr;
    const auto match = m_registry.match(app.url, app.sourceId);
    if (!match.ok() || match.value.source->id() != QLatin1String("GitHub"))
        return nullptr;
    const auto *github = static_cast<const GitHubSource *>(match.value.source.get());
    const QString apiBase = github->apiBaseUrl(match.value.standardUrl);
    const QString token = m_sourceConfig.value(github->tokenKey()).value(QStringLiteral("token")).toString().trimmed();
    const bool enforce = mode == QLatin1String("enforce");
    HttpTransport *transport = &m_transport;

    return [transport, apiBase, token, enforce](const App &, const QStringList &files, const QStringList &sha256s,
                                                std::function<void(const AppInstaller::Verification &)> done) {
        struct State
        {
            int remaining;
            AttestationStatus worst = AttestationStatus::Verified;
            QStringList problems;
        };
        auto state = std::make_shared<State>();
        state->remaining = sha256s.size();
        auto finish = [state, enforce, done]() {
            AppInstaller::Verification v;
            v.status = QStringLiteral("attestation:") + attestationStatusName(state->worst);
            if (state->worst != AttestationStatus::Verified) {
                const QString message = QStringLiteral("Build provenance not confirmed: %1")
                                            .arg(state->problems.join(QStringLiteral("; ")));
                if (enforce)
                    v.error = Error::make(Error::Verification, message);
                else
                    v.warnings << message;
            }
            done(v);
        };
        if (sha256s.isEmpty()) {
            finish();
            return;
        }
        for (int i = 0; i < sha256s.size(); ++i) {
            const QString name = files.value(i).section(QLatin1Char('/'), -1);
            checkGitHubAttestation(*transport, apiBase, token, sha256s.at(i),
                                   [state, name, finish](const AttestationResult &r) {
                                       if (r.status != AttestationStatus::Verified) {
                                           // Error outranks Missing.
                                           if (state->worst != AttestationStatus::Error)
                                               state->worst = r.status;
                                           state->problems << QStringLiteral("%1: %2").arg(name, r.message);
                                       }
                                       if (--state->remaining == 0)
                                           finish();
                                   });
        }
    };
}

void AppChecker::checkAll(const QList<App> &apps, AppDone perApp, std::function<void()> allDone)
{
    if (apps.isEmpty()) {
        allDone();
        return;
    }
    auto remaining = std::make_shared<int>(apps.size());
    for (const App &app : apps)
        check(app, [perApp, allDone, remaining](const App &updated, const Error &error) {
            perApp(updated, error);
            if (--*remaining == 0)
                allDone();
        });
}

void AppChecker::pump()
{
    while (m_running < m_maxConcurrent && !m_pending.empty()) {
        auto job = std::move(m_pending.front());
        m_pending.pop_front();
        ++m_running;
        job();
    }
}

} // namespace Harpoon
