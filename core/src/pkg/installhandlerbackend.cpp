#include "pkg/installhandlerbackend.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QTimer>
#include <QUrl>

namespace Harpoon {

namespace {
const QString kPath = QStringLiteral("/org/sailfishos/installationhandler");
const QString kInterface = QStringLiteral("org.sailfishos.installationhandler");
} // namespace

InstallHandlerBackend::InstallHandlerBackend(const QDBusConnection &bus, const QString &service, QObject *parent)
    : QObject(parent), m_bus(bus), m_service(service)
{
    m_bus.connect(m_service, kPath, kInterface, QStringLiteral("installFinished"), this,
                  SLOT(onInstallFinished(bool, QString)));
    m_bus.connect(m_service, kPath, kInterface, QStringLiteral("removalFinished"), this,
                  SLOT(onRemovalFinished(bool, QString)));
}

InstallHandlerBackend::~InstallHandlerBackend()
{
    m_bus.disconnect(m_service, kPath, kInterface, QStringLiteral("installFinished"), this,
                     SLOT(onInstallFinished(bool, QString)));
    m_bus.disconnect(m_service, kPath, kInterface, QStringLiteral("removalFinished"), this,
                     SLOT(onRemovalFinished(bool, QString)));
}

void InstallHandlerBackend::installFiles(const QStringList &paths, const InstallOptions &, Done done)
{
    QStringList urls;
    for (const QString &path : paths)
        urls << QUrl::fromLocalFile(path).toString();
    m_queue.push_back({Op::Install, urls, std::move(done)});
    if (!m_busy)
        runNext();
}

void InstallHandlerBackend::removePackage(const QString &name, Done done)
{
    m_queue.push_back({Op::Remove, QStringList{name}, std::move(done)});
    if (!m_busy)
        runNext();
}

void InstallHandlerBackend::runNext()
{
    if (m_queue.empty()) {
        m_busy = false;
        return;
    }
    m_busy = true;
    Job job = std::move(m_queue.front());
    m_queue.pop_front();
    m_currentOp = job.op;
    m_current = std::move(job.done);
    const quint64 generation = ++m_generation;

    QTimer::singleShot(m_timeoutMs, this, [this, generation]() {
        if (generation == m_generation && m_current)
            complete(m_currentOp, Error::make(Error::Install, QStringLiteral("The installation handler did not answer")));
    });

    QDBusMessage call = QDBusMessage::createMethodCall(
        m_service, kPath, kInterface,
        job.op == Op::Install ? QStringLiteral("installFiles") : QStringLiteral("removePackages"));
    call << job.args;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    const Op op = job.op;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, op, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError() && generation == m_generation && m_current) {
            const QDBusError e = w->error();
            const Error::Kind kind = e.type() == QDBusError::AccessDenied ? Error::NotAuthorized : Error::Install;
            complete(op, Error::make(kind, QStringLiteral("Installation handler: %1").arg(e.message())));
        }
    });
}

void InstallHandlerBackend::complete(Op op, const Error &error)
{
    if (!m_current || op != m_currentOp)
        return;
    Done done = std::move(m_current);
    m_current = nullptr;
    ++m_generation; // invalidates the pending timeout
    done(error);
    QTimer::singleShot(0, this, &InstallHandlerBackend::runNext);
}

void InstallHandlerBackend::onInstallFinished(bool success, const QString &error)
{
    complete(Op::Install, success ? Error()
                                  : Error::make(Error::Install, error.isEmpty() ? QStringLiteral("Installation failed or was declined")
                                                                                : error));
}

void InstallHandlerBackend::onRemovalFinished(bool success, const QString &error)
{
    complete(Op::Remove, success ? Error()
                                 : Error::make(Error::Install, error.isEmpty() ? QStringLiteral("Removal failed or was declined")
                                                                               : error));
}

} // namespace Harpoon
