#include "version/versioncompare.h"

#include <QDateTime>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <optional>

namespace Harpoon {

namespace {

// Compiled-pattern cache. Patterns are string literals, so the pointer is a
// stable key. Not thread-safe: version comparison runs on the main thread.
const QRegularExpression &re(const char *pattern)
{
    static QHash<const char *, QRegularExpression> cache;
    auto it = cache.find(pattern);
    if (it == cache.end())
        it = cache.insert(pattern, QRegularExpression(QString::fromLatin1(pattern),
                                                      QRegularExpression::UseUnicodePropertiesOption));
    return *it;
}

bool fullMatch(const char *pattern, const QString &text)
{
    return re(pattern).match(text).hasMatch();
}

const char *kDecimal = "^\\d+$";
const char *kNumericCore = "^(\\d+(?:\\.\\d+)*)(.*)$";
const char *kReleaseVersion =
    "(?<![a-z0-9.])v?\\d+(?:\\.\\d+)+[a-z0-9.+_-]*"
    "(?:[ \\t]+(?:dev|snapshot|nightly|alpha|beta|preview|pre|rc)"
    "(?:[.-][a-z0-9-]+(?:\\.[a-z0-9-]+)*|\\d+(?:\\.[a-z0-9-]+)*)?\\b)?"
    "(?:\\s*\\([^)]*\\))?";
const char *kHashToken = "(?<![a-z0-9])[a-f0-9]{6,}(?![a-z0-9])";
const char *kPrerelease =
    "^[.-]?(dev|snapshot|nightly|alpha|beta|preview|pre|rc)"
    "(?:\\.([a-z][a-z0-9-]*(?:\\.[a-z0-9-]+)*|\\d+(?:\\.[a-z0-9-]+)*)|-?(\\d+(?:\\.[a-z0-9-]+)*))?$";
const char *kEmbeddedPrerelease =
    "(?<![a-z0-9])(dev|snapshot|nightly|alpha|beta|preview|pre|rc)(?:[.-]?(\\d+(?:\\.\\d+)*))?(?=$|[.\\-_\\s()])";
const char *kDescriptiveSuffix = "^[.\\-_\\s()]*\\p{L}[\\p{L}\\p{N}.\\-_\\s()]*$";
const char *kBuildHash = "^[a-f0-9]{7,40}$";

int prereleaseRank(const std::optional<QString> &qualifier)
{
    if (!qualifier)
        return 8; // final release ranks above every prerelease
    static const QHash<QString, int> ranks = {
        {QStringLiteral("dev"), 0},  {QStringLiteral("snapshot"), 1}, {QStringLiteral("nightly"), 2},
        {QStringLiteral("alpha"), 3}, {QStringLiteral("beta"), 4},    {QStringLiteral("pre"), 5},
        {QStringLiteral("preview"), 6}, {QStringLiteral("rc"), 7},
    };
    return ranks.value(*qualifier, 8);
}

int sign(int v) { return v < 0 ? -1 : (v > 0 ? 1 : 0); }

int compareComponents(const QStringList &first, const QStringList &second, bool pad = true)
{
    const int length = qMax(first.size(), second.size());
    for (int i = 0; i < length; ++i) {
        if (!pad && (i >= first.size() || i >= second.size()))
            return sign(first.size() - second.size());
        const QString a = i < first.size() ? first.at(i) : QStringLiteral("0");
        const QString b = i < second.size() ? second.at(i) : QStringLiteral("0");
        const bool aNum = fullMatch(kDecimal, a);
        const bool bNum = fullMatch(kDecimal, b);
        int c;
        if (aNum && bNum)
            c = compareDecimalIdentifiers(a, b);
        else if (aNum != bNum)
            c = aNum ? -1 : 1;
        else
            c = sign(a.compare(b));
        if (c != 0)
            return c;
    }
    return 0;
}

struct ParsedRelease
{
    QStringList core;
    std::optional<QString> qualifier;
    QStringList qualifierParts;
    std::optional<QStringList> revision;
    QString variant;
    std::optional<QString> hash;
};

QStringList splitDots(const QString &s) { return s.split(QLatin1Char('.')); }

bool isHashCandidate(const QString &token)
{
    return token.contains(re("[a-f]")) && (token.contains(re("\\d")) || token.size() >= 7);
}

std::optional<ParsedRelease> parseRelease(const QString &input)
{
    QString text = normalizeVersionLabel(input);

    // A title's digits are not version segments. Extract only one unambiguous
    // dotted version, preserving its suffix.
    if (!re("^\\d+(?:[.\\-+ (]|$)").match(text).hasMatch()) {
        auto it = re(kReleaseVersion).globalMatch(text);
        QStringList candidates;
        while (it.hasNext())
            candidates << it.next().captured(0);
        if (candidates.size() != 1)
            return std::nullopt;
        text = normalizeVersionLabel(candidates.first());
    }

    const int metadata = text.indexOf(QLatin1Char('+'));
    if (metadata >= 0) {
        if (!re("^[a-z0-9-]+(?:\\.[a-z0-9-]+)*$").match(text.mid(metadata + 1)).hasMatch())
            return std::nullopt;
        text = text.left(metadata);
    }

    std::optional<QString> hash;
    {
        QList<QRegularExpressionMatch> hashes;
        auto it = re(kHashToken).globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            if (isHashCandidate(m.captured(0)))
                hashes << m;
        }
        if (hashes.size() > 1)
            return std::nullopt;
        if (!hashes.isEmpty()) {
            hash = hashes.first().captured(0);
            text.remove(hashes.first().capturedStart(0), hashes.first().capturedLength(0));
            text.remove(re("\\s*\\((?:git\\s*)?\\)\\s*"));
            text.replace(re("[.-]$"), QString());
        }
    }

    std::optional<QStringList> revision;
    {
        QList<QRegularExpressionMatch> builds;
        auto it = re("\\s*\\((\\d+)\\)").globalMatch(text);
        while (it.hasNext())
            builds << it.next();
        if (builds.size() > 1)
            return std::nullopt;
        if (!builds.isEmpty()) {
            revision = QStringList{builds.first().captured(1)};
            text.remove(builds.first().capturedStart(0), builds.first().capturedLength(0));
        }
    }

    const auto coreMatch = re(kNumericCore).match(text);
    if (!coreMatch.hasMatch())
        return std::nullopt;
    ParsedRelease out;
    out.core = splitDots(coreMatch.captured(1));
    out.hash = hash;
    QString suffix = coreMatch.captured(2).trimmed();

    // Only a leading numeric suffix denotes a revision.
    const auto build = re("^-(\\d+(?:\\.\\d+)*)(?=$|[.\\-_\\s()])").match(suffix);
    if (build.hasMatch()) {
        if (revision)
            return std::nullopt;
        revision = splitDots(build.captured(1));
        suffix = suffix.mid(build.capturedEnd(0)).trimmed();
    }
    out.revision = revision;
    if (suffix.isEmpty())
        return out;

    const auto qualifier = re(kPrerelease).match(suffix);
    if (qualifier.hasMatch()) {
        out.qualifier = qualifier.captured(1);
        const QString parts = !qualifier.captured(2).isEmpty() ? qualifier.captured(2) : qualifier.captured(3);
        if (!parts.isEmpty())
            out.qualifierParts = splitDots(parts);
        return out;
    }

    if (!re(kDescriptiveSuffix).match(suffix).hasMatch()
        || re("(?:^|\\s)v?\\d+(?:\\.\\d+)+").match(suffix).hasMatch())
        return std::nullopt;

    // Recognized prerelease markers remain meaningful even with descriptive
    // text around them.
    QList<QRegularExpressionMatch> markers;
    {
        auto it = re(kEmbeddedPrerelease).globalMatch(suffix);
        while (it.hasNext())
            markers << it.next();
    }
    if (!markers.isEmpty()) {
        const int start = markers.first().capturedStart(0);
        const auto complete = re(kPrerelease).match(suffix.mid(start));
        if (complete.hasMatch()) {
            out.qualifier = complete.captured(1);
            const QString parts = !complete.captured(2).isEmpty() ? complete.captured(2) : complete.captured(3);
            if (!parts.isEmpty())
                out.qualifierParts = splitDots(parts);
            out.variant = suffix.left(start);
            return out;
        }
    }
    if (markers.size() > 1)
        return std::nullopt;
    if (markers.isEmpty()) {
        out.variant = suffix;
    } else {
        const auto &m = markers.first();
        out.qualifier = m.captured(1);
        if (!m.captured(2).isEmpty())
            out.qualifierParts = splitDots(m.captured(2));
        out.variant = suffix;
        out.variant.remove(m.capturedStart(0), m.capturedLength(0));
    }
    return out;
}

std::optional<QDateTime> releaseDate(const QString &value)
{
    const QString text = normalizeVersionLabel(value);
    if (re("^\\d{15,17}$").match(text).hasMatch()) {
        bool ok = false;
        const qint64 micros = text.toLongLong(&ok);
        if (!ok)
            return std::nullopt;
        return QDateTime::fromMSecsSinceEpoch(micros / 1000, Qt::UTC);
    }
    if (!re("^\\d{4}-\\d{2}-\\d{2}(?:[t ].*)?$").match(text).hasMatch())
        return std::nullopt;
    QString iso = text.toUpper();
    iso.replace(QLatin1Char(' '), QLatin1Char('T'));
    QDateTime dt = QDateTime::fromString(iso, Qt::ISODate);
    if (!dt.isValid() && iso.size() == 10)
        dt = QDateTime(QDate::fromString(iso, Qt::ISODate), QTime(0, 0), Qt::UTC);
    if (!dt.isValid())
        return std::nullopt;
    return dt.toUTC();
}

QString canonicalComponent(const QString &value)
{
    if (!fullMatch(kDecimal, value))
        return value;
    QString digits = value;
    digits.remove(re("^0+"));
    return digits.isEmpty() ? QStringLiteral("0") : digits;
}

VersionDecision decision(VersionRelation relation, const char *reason)
{
    return VersionDecision{relation, QString::fromLatin1(reason)};
}

VersionDecision ordered(int comparison, const char *reason)
{
    return decision(comparison == 0 ? VersionRelation::Same
                                    : (comparison < 0 ? VersionRelation::Older : VersionRelation::Newer),
                    reason);
}

} // namespace

int VersionDecision::comparison() const
{
    switch (relation) {
    case VersionRelation::Older: return -1;
    case VersionRelation::Newer: return 1;
    default: return 0;
    }
}

QString normalizeVersionLabel(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (re("^v\\d").match(normalized).hasMatch())
        return normalized.mid(1);
    return normalized;
}

int compareDecimalIdentifiers(const QString &first, const QString &second)
{
    QString a = first;
    QString b = second;
    a.remove(re("^0+"));
    b.remove(re("^0+"));
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    return sign(a.compare(b));
}

VersionDecision compareVersionStrings(const QString &installed, const QString &latest)
{
    const QString first = normalizeVersionLabel(installed);
    const QString second = normalizeVersionLabel(latest);
    if (first.isEmpty() || second.isEmpty())
        return decision(VersionRelation::Unknown, "missingVersion");
    if (first == second)
        return decision(VersionRelation::Same, "sameLabel");

    const auto firstDate = releaseDate(first);
    const auto secondDate = releaseDate(second);
    if (firstDate && secondDate) {
        const qint64 a = firstDate->toMSecsSinceEpoch();
        const qint64 b = secondDate->toMSecsSinceEpoch();
        return ordered(a < b ? -1 : (a > b ? 1 : 0), "releaseDateVersion");
    }
    if (firstDate || secondDate)
        return decision(VersionRelation::Unknown, "differentSchemes");

    if (fullMatch(kBuildHash, first) && fullMatch(kBuildHash, second)
        && first.contains(re("[a-f]")) && second.contains(re("[a-f]")))
        return decision(VersionRelation::Unknown, "differentBuildHashes");

    const auto a = parseRelease(first);
    const auto b = parseRelease(second);
    if (!a || !b)
        return decision(VersionRelation::Unknown, "unrecognizedFormat");
    // A lone build ID and a dotted display version occupy different schemes.
    if ((a->core.size() == 1) != (b->core.size() == 1))
        return decision(VersionRelation::Unknown, "differentSchemes");

    const int core = compareComponents(a->core, b->core);
    if (core != 0)
        return ordered(core, "numericRelease");
    if (a->qualifier != b->qualifier)
        return ordered(sign(prereleaseRank(a->qualifier) - prereleaseRank(b->qualifier)), "prerelease");
    const int qualifierParts = compareComponents(a->qualifierParts, b->qualifierParts, false);
    if (qualifierParts != 0)
        return ordered(qualifierParts, "prerelease");
    if (a->hash && b->hash && *a->hash != *b->hash)
        return decision(VersionRelation::Unknown, "differentBuildHashes");
    if (a->revision.has_value() != b->revision.has_value())
        return decision(VersionRelation::Unknown, "missingBuildRevision");
    if (a->revision)
        return ordered(compareComponents(*a->revision, *b->revision), "buildRevision");
    return decision(VersionRelation::Same, a->hash && b->hash ? "sameBuildHash" : "sameRelease");
}

bool versionsHaveConsistentOrder(const QStringList &labels)
{
    QSet<QString> normalized;
    for (const QString &label : labels)
        normalized.insert(normalizeVersionLabel(label));
    if (normalized.size() < 2)
        return true;
    if (normalized.contains(QString()))
        return false;

    std::optional<bool> dateScheme;
    std::optional<bool> buildIdScheme;
    // group -> (hash, hasRevision)
    QHash<QString, QPair<QString, bool>> builds;
    for (const QString &label : normalized) {
        const bool isDate = releaseDate(label).has_value();
        if (!dateScheme)
            dateScheme = isDate;
        if (*dateScheme != isDate)
            return false;
        if (isDate)
            continue;
        const auto release = parseRelease(label);
        if (!release)
            return false;
        const bool buildId = release->core.size() == 1;
        if (!buildIdScheme)
            buildIdScheme = buildId;
        if (*buildIdScheme != buildId)
            return false;

        // Hash and revision ambiguity only matters within the same numeric
        // release and prerelease.
        QStringList core;
        for (const QString &c : release->core)
            core << canonicalComponent(c);
        while (core.size() > 1 && core.last() == QLatin1String("0"))
            core.removeLast();
        QStringList qualifierParts;
        for (const QString &c : release->qualifierParts)
            qualifierParts << canonicalComponent(c);
        const QString group = core.join(QLatin1Char('.')) + QLatin1Char('|')
                              + release->qualifier.value_or(QStringLiteral("null")) + QLatin1Char('|')
                              + qualifierParts.join(QLatin1Char('.'));
        const QString hash = release->hash.value_or(QString());
        const bool hasRevision = release->revision.has_value();
        const auto previous = builds.constFind(group);
        if (previous != builds.constEnd()) {
            if (previous->second != hasRevision
                || (!previous->first.isEmpty() && !hash.isEmpty() && previous->first != hash))
                return false;
            builds.insert(group, qMakePair(previous->first.isEmpty() ? hash : previous->first, hasRevision));
        } else {
            builds.insert(group, qMakePair(hash, hasRevision));
        }
    }
    return true;
}

int compareAlphaNumeric(const QString &a, const QString &b)
{
    auto split = [](const QString &s) {
        QStringList parts;
        if (s.isEmpty())
            return parts;
        QString current(s.at(0));
        bool numeric = s.at(0).isDigit();
        for (int i = 1; i < s.size(); ++i) {
            const bool isNum = s.at(i).isDigit();
            if (isNum == numeric) {
                current += s.at(i);
            } else {
                parts << current;
                current = QString(s.at(i));
                numeric = isNum;
            }
        }
        parts << current;
        return parts;
    };
    const QStringList aParts = split(a);
    const QStringList bParts = split(b);
    for (int i = 0; i < aParts.size() && i < bParts.size(); ++i) {
        const bool aNum = aParts.at(i).at(0).isDigit();
        const bool bNum = bParts.at(i).at(0).isDigit();
        if (aNum && bNum) {
            const int c = compareDecimalIdentifiers(aParts.at(i), bParts.at(i));
            if (c != 0)
                return c;
        } else if (!aNum && !bNum) {
            const int c = sign(aParts.at(i).compare(bParts.at(i)));
            if (c != 0)
                return c;
        } else {
            return aNum ? 1 : -1; // text before numbers
        }
    }
    return sign(aParts.size() - bParts.size());
}

} // namespace Harpoon
