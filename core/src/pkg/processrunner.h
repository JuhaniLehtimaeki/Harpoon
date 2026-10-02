#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

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
};

// Runs real processes synchronously with QProcess.
class SystemProcessRunner : public ProcessRunner
{
public:
    ProcessResult run(const QString &program, const QStringList &arguments, int timeoutMs = 15000) override;
};

} // namespace Harpoon
