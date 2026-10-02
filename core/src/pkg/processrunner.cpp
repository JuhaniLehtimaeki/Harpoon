#include "pkg/processrunner.h"

#include <QProcess>

namespace Harpoon {

ProcessResult SystemProcessRunner::run(const QString &program, const QStringList &arguments, int timeoutMs)
{
    ProcessResult result;
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(timeoutMs))
        return result;
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return result;
    }
    result.started = process.exitStatus() == QProcess::NormalExit;
    result.exitCode = process.exitCode();
    result.standardOutput = process.readAllStandardOutput();
    result.standardError = process.readAllStandardError();
    return result;
}

} // namespace Harpoon
