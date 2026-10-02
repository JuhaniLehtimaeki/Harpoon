#pragma once

#include <QString>
#include <QStringList>

namespace Harpoon {

// Comparison of human-facing version labels (forge tags, release titles,
// RPM VERSION fields). Ported from ObtainX lib/version/version_comparison.dart
// without its Android/Google-specific schemes.
//
// For comparing two RPM EVRs use rpmversion.h instead.
enum class VersionRelation { Same, Older, Newer, Unknown };

struct VersionDecision
{
    VersionRelation relation = VersionRelation::Unknown;
    QString reason;

    // -1 (first older), 0, 1 (first newer); 0 also when Unknown — check
    // isOrdered() first.
    int comparison() const;
    bool isOrdered() const { return relation != VersionRelation::Unknown; }
};

// Lowercases, trims and strips a leading "v" before a digit ("V1.2" -> "1.2").
QString normalizeVersionLabel(const QString &value);

// Compares two digit strings of any length numerically.
int compareDecimalIdentifiers(const QString &first, const QString &second);

// How `installed` relates to `latest`: Older means an update is available.
VersionDecision compareVersionStrings(const QString &installed, const QString &latest);

// True when every label can be ordered against every other one with
// compareVersionStrings, i.e. version order is a safe sort key for the list.
bool versionsHaveConsistentOrder(const QStringList &labels);

// Natural sort: digit runs compared numerically, text before numbers.
int compareAlphaNumeric(const QString &a, const QString &b);

} // namespace Harpoon
