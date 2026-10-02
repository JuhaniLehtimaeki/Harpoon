#pragma once

#include <QString>

namespace Harpoon {

// RPM epoch:version-release.
struct Evr
{
    int epoch = 0;
    QString version;
    QString release;

    // "[epoch:]version[-release]"; epoch 0 is omitted.
    QString toString() const;
};

// Parses "[epoch:]version[-release]". The release is whatever follows the last
// '-'. A missing or non-numeric epoch is 0.
Evr parseEvr(const QString &evr);

// rpm's rpmvercmp(): segment-wise comparison of alphanumeric runs where
// numbers beat letters, '~' sorts before anything (pre-releases) and '^'
// sorts after the base but before the next segment (post-release snapshots).
// Returns -1, 0 or 1.
int rpmVerCmp(const QString &a, const QString &b);

// Full EVR comparison as rpm does it. A release that is empty on either side
// is not compared (matches rpm's behaviour for unversioned releases).
int compareEvr(const Evr &a, const Evr &b);

} // namespace Harpoon
