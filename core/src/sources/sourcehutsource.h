#pragma once

#include "sources/source.h"

#include <memory>

namespace Harpoon {

// SourceHut git refs (ObtainX lib/app_sources/sourcehut.dart). There is no
// API, so this reads the refs feed and scrapes each ref page:
//
//   GET {repo}/refs/rss.xml        first 6 items; version = <title>, date = <pubDate>
//   GET <guid>                     each ref page; <a href> links ending in .rpm
//
// The canonical URL is https://git.sr.ht/~{owner}/{repo}. Items whose guid is
// not under {repo}/refs are skipped, as upstream. With
// fallbackToOlderReleases off only the newest ref page is fetched.
class SourceHutSource : public Source
{
public:
    QString id() const override { return QStringLiteral("SourceHut"); }
    QString displayName() const override { return QStringLiteral("SourceHut"); }
    QStringList defaultHosts() const override { return {QStringLiteral("git.sr.ht")}; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

private:
    void fetchRefPages(std::shared_ptr<QList<Release>> releases, int index, const QString &standardUrl,
                       HttpTransport &transport, Callback done);
};

} // namespace Harpoon
