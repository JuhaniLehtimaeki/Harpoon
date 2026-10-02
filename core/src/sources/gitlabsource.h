#pragma once

#include "sources/source.h"

namespace Harpoon {

// GitLab releases via the REST API v4 (ObtainX lib/app_sources/gitlab.dart).
//
//   GET {origin}/api/v4/projects/{group%2Fsub%2Fproject}           numeric project id
//   GET {origin}/api/v4/projects/{path}/releases?per_page=100      (first page only)
//   GET {origin}/api/v4/projects/{path}/repository/tags?per_page=100   when trackOnly
//
// The canonical URL is https://gitlab.com/{group}/{subgroups...}/{project},
// cut at the "/-/" separator. Self-hosted instances work via override.
//
// Config key "token": personal access token, sent as the private_token query
// parameter on API requests (as upstream). Every request carries a Referer
// for Cloudflare-fronted instances.
//
// Assets are assets.links[] (direct_asset_url ?? url) plus "](/uploads/...rpm)"
// markdown links in the description, which resolve to
// {origin}/-/project/{id}/uploads/.... CI artifact links
// {project}/-/jobs/N/artifacts/file/X are rewritten to .../artifacts/raw/X.
//
// Differences from upstream:
//  - Asset URLs are stored without the token, because assets end up in the
//    app store and exports. Private assets need authorizedAssetUrl() at
//    download time.
//  - upcoming_release (a release whose released_at is in the future) maps to
//    Release.prerelease, so it is skipped unless includePrereleases.
//  - Every release gets its CI-artifact rewrite, not only the selected one.
//  - When a link's name is not an RPM file name but its URL is, the URL's
//    file name is used so AssetFilter sees the package.
class GitLabSource : public Source
{
public:
    QString id() const override { return QStringLiteral("GitLab"); }
    QString displayName() const override { return QStringLiteral("GitLab"); }
    QStringList defaultHosts() const override { return {QStringLiteral("gitlab.com")}; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

    // {origin}/api/v4/projects/{url-encoded project path}
    static QString apiBaseUrl(const QString &standardUrl);
    // assetUrl with ?private_token= (or &private_token=) when a token is configured.
    QString authorizedAssetUrl(const QString &assetUrl) const;
    void prepareDownload(const Asset &asset, const AppSettings &settings, DownloadRequest &request) const override;

private:
    HttpRequest request(const QString &url, const QString &standardUrl) const;
    QString withToken(const QString &url) const;
};

// Parses a GitLab /releases (or, with fromTags, /repository/tags) response.
// projectId is used for description uploads; standardUrl for the CI-artifact
// rewrite and the release page fallback.
Result<QList<Release>> parseGitLabReleases(const QByteArray &json, const QString &standardUrl, qint64 projectId,
                                           bool fromTags);

} // namespace Harpoon
