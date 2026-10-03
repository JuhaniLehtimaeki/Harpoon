#pragma once

#include "sources/source.h"

namespace Harpoon {

// GitHub releases via the REST API (ObtainX lib/app_sources/github.dart).
//
//   GET {api}/releases?per_page=100   (first page only, like upstream)
//   GET {api}/tags?per_page=100       fallback when there are no releases and
//                                     the app is track-only
//
// {api} is https://api.github.com/repos/{owner}/{repo} for github.com and
// https://{host}/api/v3/repos/{owner}/{repo} for GitHub Enterprise hosts.
// Config key "token": personal access token, sent as a Bearer token. A 401
// with a token is retried once without it.
class GitHubSource : public Source
{
public:
    QString id() const override { return QStringLiteral("GitHub"); }
    QString displayName() const override { return QStringLiteral("GitHub"); }
    bool usesToken() const override { return true; }
    QStringList defaultHosts() const override { return {QStringLiteral("github.com")}; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

    virtual QString apiBaseUrl(const QString &standardUrl) const;

    // With a token, downloads through the asset API (works for private
    // repositories); the downloader drops the token on the storage redirect.
    // The token is only attached to the repository's own host and its API.
    void prepareDownload(const Asset &asset, const AppSettings &settings, const QString &standardUrl,
                         DownloadRequest &request) const override;

protected:
    virtual QByteArray authorizationHeader(const QString &token) const;
    virtual Error errorForResponse(const HttpResponse &response) const;
    // The releases request answered 404. On GitHub that means the
    // repository does not exist (or is private), which `error` says; other
    // forges can tell more, such as a repository with releases turned off.
    virtual void explainReleasesNotFound(const QString &api, HttpTransport &transport, const Error &error,
                                         std::function<void(const Error &)> done);

    void getJson(const QString &url, HttpTransport &transport, bool withToken,
                 std::function<void(const Result<QByteArray> &)> done);
};

// Parses a GitHub-compatible /releases response (GitHub, Forgejo, Gitea).
Result<QList<Release>> parseGitHubStyleReleases(const QByteArray &json);
// Parses a GitHub-compatible /tags response into tag-only releases.
Result<QList<Release>> parseGitHubStyleTags(const QByteArray &json);

} // namespace Harpoon
