#pragma once

#include "pkg/processrunner.h"
#include "pkg/rpminspector.h"

#include <QHash>
#include <QList>

#include <functional>

// ProcessRunner that runs `rpm -qp` for real (reads package files) but answers
// `rpm -q` from an in-memory "installed" database the test controls.
class FakeRpmDb : public Harpoon::ProcessRunner
{
public:
    QHash<QString, Harpoon::RpmInfo> installed;
    int calls = 0;   // rpm -q runs
    int queries = 0; // package names asked about
    // When set, runAsync() answers (as of the call) only when deliver() is
    // called, in any order; otherwise it answers at once.
    bool deferAsync = false;
    QList<std::function<void()>> pending;

    void runAsync(QObject *context, const QString &program, const QStringList &arguments,
                  std::function<void(const Harpoon::ProcessResult &)> done, int timeoutMs) override
    {
        if (!deferAsync) {
            ProcessRunner::runAsync(context, program, arguments, done, timeoutMs);
            return;
        }
        const Harpoon::ProcessResult result = run(program, arguments, timeoutMs);
        pending << [done, result]() { done(result); };
    }

    void deliver(int index = 0) { pending.takeAt(index)(); }

    Harpoon::ProcessResult run(const QString &program, const QStringList &arguments, int timeoutMs) override
    {
        if (arguments.contains(QStringLiteral("-qp")))
            return m_real.run(program, arguments, timeoutMs);
        Harpoon::ProcessResult r;
        r.started = true;
        const int dashes = arguments.indexOf(QStringLiteral("--"));
        const QStringList names = dashes >= 0 ? arguments.mid(dashes + 1) : QStringList{arguments.last()};
        r.exitCode = 0;
        for (const QString &name : names) {
            ++queries;
            if (!installed.contains(name)) {
                r.exitCode = 1;
                r.standardOutput += "package " + name.toUtf8() + " is not installed\n";
                continue;
            }
            const Harpoon::RpmInfo &i = installed.value(name);
            r.standardOutput += "@@" + i.name.toUtf8() + '\t'
                                + (i.evr.epoch ? QByteArray::number(i.evr.epoch) : QByteArray("(none)")) + '\t'
                                + i.evr.version.toUtf8() + '\t' + i.evr.release.toUtf8() + '\t' + i.arch.toUtf8() + '\t'
                                + (i.vendor.isEmpty() ? QByteArray("(none)") : i.vendor.toUtf8()) + "\tsummary\n";
        }
        ++calls;
        return r;
    }

private:
    Harpoon::SystemProcessRunner m_real;
};
