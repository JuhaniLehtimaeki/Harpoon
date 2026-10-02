#include "pipeline/deviceinfo.h"

#include <QFile>
#include <QProcess>
#include <QStringList>
#include <QSysInfo>

namespace Harpoon {

QString DeviceInfo::normalizeArch(const QString &arch)
{
    const QString a = arch.trimmed().toLower();
    if (a == QLatin1String("arm64") || a == QLatin1String("aarch64"))
        return QStringLiteral("aarch64");
    if (a == QLatin1String("arm") || a.startsWith(QLatin1String("armv7")))
        return QStringLiteral("armv7hl");
    if (a == QLatin1String("i386") || a == QLatin1String("i486") || a == QLatin1String("i586")
        || a == QLatin1String("i686") || a == QLatin1String("x86"))
        return QStringLiteral("i486");
    if (a == QLatin1String("amd64") || a == QLatin1String("x86_64"))
        return QStringLiteral("x86_64");
    return a;
}

QString DeviceInfo::parseVersionId(const QString &releaseFile)
{
    for (const QString &line : releaseFile.split(QLatin1Char('\n'))) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1String("VERSION_ID=")))
            continue;
        QString value = trimmed.mid(11).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\''))))
            value = value.mid(1, value.size() - 2);
        return value;
    }
    return QString();
}

DeviceInfo DeviceInfo::detect()
{
    DeviceInfo info;

    QProcess rpm;
    rpm.start(QStringLiteral("rpm"), QStringList{QStringLiteral("--eval"), QStringLiteral("%{_arch}")});
    if (rpm.waitForFinished(3000) && rpm.exitStatus() == QProcess::NormalExit && rpm.exitCode() == 0) {
        const QString arch = QString::fromUtf8(rpm.readAllStandardOutput()).trimmed();
        if (!arch.isEmpty() && !arch.startsWith(QLatin1Char('%')))
            info.arch = normalizeArch(arch);
    }
    if (info.arch.isEmpty())
        info.arch = normalizeArch(QSysInfo::currentCpuArchitecture());

    QFile release(QStringLiteral("/etc/sailfish-release"));
    if (release.open(QIODevice::ReadOnly))
        info.osVersion = parseVersionId(QString::fromUtf8(release.readAll()));

    return info;
}

} // namespace Harpoon
