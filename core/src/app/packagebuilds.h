#pragma once

#include "model/release.h"
#include "version/rpmversion.h"

#include <QList>
#include <QString>

namespace Harpoon {

// What an RPM file name says about its package: "name-version-release.arch.rpm",
// also with a suffix after the arch ("…aarch64-openrepos.rpm"). Only a hint:
// the package itself is what counts.
struct RpmFileName
{
    QString name;
    Evr evr;
    QString arch;

    bool isValid() const { return !name.isEmpty(); }
};

RpmFileName parseRpmFileName(const QString &fileName);

// One of several builds of the same package in a release, such as an
// "openrepos" and a "store" build, and the package filter that keeps it.
struct BuildChoice
{
    QString assetName;
    QString key;         // the asset's URL: unique even when file names are the same
    QString label;       // what tells it apart ("openrepos"); empty for the plain build
    QString filter;      // for Keys::assetFilterRegEx
    bool invert = false; // for Keys::invertAssetFilter
};

// When `assets` (the release's packages for this device) hold several builds
// of one package, the builds to choose from, else nothing. The filters work
// for later releases too: they use the words that tell the builds apart, not
// version numbers, and keep the release's other packages where they can.
// Builds with the same file name (GitLab's package registry keeps one folder
// per version and variant) are told apart by the folder of the file.
// packageName: set to the package that has several builds.
QList<BuildChoice> buildChoices(const QList<Asset> &assets, QString *packageName = nullptr);

} // namespace Harpoon
