#include "pkg/installhandlerbackend.h"

#include <QElapsedTimer>
#include "pkg/packagekitbackend.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QProcess>
#include <QTimer>
#include <QtTest>

using namespace Harpoon;

// ---- Mock org.freedesktop.PackageKit ---------------------------------------

class MockPackageKit;

class MockPkTransaction : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PackageKit.Transaction")
public:
    MockPkTransaction(MockPackageKit *pk, const QString &path) : m_pk(pk), m_path(path) {}

public slots:
    void InstallFiles(qulonglong flags, const QStringList &paths);
    void Resolve(qulonglong filter, const QStringList &names);
    void RemovePackages(qulonglong flags, const QStringList &ids, bool allowDeps, bool autoremove);
    void Cancel();

private:
    bool refuseIfConfigured();
    void finishLater(QList<QPair<uint, QString>> packages = {});

    MockPackageKit *m_pk;
    QString m_path;
};

class MockPackageKit : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.PackageKit")
public:
    struct Call
    {
        QString method;
        qulonglong flags;
        QStringList args;
    };

    explicit MockPackageKit(const QDBusConnection &bus) : bus(bus) {}

    QDBusConnection bus;
    QList<Call> calls;
    QHash<QString, QString> installedIds; // name -> package id
    uint nextExit = PackageKitBackend::ExitSuccess;
    int nextErrorCode = -1;
    QString nextErrorDetails;
    bool refuseNext = false;
    int active = 0;
    int maxActive = 0;
    int finishDelayMs = 20;
    int counter = 0;

    void emitSignal(const QString &path, const QString &name, const QList<QVariant> &args)
    {
        QDBusMessage m = QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.PackageKit.Transaction"), name);
        m.setArguments(args);
        bus.send(m);
    }

public slots:
    QDBusObjectPath CreateTransaction()
    {
        const QString path = QStringLiteral("/%1").arg(++counter);
        auto *tx = new MockPkTransaction(this, path);
        tx->setParent(this);
        bus.registerObject(path, tx, QDBusConnection::ExportAllSlots);
        return QDBusObjectPath(path);
    }
};

bool MockPkTransaction::refuseIfConfigured()
{
    if (!m_pk->refuseNext)
        return false;
    m_pk->refuseNext = false;
    sendErrorReply(QStringLiteral("org.freedesktop.PackageKit.Transaction.RefusedByPolicy"),
                   QStringLiteral("Not allowed"));
    return true;
}

void MockPkTransaction::finishLater(QList<QPair<uint, QString>> packages)
{
    ++m_pk->active;
    m_pk->maxActive = qMax(m_pk->maxActive, m_pk->active);
    const uint exit = m_pk->nextExit;
    const int errorCode = m_pk->nextErrorCode;
    const QString details = m_pk->nextErrorDetails;
    m_pk->nextExit = PackageKitBackend::ExitSuccess;
    m_pk->nextErrorCode = -1;
    MockPackageKit *pk = m_pk;
    const QString path = m_path;
    QTimer::singleShot(pk->finishDelayMs, pk, [pk, path, exit, errorCode, details, packages]() {
        for (const auto &p : packages)
            pk->emitSignal(path, QStringLiteral("Package"), {p.first, p.second, QStringLiteral("summary")});
        if (errorCode >= 0)
            pk->emitSignal(path, QStringLiteral("ErrorCode"), {uint(errorCode), details});
        --pk->active;
        pk->emitSignal(path, QStringLiteral("Finished"), {exit, uint(5)});
    });
}

void MockPkTransaction::Cancel()
{
    m_pk->calls << MockPackageKit::Call{QStringLiteral("Cancel"), 0, {}};
    m_pk->emitSignal(m_path, QStringLiteral("Finished"), {uint(PackageKitBackend::ExitCancelled), uint(1)});
}

void MockPkTransaction::InstallFiles(qulonglong flags, const QStringList &paths)
{
    m_pk->calls << MockPackageKit::Call{QStringLiteral("InstallFiles"), flags, paths};
    if (!refuseIfConfigured())
        finishLater();
}

void MockPkTransaction::Resolve(qulonglong filter, const QStringList &names)
{
    m_pk->calls << MockPackageKit::Call{QStringLiteral("Resolve"), filter, names};
    QList<QPair<uint, QString>> packages;
    for (const QString &n : names)
        if (m_pk->installedIds.contains(n))
            packages << qMakePair(uint(PackageKitBackend::InfoInstalled), m_pk->installedIds.value(n));
    finishLater(packages);
}

void MockPkTransaction::RemovePackages(qulonglong flags, const QStringList &ids, bool allowDeps, bool autoremove)
{
    m_pk->calls << MockPackageKit::Call{QStringLiteral("RemovePackages"), flags, ids};
    QVERIFY(!allowDeps);
    QVERIFY(!autoremove);
    finishLater();
}

// ---- Mock org.sailfishos.installationhandler --------------------------------

class MockInstallHandler : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.sailfishos.installationhandler")
public:
    explicit MockInstallHandler(const QDBusConnection &bus) : bus(bus) {}
    QDBusConnection bus;
    QList<QStringList> installs;
    QList<QStringList> removals;
    bool succeed = true;
    int delayMs = 10;

public slots:
    void installFiles(const QStringList &urls)
    {
        installs << urls;
        reply(QStringLiteral("installFinished"));
    }
    void removePackages(const QStringList &names)
    {
        removals << names;
        reply(QStringLiteral("removalFinished"));
    }

private:
    void reply(const QString &signal)
    {
        const bool ok = succeed;
        QTimer::singleShot(delayMs, this, [this, signal, ok]() {
            QDBusMessage m = QDBusMessage::createSignal(QStringLiteral("/org/sailfishos/installationhandler"),
                                                        QStringLiteral("org.sailfishos.installationhandler"), signal);
            m << ok << (ok ? QString() : QStringLiteral("User declined"));
            bus.send(m);
        });
    }
};

// ---- Tests -----------------------------------------------------------------

namespace {
Error waitFor(std::function<void(PackageBackend::Done)> start)
{
    Error result;
    bool finished = false;
    start([&](const Error &e) {
        result = e;
        finished = true;
    });
    if (!QTest::qWaitFor([&]() { return finished; }, 5000))
        result = Error::make(Error::Install, QStringLiteral("test timeout"));
    return result;
}
} // namespace

class TestPackageBackends : public QObject
{
    Q_OBJECT

    QProcess m_daemon;
    QString m_address;
    MockPackageKit *m_pk = nullptr;
    MockInstallHandler *m_handler = nullptr;

    QDBusConnection client() { return QDBusConnection(QStringLiteral("client")); }

private slots:
    void initTestCase()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("dbus-daemon")).isEmpty())
            QSKIP("dbus-daemon not installed");
        m_daemon.start(QStringLiteral("dbus-daemon"),
                       {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address")});
        QVERIFY(m_daemon.waitForStarted());
        QTRY_VERIFY(m_daemon.canReadLine());
        m_address = QString::fromLatin1(m_daemon.readLine()).trimmed();

        QDBusConnection service = QDBusConnection::connectToBus(m_address, QStringLiteral("service"));
        QVERIFY(service.isConnected());
        QVERIFY(QDBusConnection::connectToBus(m_address, QStringLiteral("client")).isConnected());

        m_pk = new MockPackageKit(service);
        QVERIFY(service.registerObject(QStringLiteral("/org/freedesktop/PackageKit"), m_pk, QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService(QStringLiteral("org.freedesktop.PackageKit")));

        m_handler = new MockInstallHandler(service);
        QVERIFY(service.registerObject(QStringLiteral("/org/sailfishos/installationhandler"), m_handler,
                                       QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService(QStringLiteral("org.sailfishos.installationhandler")));
    }

    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("service"));
        delete m_pk;
        delete m_handler;
        m_daemon.kill();
        m_daemon.waitForFinished();
    }

    void init()
    {
        m_pk->calls.clear();
        m_pk->maxActive = 0;
    }

    void installSuccess()
    {
        PackageKitBackend backend(client());
        const Error e = waitFor([&](PackageBackend::Done d) {
            backend.installFiles({QStringLiteral("/tmp/a.rpm"), QStringLiteral("/tmp/b.rpm")}, InstallOptions(), d);
        });
        QVERIFY2(e.ok(), qPrintable(e.message));
        QCOMPARE(m_pk->calls.size(), 1);
        QCOMPARE(m_pk->calls.first().method, QStringLiteral("InstallFiles"));
        QCOMPARE(m_pk->calls.first().flags, qulonglong(0));
        QCOMPARE(m_pk->calls.first().args, (QStringList{"/tmp/a.rpm", "/tmp/b.rpm"}));
    }

    void installFlags()
    {
        PackageKitBackend backend(client());
        InstallOptions o;
        o.allowReinstall = true;
        o.allowDowngrade = true;
        QVERIFY(waitFor([&](PackageBackend::Done d) { backend.installFiles({QStringLiteral("/tmp/a.rpm")}, o, d); }).ok());
        QCOMPARE(m_pk->calls.first().flags, qulonglong((1 << 4) | (1 << 6)));
    }

    void installErrorCode()
    {
        PackageKitBackend backend(client());
        m_pk->nextExit = PackageKitBackend::ExitFailed;
        m_pk->nextErrorCode = 29; // LOCAL_INSTALL_FAILED
        m_pk->nextErrorDetails = QStringLiteral("conflicts with file from package foo");
        const Error e = waitFor([&](PackageBackend::Done d) {
            backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
        });
        QCOMPARE(int(e.kind), int(Error::Install));
        QVERIFY(e.message.contains(QLatin1String("conflicts with file")));

        m_pk->nextExit = PackageKitBackend::ExitFailed;
        m_pk->nextErrorCode = 48; // NOT_AUTHORIZED
        QCOMPARE(int(waitFor([&](PackageBackend::Done d) {
                         backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
                     }).kind),
                 int(Error::NotAuthorized));
    }

    void refusedByPolicy()
    {
        PackageKitBackend backend(client());
        m_pk->refuseNext = true;
        const Error e = waitFor([&](PackageBackend::Done d) {
            backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
        });
        QCOMPARE(int(e.kind), int(Error::NotAuthorized));
        // The queue must keep working after a refused call.
        QVERIFY(waitFor([&](PackageBackend::Done d) {
                    backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
                }).ok());
    }

    void transactionsAreSerialized()
    {
        PackageKitBackend backend(client());
        int done = 0;
        QList<int> order;
        for (int i = 0; i < 3; ++i)
            backend.installFiles({QStringLiteral("/tmp/%1.rpm").arg(i)}, InstallOptions(), [&, i](const Error &e) {
                QVERIFY(e.ok());
                order << i;
                ++done;
            });
        QTRY_COMPARE_WITH_TIMEOUT(done, 3, 5000);
        QCOMPARE(order, (QList<int>{0, 1, 2}));
        QCOMPARE(m_pk->maxActive, 1);
    }

    void removeResolvesThenRemoves()
    {
        PackageKitBackend backend(client());
        m_pk->installedIds.insert(QStringLiteral("harbour-demo"), QStringLiteral("harbour-demo;1.0-1;aarch64;installed"));
        const Error e = waitFor([&](PackageBackend::Done d) { backend.removePackages({QStringLiteral("harbour-demo")}, d); });
        QVERIFY2(e.ok(), qPrintable(e.message));
        QCOMPARE(m_pk->calls.size(), 2);
        QCOMPARE(m_pk->calls.at(0).method, QStringLiteral("Resolve"));
        QCOMPARE(m_pk->calls.at(0).flags, qulonglong(PackageKitBackend::FilterInstalled));
        QCOMPARE(m_pk->calls.at(1).method, QStringLiteral("RemovePackages"));
        QCOMPARE(m_pk->calls.at(1).args, QStringList{"harbour-demo;1.0-1;aarch64;installed"});
    }

    void removeNotInstalled()
    {
        PackageKitBackend backend(client());
        const Error e = waitFor([&](PackageBackend::Done d) { backend.removePackages({QStringLiteral("nothing")}, d); });
        QCOMPARE(int(e.kind), int(Error::Package));
        QCOMPARE(m_pk->calls.size(), 1); // no RemovePackages
    }

    void missingService()
    {
        PackageKitBackend backend(client(), QStringLiteral("org.example.NoSuchService"));
        const Error e = waitFor([&](PackageBackend::Done d) {
            backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
        });
        QVERIFY(!e.ok());
    }

    void noBusFailsFast()
    {
        PackageKitBackend pk(QDBusConnection(QStringLiteral("none")));
        QCOMPARE(int(waitFor([&](PackageBackend::Done d) {
                         pk.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
                     }).kind),
                 int(Error::Install));
        InstallHandlerBackend handler(QDBusConnection(QStringLiteral("none")));
        QCOMPARE(int(waitFor([&](PackageBackend::Done d) { handler.removePackages({QStringLiteral("x")}, d); }).kind),
                 int(Error::Install));
    }

    void timeoutCancelsTheTransaction()
    {
        PackageKitBackend backend(client());
        backend.setTransactionTimeoutMs(100);
        m_pk->finishDelayMs = 3000; // PackageKit "hangs"
        m_pk->calls.clear();
        QElapsedTimer timer;
        timer.start();
        const Error e = waitFor([&](PackageBackend::Done d) {
            backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
        });
        m_pk->finishDelayMs = 20;
        // Cancelled, and reported once PackageKit confirmed it, not after a
        // fixed grace period.
        QCOMPARE(int(e.kind), int(Error::Cancelled));
        QVERIFY(timer.elapsed() < 2000);
        QCOMPARE(m_pk->calls.last().method, QStringLiteral("Cancel"));
        QTest::qWait(3200); // let the stray Finished pass
    }

    void installHandler()
    {
        InstallHandlerBackend backend(client());
        m_handler->succeed = true;
        QVERIFY(waitFor([&](PackageBackend::Done d) {
                    backend.installFiles({QStringLiteral("/home/u/.cache/x/a b.rpm")}, InstallOptions(), d);
                }).ok());
        QCOMPARE(m_handler->installs.last(), QStringList{"file:///home/u/.cache/x/a b.rpm"});

        m_handler->succeed = false;
        const Error declined = waitFor([&](PackageBackend::Done d) {
            backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
        });
        QCOMPARE(int(declined.kind), int(Error::Install));
        QCOMPARE(declined.message, QStringLiteral("User declined"));

        m_handler->succeed = true;
        QVERIFY(waitFor([&](PackageBackend::Done d) { backend.removePackages({QStringLiteral("harbour-demo")}, d); }).ok());
        QCOMPARE(m_handler->removals.last(), QStringList{"harbour-demo"});
    }

    void installHandlerTimeout()
    {
        InstallHandlerBackend backend(client(), QStringLiteral("org.example.Silent"));
        backend.setTimeoutMs(200);
        QVERIFY(!waitFor([&](PackageBackend::Done d) {
                     backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
                 }).ok());
    }

    void lateReplyIsNotCreditedToTheNextJob()
    {
        InstallHandlerBackend backend(client());
        backend.setTimeoutMs(100);
        m_handler->delayMs = 400; // answers after the timeout
        m_handler->succeed = true;
        const int before = m_handler->installs.size();

        Error first;
        Error second;
        bool firstDone = false;
        bool secondDone = false;
        backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), [&](const Error &e) {
            first = e;
            firstDone = true;
            // The user declines the next one; the late "success" of the
            // first dialog must not be reported for it.
            m_handler->succeed = false;
            m_handler->delayMs = 600; // answers after the stray reply
            backend.setTimeoutMs(3000);
            backend.installFiles({QStringLiteral("/tmp/b.rpm")}, InstallOptions(), [&](const Error &e2) {
                second = e2;
                secondDone = true;
            });
        });
        QVERIFY(QTest::qWaitFor([&]() { return secondDone; }, 5000));
        QVERIFY(firstDone);
        QCOMPARE(first.message, QStringLiteral("The installation handler did not answer"));
        QCOMPARE(second.message, QStringLiteral("User declined"));
        QCOMPARE(m_handler->installs.size(), before + 2);
        m_handler->delayMs = 10;
        m_handler->succeed = true;
    }

    void drainEndsWhenNoReplyComes()
    {
        InstallHandlerBackend backend(client());
        backend.setTimeoutMs(100);
        backend.setDrainMs(100);
        m_handler->delayMs = 1500; // far beyond timeout and drain
        QVERIFY(!waitFor([&](PackageBackend::Done d) {
                     backend.installFiles({QStringLiteral("/tmp/a.rpm")}, InstallOptions(), d);
                 }).ok());
        // The next job runs once the drain period ends, before the late reply.
        m_handler->delayMs = 10;
        QElapsedTimer timer;
        timer.start();
        QVERIFY(waitFor([&](PackageBackend::Done d) {
                    backend.removePackages({QStringLiteral("harbour-demo")}, d);
                }).ok());
        QVERIFY(timer.elapsed() < 1000);
        QTest::qWait(1600); // let the stray signal pass
    }
};

QTEST_GUILESS_MAIN(TestPackageBackends)
#include "tst_packagebackends.moc"
