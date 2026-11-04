#pragma once

#include "sources/source.h"

#include <memory>

namespace Harpoon {

// Tangled (tangled.org), a code forge on the AT Protocol. Releases are git
// tags with uploaded artifacts (org.tangled.repo.artifact records). The
// public bobbin API is not exposed outside the web app, so this reads the
// web app's Atom feed and the tag pages it links to, like SourceHut:
//
//   GET {repo}/feed.atom?include=tags   tag entries: name, date, message
//   GET <link>                          each tag page; the artifacts with
//                                      their file names and download links
//
// The feed entry's content is "Tag {name} created by {author}:\\n\\n{message}";
// the message part becomes the changelog. Artifacts are listed per tag on the
// tag page under stable data-testid markers: the row title is the file name,
// the Download link ({repo}/artifacts/{tid}) does not end in ".rpm" but serves
// the file with a Content-Disposition carrying that name.
//
// The canonical URL is https://tangled.org/{handle}/{repo}; anything after
// the repo (e.g. "/tags") is cut. Only annotated tags reach the feed, and at
// most kMaxTags tag pages are read, newest first; without
// fallbackToOlderReleases only the newest one is fetched.
class TangledSource : public Source
{
public:
    QString id() const override { return QStringLiteral("Tangled"); }
    QString displayName() const override { return QStringLiteral("Tangled"); }
    QStringList defaultHosts() const override { return {QStringLiteral("tangled.org")}; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

private:
    void fetchTagPages(std::shared_ptr<QList<Release>> releases, int index, const QString &standardUrl,
                       HttpTransport &transport, Callback done);
};

} // namespace Harpoon
