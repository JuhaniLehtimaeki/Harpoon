#pragma once

#include "sources/htmlsource.h"

namespace Harpoon {

// A fixed URL that serves an RPM (ObtainX DirectAPKLink). Hostless:
// auto-selected only for URLs whose path ends in ".rpm"; any URL works when
// chosen via override.
//
// Nothing is scraped: the release has one asset, the URL itself. The
// version is a pseudo-version exactly as for HtmlSource (ETag, falling back
// to the link hash; defaultPseudoVersioningMethod "linkHash" skips the
// request), or, when versionExtractionRegEx is set, the decoded URL for
// ReleasePipeline to extract from. requestHeader applies to the ETag probe.
//
// Upstream matches ".+\.apk$" against the whole URL; here only the path must
// end in .rpm, so download links with a query string are recognised too.
class DirectLinkSource : public HtmlSource
{
public:
    QString id() const override { return QStringLiteral("DirectLink"); }
    QString displayName() const override { return QStringLiteral("Direct RPM link"); }
    bool matchesUrlShape(const QString &url) const override;

    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;
};

} // namespace Harpoon
