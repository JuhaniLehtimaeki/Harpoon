#include "sources/sourceregistry.h"

#include "sources/forgejosource.h"
#include "sources/githubsource.h"

#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

SourceRegistry::SourceRegistry()
{
    // Host-based sources in alphabetical order of display name. Hostless
    // catch-alls (direct link, HTML) will go last once they exist.
    registerSource([] { return std::make_shared<ForgejoSource>(); });
    registerSource([] { return std::make_shared<GitHubSource>(); });
}

void SourceRegistry::registerSource(Factory factory)
{
    m_templates.push_back(factory());
    m_factories.push_back(std::move(factory));
}

QStringList SourceRegistry::ids() const
{
    QStringList out;
    for (const auto &t : m_templates)
        out << t->id();
    return out;
}

std::shared_ptr<Source> SourceRegistry::create(const QString &id) const
{
    for (size_t i = 0; i < m_templates.size(); ++i)
        if (m_templates[i]->id() == id)
            return m_factories[i]();
    return nullptr;
}

Result<SourceMatch> SourceRegistry::match(const QString &rawUrl, const QString &overrideId) const
{
    const QString url = preStandardizeUrl(rawUrl);
    const QString host = QUrl(url).host().toLower();
    if (host.isEmpty())
        return Result<SourceMatch>::failure(Error::make(Error::InvalidUrl, QStringLiteral("Not a URL: %1").arg(rawUrl)));

    std::shared_ptr<Source> source;
    if (!overrideId.isEmpty()) {
        source = create(overrideId);
        if (!source)
            return Result<SourceMatch>::failure(
                Error::make(Error::UnsupportedUrl, QStringLiteral("Unknown source: %1").arg(overrideId)));
        QString bareHost = host;
        if (bareHost.startsWith(QLatin1String("www.")))
            bareHost = bareHost.mid(4);
        // Keep a non-default port: self-hosted forges often run on :3000 etc.
        const int port = QUrl(url).port();
        if (port > 0)
            source->setCustomHost(bareHost + QLatin1Char(':') + QString::number(port));
        else if (!source->defaultHosts().contains(bareHost))
            source->setCustomHost(bareHost);
    } else {
        for (size_t i = 0; i < m_templates.size() && !source; ++i) {
            const auto &t = m_templates[i];
            QStringList escaped;
            for (const QString &h : t->defaultHosts())
                escaped << QRegularExpression::escape(h);
            if (escaped.isEmpty())
                continue;
            const QString prefix = t->allowSubDomains() ? QStringLiteral("([^.]+\\.)*") : QStringLiteral("(www\\.)?");
            const QRegularExpression hostPattern(
                QStringLiteral("^%1(%2)$").arg(prefix, escaped.join(QLatin1Char('|'))));
            if (hostPattern.match(host).hasMatch())
                source = m_factories[i]();
        }
        if (!source)
            return Result<SourceMatch>::failure(Error::make(
                Error::UnsupportedUrl,
                QStringLiteral("No source recognises %1. For a self-hosted forge, choose its type manually.").arg(host)));
    }

    const auto standard = source->standardizeUrl(url);
    if (!standard.ok())
        return Result<SourceMatch>::failure(standard.error);
    SourceMatch match;
    match.source = source;
    match.standardUrl = standard.value;
    return Result<SourceMatch>::success(match);
}

} // namespace Harpoon
