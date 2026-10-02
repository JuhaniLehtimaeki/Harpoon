#pragma once

#include "model/error.h"
#include "pkg/processrunner.h"
#include "version/rpmversion.h"

#include <QList>

namespace Harpoon {

struct RpmInfo
{
    QString name;
    Evr evr;
    QString arch;
    QString vendor;  // empty when the package has none
    QString summary;

    QString nevra() const { return name + QLatin1Char('-') + evr.toString() + QLatin1Char('.') + arch; }
};

// Reads RPM headers with the rpm command line tool. Works unprivileged and
// inside the sandbox. PackageKit's GetDetailsLocal is not supported by the
// SailfishOS zypp backend, which is why Storeman does the same.
class RpmInspector
{
public:
    explicit RpmInspector(ProcessRunner &runner) : m_runner(runner) {}

    // rpm -qp on a package file.
    Result<RpmInfo> inspectFile(const QString &path) const;

    // rpm -q on the installed database. Returns an empty list when the package
    // is not installed; several entries when multiple versions are installed.
    Result<QList<RpmInfo>> queryInstalled(const QString &name) const;

    // Highest installed EVR of a package, or a null name when not installed.
    Result<RpmInfo> installedPackage(const QString &name) const;

    // Parses lines produced with queryFormat().
    static QList<RpmInfo> parse(const QByteArray &output);
    static QString queryFormat();

private:
    ProcessRunner &m_runner;
};

} // namespace Harpoon
