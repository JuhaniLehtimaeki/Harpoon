#pragma once

#include "sources/source.h"

#include <functional>
#include <memory>
#include <vector>

namespace Harpoon {

struct SourceMatch
{
    std::shared_ptr<Source> source;
    QString standardUrl;
};

// Ordered list of known sources. Matching walks the list and picks the first
// source whose hosts match the URL's host (ObtainX SourceProvider). An
// override id forces a specific source onto any host, which is how self-hosted
// Forgejo/Gitea/GitHub Enterprise instances are supported.
class SourceRegistry
{
public:
    using Factory = std::function<std::shared_ptr<Source>()>;

    SourceRegistry(); // registers the built-in sources

    void registerSource(Factory factory);

    QStringList ids() const;
    std::shared_ptr<Source> create(const QString &id) const;

    Result<SourceMatch> match(const QString &url, const QString &overrideId = QString()) const;

private:
    std::vector<Factory> m_factories;
    std::vector<std::shared_ptr<Source>> m_templates; // for reading ids/hosts only
};

} // namespace Harpoon
