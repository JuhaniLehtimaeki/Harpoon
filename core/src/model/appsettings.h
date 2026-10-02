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
inline const char *githubBuildVerificationMode = "githubBuildVerificationMode"; // off|audit|enforce
// Version string
inline const char *versionSource = "versionSource";         // tag|title|assetName|date
inline const char *versionExtractionRegEx = "versionExtractionRegEx";
inline const char *matchGroupToUse = "matchGroupToUse";
// Assets
inline const char *assetFilterRegEx = "assetFilterRegEx";
inline const char *invertAssetFilter = "invertAssetFilter";
inline const char *autoAssetFilterByArch = "autoAssetFilterByArch";
inline const char *preferSfosVersionTag = "preferSfosVersionTag";
// HTML / direct link (ObtainX html.dart). The link options also apply to each
// intermediateLink hop, whose value is a list of maps with these keys.
inline const char *intermediateLink = "intermediateLink";             // [{customLinkFilterRegex, ...}], max 10
inline const char *customLinkFilterRegex = "customLinkFilterRegex";   // default: installable .rpm links
inline const char *filterByLinkText = "filterByLinkText";             // match the link text, not the URL
inline const char *matchLinksOutsideATags = "matchLinksOutsideATags"; // also find bare URLs in text/JSON
inline const char *skipSort = "skipSort";                             // keep page order
inline const char *reverseSort = "reverseSort";                       // take the first link instead of the last
inline const char *sortByLastLinkSegment = "sortByLastLinkSegment";   // sort by file name, not full URL
inline const char *versionExtractWholePage = "versionExtractWholePage";
inline const char *requestHeader = "requestHeader";                   // "Name: value" lines
inline const char *defaultPseudoVersioningMethod = "defaultPseudoVersioningMethod"; // ETag|linkHash
// rpm-md repository
inline const char *packageName = "packageName";                       // RPM %{NAME} to track
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
