#pragma once

#include "pkg/packagebackend.h"

#include <QDBusConnection>
#include <QObject>

#include <deque>

namespace Harpoon {

// PackageKit over D-Bus (system bus: org.freedesktop.PackageKit).
//
// Each operation creates a transaction (CreateTransaction), subscribes to its
// ErrorCode/Package/Finished signals and calls the method:
//   install: InstallFiles(flags, paths)
//   remove:  Resolve(filter=installed, [name]) then RemovePackages(0, ids, false, false)
//
// SailfishOS's PackageKit allows these calls without polkit when the caller's
// effective group is "privileged" (privileges.d entry, or devel-su -p).
class PackageKitBackend : public QObject, public PackageBackend
{
    Q_OBJECT
public:
    // PackageKit enum values (pk-enum.h); flags and filters are bit positions.
    enum : quint64 {
        FlagNone = 0,
        FlagOnlyTrusted = 1ull << 1,
        FlagAllowReinstall = 1ull << 4,
        FlagAllowDowngrade = 1ull << 6,
        FilterInstalled = 1ull << 2,
    };
    enum : uint { ExitSuccess = 1, ExitFailed = 2, ExitCancelled = 3, InfoInstalled = 1 };

    explicit PackageKitBackend(const QDBusConnection &bus = QDBusConnection::systemBus(),
                               const QString &service = QStringLiteral("org.freedesktop.PackageKit"),
                               QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("PackageKit"); }
    bool isSilent() const override { return true; }

    void installFiles(const QStringList &paths, const InstallOptions &options, Done done) override;
    void removePackage(const QString &name, Done done) override;

    // Abort a transaction that has not finished after this long.
    void setTransactionTimeoutMs(int ms) { m_timeoutMs = ms; }

    // Maps a PackageKit ErrorCode to a Harpoon error kind.
    static Error::Kind errorKindFor(uint pkErrorCode);

private:
    struct TransactionResult
    {
        Error error;
        QStringList packageIds; // from Package signals
    };
    using TransactionDone = std::function<void(const TransactionResult &)>;

    // Creates a transaction, connects its signals, then invokes method(args).
    void runTransaction(const QString &method, const QList<QVariant> &args, TransactionDone done);
    void enqueue(std::function<void(std::function<void()>)> job);
    void runNext();

    QDBusConnection m_bus;
    QString m_service;
    int m_timeoutMs = 15 * 60 * 1000;
    std::deque<std::function<void(std::function<void()>)>> m_queue;
    bool m_busy = false;
};

} // namespace Harpoon
