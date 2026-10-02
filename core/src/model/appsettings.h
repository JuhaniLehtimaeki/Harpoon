#pragma once

#include <QString>
#include <QVariantMap>

namespace Harpoon {

// Per-app settings. Key names follow ObtainX where an equivalent exists, so
// the research notes and upstream code stay easy to cross-reference.
namespace Keys {
// Release selection
inline const char *includePrereleases = "includePrereleases";
inline const char *fallbackToOlderReleases = "fallbackToOlderReleases";
inline const char *filterReleaseTitlesByRegEx = "filterReleaseTitlesByRegEx";
inline const char *filterReleaseNotesByRegEx = "filterReleaseNotesByRegEx";
inline const char *sortMethodChoice = "sortMethodChoice";   // date|smartname|smartname-datefallback|name|none
inline const char *useLatestAssetDateAsReleaseDate = "useLatestAssetDateAsReleaseDate";
inline const char *trackOnly = "trackOnly";
// Installation
inline const char *allowIdChange = "allowIdChange";         // accept a different RPM name than the tracked id
// Version string
inline const char *versionSource = "versionSource";         // tag|title|assetName|date
inline const char *versionExtractionRegEx = "versionExtractionRegEx";
inline const char *matchGroupToUse = "matchGroupToUse";
// Assets
inline const char *assetFilterRegEx = "assetFilterRegEx";
inline const char *invertAssetFilter = "invertAssetFilter";
inline const char *autoAssetFilterByArch = "autoAssetFilterByArch";
inline const char *preferSfosVersionTag = "preferSfosVersionTag";
} // namespace Keys

class AppSettings
{
public:
    AppSettings() = default;
    explicit AppSettings(const QVariantMap &values) : m_values(values) {}

    bool getBool(const char *key, bool defaultValue = false) const;
    QString getString(const char *key, const QString &defaultValue = QString()) const;
    void set(const char *key, const QVariant &value) { m_values.insert(QString::fromLatin1(key), value); }

    const QVariantMap &values() const { return m_values; }

private:
    QVariantMap m_values;
};

} // namespace Harpoon
