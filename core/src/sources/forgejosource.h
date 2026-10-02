#pragma once

#include "sources/githubsource.h"

namespace Harpoon {

// Codeberg and any other Forgejo or Gitea instance. Their release API mirrors
// GitHub's, so this reuses GitHubSource and only swaps the API base, auth
// header and error handling — the same approach as ObtainX's Codeberg/Forgejo.
//
//   {origin}/api/v1/repos/{owner}/{repo}/releases?per_page=100
//
// Self-hosted instances are reached by forcing this source onto their host
// (SourceRegistry override). Config key "token": sent as "Authorization: token".
class ForgejoSource : public GitHubSource
{
public:
    QString id() const override { return QStringLiteral("Forgejo"); }
    QString displayName() const override { return QStringLiteral("Codeberg / Forgejo / Gitea"); }
    QStringList defaultHosts() const override
    {
        return {QStringLiteral("codeberg.org"), QStringLiteral("codefloe.com")};
    }

    QString apiBaseUrl(const QString &standardUrl) const override;

protected:
    QByteArray authorizationHeader(const QString &token) const override;
    Error errorForResponse(const HttpResponse &response) const override;
};

} // namespace Harpoon
