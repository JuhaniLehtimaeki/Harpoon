#pragma once

#include "model/appsettings.h"
#include "model/error.h"
#include "model/release.h"
#include "net/httptransport.h"

#include <QStringList>
#include <QVariantMap>

#include <functional>

namespace Harpoon {

struct FetchResult
{
    QList<Release> releases; // in the forge's own order
    Error error;
};

// A place releases come from (one per forge type). Mirrors ObtainX's AppSource,
// with one deliberate difference: fetchReleases() returns every candidate
// release and leaves selection to the shared ReleaseSelector.
//
// fetchReleases() may complete asynchronously; the Source must outlive the
// callback.
class Source
{
public:
    using Callback = std::function<void(const FetchResult &)>;

    virtual ~Source() = default;

    // Stable identifier, stored per app as overrideSource. Matches the ObtainX
    // class name where one exists.
    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual QStringList defaultHosts() const = 0;
    virtual bool allowSubDomains() const { return false; }
    // Hostless sources with this flag are only used when chosen explicitly
    // (override id), never by URL matching (ObtainX neverAutoSelect).
    virtual bool neverAutoSelect() const { return false; }
    // Hostless sources (empty defaultHosts) are tried in registry order after
    // every host-based source; this decides whether one takes a URL by its
    // shape (ObtainX sourceSpecificStandardizeURL(forSelection: true)).
    virtual bool matchesUrlShape(const QString &url) const
    {
        Q_UNUSED(url);
        return false;
    }

    // Hosts this instance answers for: the defaults, or the single custom host
    // when the user forced this source onto another server (self-hosted forge).
    QStringList hosts() const;
    void setCustomHost(const QString &host) { m_customHost = host.toLower(); }
    QString customHost() const { return m_customHost; }

    // Global per-source configuration (e.g. "token").
    void setConfig(const QVariantMap &config) { m_config = config; }
    const QVariantMap &config() const { return m_config; }

    // Canonical URL for an app, or InvalidUrl.
    virtual Result<QString> standardizeUrl(const QString &url) const = 0;

    virtual void fetchReleases(const QString &standardUrl, const AppSettings &settings,
                               HttpTransport &transport, Callback done) = 0;

protected:
    // Matches ^https?://(www\.)?(<hosts>)<pathPattern> case-insensitively and
    // returns the matched prefix.
    Result<QString> standardizeWithRegex(const QString &url, const QString &pathPattern) const;
    // "^https?://(www\.)?(<hosts>)" (or the subdomain variant), for sources
    // that need several regexes over the same hosts.
    QString hostPrefixPattern() const;
    // The token from config("token"), trimmed; empty when none.
    QString configToken() const;

private:
    QString m_customHost;
    QVariantMap m_config;
};

// Adds https:// when the scheme is missing, trims whitespace and collapses
// duplicate slashes in the path (ObtainX preStandardizeUrl).
QString preStandardizeUrl(const QString &url);

} // namespace Harpoon
