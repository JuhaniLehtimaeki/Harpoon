#pragma once

#include "sources/source.h"

namespace Harpoon {

// SourceForge file releases via the project RSS feed (ObtainX
// lib/app_sources/sourceforge.dart).
//
//   GET {origin}/projects/{name}/rss?path=/
//
// URL forms: /p/{name}/... becomes /projects/{name}/...; /projects/{name}
// becomes /projects/{name}/files; the canonical URL is
// https://sourceforge.net/projects/{name}/files[/{subfolder}].
//
// Feed items are kept when their <guid> starts with the canonical URL and
// ends in "/download" after a .rpm file name. The version is the folder that
// holds the file, relative to the canonical URL (".../files/v1.2/app.rpm/download"
// -> "v1.2"); a file at the top level uses its own name, as upstream.
//
// Mapping into Harpoon's pipeline: upstream suppresses the global version
// extraction and instead applies versionExtractionRegEx to each folder,
// dropping files whose folder does not match, and groups files by the
// extracted version. Here the same filtering and grouping happen in the
// source, but Release.tag keeps the raw folder so ReleasePipeline applies
// the regex exactly once to produce the version. Every version group becomes
// a release (newest first, as the feed lists them) so fallbackToOlderReleases
// works; the date is the newest file's <pubDate>, and <media:content
// filesize> becomes Asset.size.
class SourceForgeSource : public Source
{
public:
    QString id() const override { return QStringLiteral("SourceForge"); }
    QString displayName() const override { return QStringLiteral("SourceForge"); }
    QStringList defaultHosts() const override { return {QStringLiteral("sourceforge.net")}; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

    // {origin}/projects/{name}/rss?path=/
    static QString feedUrl(const QString &standardUrl);
};

// Groups feed items into releases as described above. versionRegex and
// matchGroup are the app's versionExtractionRegEx and matchGroupToUse.
Result<QList<Release>> parseSourceForgeFeed(const QByteArray &xml, const QString &standardUrl,
                                            const QString &versionRegex, const QString &matchGroup);

} // namespace Harpoon
