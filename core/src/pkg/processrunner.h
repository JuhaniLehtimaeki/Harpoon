#pragma once

#include <QByteArray>
#include <QtGlobal>
#include <QString>
#include <QStringList>

#include <functional>

class QObject;

namespace Harpoon {

struct ProcessResult
{
    bool started = false; // false: program missing or timed out
    int exitCode = -1;
    QByteArray standardOutput;
    QByteArray standardError;
};

// Runs short-lived helper programs (rpm). An interface so tests can fake them.
class ProcessRunner
{
public:
    virtual ~ProcessRunner() = default;
    virtual ProcessResult run(const QString &program, const QStringList &arguments, int timeoutMs = 15000) = 0;

    // Runs without blocking and calls done on context's thread, unless
    // context is destroyed first. This default runs synchronously, calling
    // done before it returns (good enough for fakes).
    virtual void runAsync(QObject *context, const QString &program, const QStringList &arguments,
                          std::function<void(const ProcessResult &)> done, int timeoutMs = 15000)
    {
        Q_UNUSED(context)
        done(run(program, arguments, timeoutMs));
    }
};

// Runs real processes with QProcess.
class SystemProcessRunner : public ProcessRunner
{
public:
    ProcessResult run(const QString &program, const QStringList &arguments, int timeoutMs = 15000) override;
    void runAsync(QObject *context, const QString &program, const QStringList &arguments,
                  std::function<void(const ProcessResult &)> done, int timeoutMs = 15000) override;
};

} // namespace Harpoon
