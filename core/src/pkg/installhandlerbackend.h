#pragma once

#include "pkg/packagebackend.h"

#include <QDBusConnection>
#include <QObject>

#include <deque>

namespace Harpoon {

// SailfishOS system installation handler (session bus,
// org.sailfishos.installationhandler). Usable from a sandboxed app with the
// ApplicationInstallation permission. The system asks the user to confirm
// every operation and "Allow untrusted software" must be enabled.
//
//   installFiles(as file:// URLs)  -> signal installFinished(bool, QString)
//   removePackages(as names)       -> signal removalFinished(bool, QString)
//
// Reference users: harbour-music-sleep-timer (install),
// openrepos-clock-settings (remove).
class InstallHandlerBackend : public QObject, public PackageBackend
{
    Q_OBJECT
public:
    explicit InstallHandlerBackend(const QDBusConnection &bus = QDBusConnection::sessionBus(),
                                   const QString &service = QStringLiteral("org.sailfishos.installationhandler"),
                                   QObject *parent = nullptr);
    ~InstallHandlerBackend() override;

    QString name() const override { return QStringLiteral("Installation handler"); }
    bool isSilent() const override { return false; }

    // The handler has no reinstall/downgrade flags; options are ignored and
    // the system dialog decides.
    void installFiles(const QStringList &paths, const InstallOptions &options, Done done) override;
    void removePackage(const QString &name, Done done) override;

    // Waiting for the user is open-ended, but not forever.
    void setTimeoutMs(int ms) { m_timeoutMs = ms; }
    // The handler's signals carry no request id. After a timeout, the next
    // job waits up to this long for the late reply, which is then dropped,
    // so it cannot be credited to the wrong job.
    void setDrainMs(int ms) { m_drainMs = ms; }

private slots:
    void onInstallFinished(bool success, const QString &error);
    void onRemovalFinished(bool success, const QString &error);

private:
    enum class Op { Install, Remove };
    struct Job
    {
        Op op;
        QStringList args;
        Done done;
    };
    void runNext();
    void complete(Op op, const Error &error);
    void stopDraining();

    QDBusConnection m_bus;
    QString m_service;
    int m_timeoutMs = 30 * 60 * 1000;
    int m_drainMs = 2 * 60 * 1000;
    bool m_draining = false;
    Op m_drainOp = Op::Install;
    quint64 m_drainGeneration = 0;
    std::deque<Job> m_queue;
    bool m_busy = false;
    Op m_currentOp = Op::Install;
    Done m_current;
    quint64 m_generation = 0;
};

} // namespace Harpoon
