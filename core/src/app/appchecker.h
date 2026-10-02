#pragma once

#include "app/app.h"
#include "net/httptransport.h"
#include "pipeline/deviceinfo.h"
#include "sources/sourceregistry.h"

#include <QObject>
#include <QVariantMap>

#include <deque>
#include <functional>

namespace Harpoon {

// Runs update checks: resolves each app's source, fetches the latest release
// and writes the result into the app record (latest*, lastCheck, lastError).
// A failed check keeps the previous latest* fields and sets lastError.
class AppChecker : public QObject
{
    Q_OBJECT
public:
    using AppDone = std::function<void(const App &updated, const Error &error)>;

    AppChecker(const SourceRegistry &registry, HttpTransport &transport, const DeviceInfo &device,
               QObject *parent = nullptr);

    // Per-source configuration (tokens), keyed by source id.
    void setSourceConfig(const QString &sourceId, const QVariantMap &config) { m_sourceConfig[sourceId] = config; }
    void setMaxConcurrent(int n) { m_maxConcurrent = qMax(1, n); }

    void check(const App &app, AppDone done);
    // Checks all apps, calling perApp for each and allDone at the end.
    void checkAll(const QList<App> &apps, AppDone perApp, std::function<void()> allDone);

private:
    void pump();

    const SourceRegistry &m_registry;
    HttpTransport &m_transport;
    DeviceInfo m_device;
    QHash<QString, QVariantMap> m_sourceConfig;
    int m_maxConcurrent = 3;
    int m_running = 0;
    std::deque<std::function<void()>> m_pending;
};

} // namespace Harpoon
