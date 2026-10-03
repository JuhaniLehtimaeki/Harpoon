#include "sources/source.h"

#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

QStringList Source::hosts() const
{
    return m_customHost.isEmpty() ? defaultHosts() : QStringList{m_customHost};
}

QString Source::hostPrefixPattern() const
{
    QStringList escaped;
    for (const QString &host : hosts())
        escaped << QRegularExpression::escape(host);
    const QString subdomains = allowSubDomains() ? QStringLiteral("([^./]+\\.)*") : QStringLiteral("(www\\.)?");
    return QStringLiteral("^https?://%1(%2)").arg(subdomains, escaped.join(QLatin1Char('|')));
}

QString Source::configToken() const
{
    return m_config.value(QStringLiteral("token")).toString().trimmed();
}

Result<QString> Source::standardizeWithRegex(const QString &url, const QString &pathPattern) const
{
    const QRegularExpression pattern(hostPrefixPattern() + pathPattern, QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(url);
    if (!match.hasMatch())
        return Result<QString>::failure(
            Error::make(Error::InvalidUrl, QStringLiteral("Not a valid %1 URL: %2").arg(displayName(), url)));
    // One spelling per repository: later comparisons (feed ids, asset URLs,
    // credential origins) are with the URLs the server issues.
    const QString matched = match.captured(0);
    const int schemeEnd = matched.indexOf(QLatin1String("://"));
    const int hostEnd = matched.indexOf(QLatin1Char('/'), schemeEnd + 3);
    QString scheme = matched.left(schemeEnd).toLower();
    QString host = matched.mid(schemeEnd + 3, (hostEnd < 0 ? matched.size() : hostEnd) - schemeEnd - 3).toLower();
    const QString rest = hostEnd < 0 ? QString() : matched.mid(hostEnd);
    if (m_customHost.isEmpty()) {
        // Public forges are https-only; a self-hosted server keeps its scheme.
        scheme = QStringLiteral("https");
        if (host.startsWith(QLatin1String("www.")) && hosts().contains(host.mid(4)))
            host = host.mid(4);
    }
    return Result<QString>::success(scheme + QStringLiteral("://") + host + rest);
}

void Source::prepareDownload(const Asset &, const AppSettings &, const QString &, DownloadRequest &) const {}

bool Source::isOwnOrigin(const QString &url, const QString &standardUrl)
{
    const QUrl a(url);
    const QUrl b(standardUrl);
    return a.isValid() && !a.host().isEmpty() && a.scheme().compare(b.scheme(), Qt::CaseInsensitive) == 0
           && a.host().compare(b.host(), Qt::CaseInsensitive) == 0
           && a.port(a.scheme() == QLatin1String("http") ? 80 : 443) == b.port(b.scheme() == QLatin1String("http") ? 80 : 443);
}

QString preStandardizeUrl(const QString &input)
{
    QString url = input.trimmed();
    if (!url.contains(QLatin1String("://")))
        url.prepend(QStringLiteral("https://"));

    // Collapse duplicate slashes in the path only (not in "https://" or the query).
    const int schemeEnd = url.indexOf(QLatin1String("://")) + 3;
    int pathEnd = url.size();
    const int query = url.indexOf(QLatin1Char('?'), schemeEnd);
    const int fragment = url.indexOf(QLatin1Char('#'), schemeEnd);
    if (query >= 0)
        pathEnd = query;
    if (fragment >= 0 && fragment < pathEnd)
        pathEnd = fragment;
    QString main = url.mid(schemeEnd, pathEnd - schemeEnd);
    main.replace(QRegularExpression(QStringLiteral("/{2,}")), QStringLiteral("/"));
    return url.left(schemeEnd) + main + url.mid(pathEnd);
}

} // namespace Harpoon
