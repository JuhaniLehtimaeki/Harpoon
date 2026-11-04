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
// Package registry: some projects (Whisperfish) publish their RPMs in the
// project's generic package registry and leave the releases with a source
// archive. When no release has an RPM, and the app is not track-only, the
// registry is read too:
//
//   GET {api}/packages?package_type=generic&per_page=100&order_by=created_at&sort=desc
//   GET {api}/packages/{id}/package_files?per_page=100        (per version)
//   GET {api}/repository/tags?per_page=100                    notes and dates
//
// Each package version becomes a release (tagged like the repository's tag
// when there is one); "dev", "snapshot" and "nightly" versions are
// prereleases. Several packages of one name whose versions differ by a word
// ("0.6.0-beta.46" and "0.6.0-beta.46-harbour": the Harbour variant) are
// builds of the same release; the app's page asks which to install.
// Downloads are {api}/packages/generic/{name}/{version}/{file}, with the
// size and SHA-256 the registry reports.
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
    bool usesToken() const override { return true; }
    QStringList defaultHosts() const override { return {QStringLiteral("gitlab.com")}; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

    // {origin}/api/v4/projects/{url-encoded project path}
    static QString apiBaseUrl(const QString &standardUrl);
    // assetUrl with ?private_token= (or &private_token=) when a token is configured.
    QString authorizedAssetUrl(const QString &assetUrl) const;
    // Adds the token only to assets on the project's own host.
    void prepareDownload(const Asset &asset, const AppSettings &settings, const QString &standardUrl,
                         DownloadRequest &request) const override;

private:
    // Adds the package registry's builds to `releases` when none of them has
    // an RPM (see above), then calls done.
    void addPackageRegistry(const QString &api, const QString &standardUrl, const AppSettings &settings,
                            HttpTransport &transport, FetchResult releases, Callback done);
    HttpRequest request(const QString &url, const QString &standardUrl) const;
    QString withToken(const QString &url) const;
};

// One package of the project's generic package registry.
struct GitLabPackage
{
    qint64 id = 0;
    QString name;
    QString version;
    QDateTime created;
    QString webPath; // "/group/project/-/packages/1"
};

// The builds of one version: the package of that version and its variants.
struct GitLabPackageGroup
{
    QString version; // without the variant word
    QList<GitLabPackage> packages;
    QDateTime created; // newest package
    bool prerelease = false;
};

Result<QList<GitLabPackage>> parseGitLabPackages(const QByteArray &json);
// Groups packages that differ only by a variant word ("-harbour"), newest
// first. A word like "beta" or "dev" is a pre-release marker, not a variant.
QList<GitLabPackageGroup> groupGitLabPackages(const QList<GitLabPackage> &packages);
// The files of one package as assets, downloaded from the registry
// (apiBase = GitLabSource::apiBaseUrl()).
Result<QList<Asset>> parseGitLabPackageFiles(const QByteArray &json, const QString &apiBase,
                                             const GitLabPackage &package);

// Parses a GitLab /releases (or, with fromTags, /repository/tags) response.
// projectId is used for description uploads; standardUrl for the CI-artifact
// rewrite and the release page fallback.
Result<QList<Release>> parseGitLabReleases(const QByteArray &json, const QString &standardUrl, qint64 projectId,
                                           bool fromTags);

} // namespace Harpoon
