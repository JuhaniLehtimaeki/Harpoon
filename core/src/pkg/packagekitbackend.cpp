#include "pkg/packagekitbackend.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace Harpoon {

namespace {

const QString kTransactionInterface = QStringLiteral("org.freedesktop.PackageKit.Transaction");

Error errorFromDBus(const QDBusError &dbusError)
{
    const QString name = dbusError.name();
    const Error::Kind kind = (name.contains(QLatin1String("RefusedByPolicy")) || name.contains(QLatin1String("NotAuthorized"))
                              || name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied"))
                                 ? Error::NotAuthorized
                                 : Error::Install;
    return Error::make(kind, QStringLiteral("PackageKit: %1 (%2)").arg(dbusError.message(), name));
}

} // namespace

// Receives one transaction's D-Bus signals. QDBusConnection::connect() only
// accepts SLOT() strings, hence a small QObject with slots.
class PkTransactionWatcher : public QObject
{
    Q_OBJECT
public:
    using Finish = std::function<void(const Error &, const QStringList &)>;

    PkTransactionWatcher(const QDBusConnection &bus, const QString &service, const QString &path, Finish finish)
        : m_bus(bus), m_service(service), m_path(path), m_finish(std::move(finish))
    {
        m_bus.connect(m_service, m_path, kTransactionInterface, QStringLiteral("Finished"), this,
                      SLOT(onFinished(uint, uint)));
        m_bus.connect(m_service, m_path, kTransactionInterface, QStringLiteral("ErrorCode"), this,
                      SLOT(onErrorCode(uint, QString)));
        m_bus.connect(m_service, m_path, kTransactionInterface, QStringLiteral("Package"), this,
                      SLOT(onPackage(uint, QString, QString)));
    }

    ~PkTransactionWatcher() override
    {
        m_bus.disconnect(m_service, m_path, kTransactionInterface, QStringLiteral("Finished"), this,
                         SLOT(onFinished(uint, uint)));
        m_bus.disconnect(m_service, m_path, kTransactionInterface, QStringLiteral("ErrorCode"), this,
                         SLOT(onErrorCode(uint, QString)));
        m_bus.disconnect(m_service, m_path, kTransactionInterface, QStringLiteral("Package"), this,
                         SLOT(onPackage(uint, QString, QString)));
    }

    // Ends the transaction early (call failure, timeout). Idempotent.
    void fail(const Error &error) { finish(error); }

public slots:
    void onFinished(uint exit, uint /*runtimeMs*/)
    {
        if (exit == PackageKitBackend::ExitSuccess) {
            finish(Error());
        } else if (!m_error.ok()) {
            finish(m_error);
        } else {
            finish(Error::make(exit == PackageKitBackend::ExitCancelled ? Error::Cancelled : Error::Install,
                               QStringLiteral("PackageKit transaction failed (exit %1)").arg(exit)));
        }
    }

    void onErrorCode(uint code, const QString &details)
    {
        // Keep the first error; PackageKit may follow up with generic ones.
        if (m_error.ok())
            m_error = Error::make(PackageKitBackend::errorKindFor(code),
                                  QStringLiteral("PackageKit error %1: %2").arg(code).arg(details.trimmed()));
    }

    void onPackage(uint info, const QString &packageId, const QString & /*summary*/)
    {
        if (info == PackageKitBackend::InfoInstalled || packageId.endsWith(QLatin1String(";installed")))
            m_packageIds << packageId;
    }

private:
    void finish(const Error &error)
    {
        if (!m_finish)
            return;
        Finish f = std::move(m_finish);
        m_finish = nullptr;
        f(error, m_packageIds);
        deleteLater();
    }

    QDBusConnection m_bus;
    QString m_service;
    QString m_path;
    Finish m_finish;
    Error m_error;
    QStringList m_packageIds;
};

PackageKitBackend::PackageKitBackend(const QDBusConnection &bus, const QString &service, QObject *parent)
    : QObject(parent), m_bus(bus), m_service(service)
{
}

Error::Kind PackageKitBackend::errorKindFor(uint code)
{
    switch (code) {
    case 9:  // PACKAGE_ALREADY_INSTALLED
    case 41: // ALL_PACKAGES_ALREADY_INSTALLED
        return Error::AlreadyInstalled;
    case 17: // TRANSACTION_CANCELLED
    case 65: // CANCELLED_PRIORITY
        return Error::Cancelled;
    case 38: // INVALID_PACKAGE_FILE
        return Error::Package;
    case 45: // INCOMPATIBLE_ARCHITECTURE
        return Error::WrongArch;
    case 26: // CANNOT_GET_LOCK
    case 67: // LOCK_REQUIRED
        return Error::Busy;
    case 48: // NOT_AUTHORIZED
        return Error::NotAuthorized;
    default:
        return Error::Install;
    }
}

void PackageKitBackend::enqueue(std::function<void(std::function<void()>)> job)
{
    m_queue.push_back(std::move(job));
    if (!m_busy)
        runNext();
}

void PackageKitBackend::runNext()
{
    if (m_queue.empty()) {
        m_busy = false;
        return;
    }
    m_busy = true;
    auto job = std::move(m_queue.front());
    m_queue.pop_front();
    // The job calls this when it is done, which starts the next one.
    job([this]() { QTimer::singleShot(0, this, &PackageKitBackend::runNext); });
}

void PackageKitBackend::runTransaction(const QString &method, const QList<QVariant> &args, TransactionDone done)
{
    QDBusMessage create = QDBusMessage::createMethodCall(m_service, QStringLiteral("/org/freedesktop/PackageKit"),
                                                         QStringLiteral("org.freedesktop.PackageKit"),
                                                         QStringLiteral("CreateTransaction"));
    auto *createWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(create), this);
    connect(createWatcher, &QDBusPendingCallWatcher::finished, this,
            [this, method, args, done](QDBusPendingCallWatcher *w) {
                w->deleteLater();
                QDBusPendingReply<QDBusObjectPath> reply = *w;
                if (reply.isError()) {
                    done({errorFromDBus(reply.error()), {}});
                    return;
                }
                const QString path = reply.value().path();

                auto *tx = new PkTransactionWatcher(m_bus, m_service, path,
                                                    [done](const Error &e, const QStringList &ids) {
                                                        done({e, ids});
                                                    });
                QPointer<PkTransactionWatcher> guard(tx);
                QTimer::singleShot(m_timeoutMs, tx, [guard]() {
                    if (guard)
                        guard->fail(Error::make(Error::Install, QStringLiteral("PackageKit transaction timed out")));
                });

                QDBusMessage call = QDBusMessage::createMethodCall(m_service, path, kTransactionInterface, method);
                call.setArguments(args);
                auto *callWatcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
                connect(callWatcher, &QDBusPendingCallWatcher::finished, this, [guard](QDBusPendingCallWatcher *cw) {
                    cw->deleteLater();
                    if (cw->isError() && guard)
                        guard->fail(errorFromDBus(cw->error()));
                });
            });
}

void PackageKitBackend::installFiles(const QStringList &paths, const InstallOptions &options, Done done)
{
    quint64 flags = FlagNone;
    if (options.allowReinstall)
        flags |= FlagAllowReinstall;
    if (options.allowDowngrade)
        flags |= FlagAllowDowngrade;

    enqueue([this, paths, flags, done](std::function<void()> next) {
        runTransaction(QStringLiteral("InstallFiles"), {QVariant::fromValue<qulonglong>(flags), paths},
                       [done, next](const TransactionResult &r) {
                           done(r.error);
                           next();
                       });
    });
}

void PackageKitBackend::removePackage(const QString &name, Done done)
{
    enqueue([this, name, done](std::function<void()> next) {
        runTransaction(QStringLiteral("Resolve"),
                       {QVariant::fromValue<qulonglong>(FilterInstalled), QStringList{name}},
                       [this, name, done, next](const TransactionResult &resolved) {
                           if (!resolved.error.ok() || resolved.packageIds.isEmpty()) {
                               done(!resolved.error.ok()
                                        ? resolved.error
                                        : Error::make(Error::Package, QStringLiteral("%1 is not installed").arg(name)));
                               next();
                               return;
                           }
                           runTransaction(QStringLiteral("RemovePackages"),
                                          {QVariant::fromValue<qulonglong>(FlagNone), resolved.packageIds, false, false},
                                          [done, next](const TransactionResult &removed) {
                                              done(removed.error);
                                              next();
                                          });
                       });
    });
}

} // namespace Harpoon

#include "packagekitbackend.moc"
