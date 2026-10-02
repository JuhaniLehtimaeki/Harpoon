#include "sources/directlinksource.h"

#include "sources/sourceutil.h"

namespace Harpoon {

bool DirectLinkSource::matchesUrlShape(const QString &url) const
{
    return standardizeUrl(url).ok() && urlPathEndsWithRpm(preStandardizeUrl(url));
}

void DirectLinkSource::fetchReleases(const QString &standardUrl, const AppSettings &settings,
                                     HttpTransport &transport, Callback done)
{
    finish(standardUrl, QString(), {PageLink{standardUrl, lastPathSegment(standardUrl)}}, settings, transport, done);
}

} // namespace Harpoon
