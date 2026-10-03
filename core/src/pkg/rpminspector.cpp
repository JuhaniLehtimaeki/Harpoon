#include "pkg/rpminspector.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace Harpoon {

namespace {

QString field(const QString &value)
{
    return value == QLatin1String("(none)") ? QString() : value;
}

} // namespace

QString RpmInspector::queryFormat()
{
    // Tab-separated; SUMMARY is last so a tab inside it cannot shift fields.
    const QString sep = QStringLiteral("\\t");
    return QStringLiteral("@@%{NAME}") + sep + QStringLiteral("%{EPOCH}") + sep + QStringLiteral("%{VERSION}") + sep
           + QStringLiteral("%{RELEASE}") + sep + QStringLiteral("%{ARCH}") + sep + QStringLiteral("%{VENDOR}") + sep
           + QStringLiteral("%{SUMMARY}\\n");
}

QList<RpmInfo> RpmInspector::parse(const QByteArray &output)
{
    QList<RpmInfo> out;
    for (const QString &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
        // Only lines we formatted; skips "package x is not installed" etc.
        if (!line.startsWith(QLatin1String("@@")))
            continue;
        QStringList f = line.mid(2).split(QLatin1Char('\t'));
        if (f.size() < 7)
            continue;
        while (f.size() > 7)
            f[6] += QLatin1Char('\t') + f.takeAt(7);
        RpmInfo info;
        info.name = f.at(0);
        info.evr.epoch = field(f.at(1)).toInt();
        info.evr.version = f.at(2);
        info.evr.release = field(f.at(3));
        info.arch = field(f.at(4));
        info.vendor = field(f.at(5));
        info.summary = field(f.at(6));
        if (!info.name.isEmpty())
            out << info;
    }
    return out;
}

bool isValidRpmName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_+][A-Za-z0-9._+-]{0,127}$"));
    return pattern.match(name).hasMatch();
}

Result<RpmInfo> RpmInspector::inspectFile(const QString &path) const
{
    if (!QFileInfo(path).isFile())
        return Result<RpmInfo>::failure(Error::make(Error::Package, QStringLiteral("File not found: %1").arg(path)));
    const ProcessResult r = m_runner.run(QStringLiteral("rpm"),
                                         {QStringLiteral("-qp"), QStringLiteral("--nosignature"),
                                          QStringLiteral("--qf"), queryFormat(), QStringLiteral("--"), path});
    if (!r.started)
        return Result<RpmInfo>::failure(Error::make(Error::Package, QStringLiteral("Could not run rpm")));
    const QList<RpmInfo> infos = parse(r.standardOutput);
    if (r.exitCode != 0 || infos.size() != 1)
        return Result<RpmInfo>::failure(Error::make(
            Error::Package, QStringLiteral("Not a valid RPM package: %1 %2")
                                .arg(QFileInfo(path).fileName(), QString::fromUtf8(r.standardError).trimmed())));
    return Result<RpmInfo>::success(infos.first());
}

Result<QList<RpmInfo>> RpmInspector::queryInstalled(const QString &name) const
{
    // Not a package name: nothing can be installed under it, and it must not
    // be passed to rpm (where "--eval=%(...)" would run a command).
    if (!isValidRpmName(name))
        return Result<QList<RpmInfo>>::success(QList<RpmInfo>());
    const ProcessResult r = m_runner.run(QStringLiteral("rpm"), {QStringLiteral("-q"), QStringLiteral("--qf"),
                                                                 queryFormat(), QStringLiteral("--"), name});
    if (!r.started)
        return Result<QList<RpmInfo>>::failure(Error::make(Error::Package, QStringLiteral("Could not run rpm")));
    // Exit code 1 with no output lines means "not installed".
    QList<RpmInfo> infos;
    for (const RpmInfo &info : parse(r.standardOutput))
        if (info.name == name)
            infos << info;
    return Result<QList<RpmInfo>>::success(infos);
}

Result<RpmInfo> RpmInspector::installedPackage(const QString &name) const
{
    const auto all = queryInstalled(name);
    if (!all.ok())
        return Result<RpmInfo>::failure(all.error);
    RpmInfo best;
    for (const RpmInfo &info : all.value)
        if (best.name.isEmpty() || compareEvr(info.evr, best.evr) > 0)
            best = info;
    return Result<RpmInfo>::success(best);
}

} // namespace Harpoon
