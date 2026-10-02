#include "app/appchecker.h"

#include "pipeline/releasepipeline.h"

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
        source->setConfig(m_sourceConfig.value(source->id()));

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
