#include "sources/source.h"

#include <QRegularExpression>

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
    return Result<QString>::success(match.captured(0));
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
