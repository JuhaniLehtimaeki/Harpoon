#include "pkg/processrunner.h"

#include <QPointer>
#include <QProcess>
#include <QTimer>

#include <memory>

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

void SystemProcessRunner::runAsync(QObject *context, const QString &program, const QStringList &arguments,
                                   std::function<void(const ProcessResult &)> done, int timeoutMs)
{
    // Owned by the context: destroying it ends the process too.
    auto *process = new QProcess(context);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    auto ended = std::make_shared<bool>(false);
    QPointer<QObject> guard(context);
    // Exactly once, and not while the context is being destroyed. The
    // process object goes once the process has ended (after a kill too).
    auto finish = [done, ended, guard](const ProcessResult &result) {
        if (*ended)
            return;
        *ended = true;
        if (guard)
            done(result);
    };
    QObject::connect(timer, &QTimer::timeout, process, [process, finish]() {
        process->kill();
        finish(ProcessResult());
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            finish(ProcessResult());
        }
    });
    QObject::connect(process, static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), process,
                     [process, finish](int exitCode, QProcess::ExitStatus status) {
                         ProcessResult result;
                         result.started = status == QProcess::NormalExit;
                         result.exitCode = exitCode;
                         result.standardOutput = process->readAllStandardOutput();
                         result.standardError = process->readAllStandardError();
                         process->deleteLater();
                         finish(result);
                     });
    timer->start(timeoutMs);
    process->start(program, arguments);
}

} // namespace Harpoon
