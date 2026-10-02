#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace Harpoon {

// One downloadable file attached to a release.
struct Asset
{
    QString name;
    QString url;         // public download URL
    QString apiUrl;      // API download URL (GitHub: needs Accept: application/octet-stream)
    qint64 size = -1;
    QString sha256;      // hex digest when the forge publishes one
    QDateTime updatedAt; // upload/update time when known
};

// One release as published by a forge, before any Harpoon-side selection.
struct Release
{
    QString tag;         // tag name, e.g. "v1.2.3"
    QString title;       // release name/title (may be empty)
    QString changelog;   // release notes (usually Markdown)
    QDateTime date;      // publish date when known
    QString pageUrl;     // human-readable release page
    bool prerelease = false;
    bool draft = false;
    QList<Asset> assets;

    // Tag, or the title when the tag is empty. Used for sorting and as the
    // default version string.
    QString label() const { return tag.isEmpty() ? title : tag; }
};

} // namespace Harpoon
