#pragma once

#include "pkg/processrunner.h"
#include "pkg/rpminspector.h"

#include <QHash>

// ProcessRunner that runs `rpm -qp` for real (reads package files) but answers
// `rpm -q` from an in-memory "installed" database the test controls.
class FakeRpmDb : public Harpoon::ProcessRunner
{
public:
    QHash<QString, Harpoon::RpmInfo> installed;

    Harpoon::ProcessResult run(const QString &program, const QStringList &arguments, int timeoutMs) override
    {
        if (arguments.contains(QStringLiteral("-qp")))
            return m_real.run(program, arguments, timeoutMs);
        Harpoon::ProcessResult r;
        r.started = true;
        const QString name = arguments.last();
        if (!installed.contains(name)) {
            r.exitCode = 1;
            r.standardOutput = "package " + name.toUtf8() + " is not installed\n";
            return r;
        }
        const Harpoon::RpmInfo &i = installed.value(name);
        r.exitCode = 0;
        r.standardOutput = "@@" + i.name.toUtf8() + '\t' + (i.evr.epoch ? QByteArray::number(i.evr.epoch) : QByteArray("(none)"))
                           + '\t' + i.evr.version.toUtf8() + '\t' + i.evr.release.toUtf8() + '\t' + i.arch.toUtf8()
                           + '\t' + (i.vendor.isEmpty() ? QByteArray("(none)") : i.vendor.toUtf8()) + "\tsummary\n";
        return r;
    }

private:
    Harpoon::SystemProcessRunner m_real;
};
