// Notifications, update-notification planning, the systemd background
// scheduler and backups. D-Bus parts run against mocks on a private
// dbus-daemon.

#include "app/backgroundscheduler.h"
#include "app/backup.h"
#include "app/updatenotifications.h"
#include "notify/notifier.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDataStream>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace Harpoon;

class MockNotifications : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public:
    struct Call
    {
        QString appName;
        uint replacesId;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
    };
    QList<Call> calls;
    QList<uint> closed;

public slots:
    uint Notify(const QString &appName, uint replacesId, const QString &, const QString &summary, const QString &body,
                const QStringList &actions, const QVariantMap &hints, int)
    {
        calls << Call{appName, replacesId, summary, body, actions, hints};
        return replacesId ? replacesId : uint(calls.size() + 100);
    }
    void CloseNotification(uint id) { closed << id; }
};

class MockSystemd : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.systemd1.Manager")
public:
    QStringList calls;
    QString failMethod;

public slots:
    void Reload() { record(QStringLiteral("Reload")); }
    QDBusObjectPath RestartUnit(const QString &name, const QString &mode)
    {
        record(QStringLiteral("RestartUnit ") + name + QLatin1Char(' ') + mode);
        return QDBusObjectPath(QStringLiteral("/job/1"));
    }
    QDBusObjectPath StopUnit(const QString &name, const QString &mode)
    {
        record(QStringLiteral("StopUnit ") + name + QLatin1Char(' ') + mode);
        return QDBusObjectPath(QStringLiteral("/job/2"));
    }
    void EnableUnitFiles(const QStringList &files, bool runtime, bool force)
    {
        record(QStringLiteral("EnableUnitFiles %1 %2 %3").arg(files.join(QLatin1Char(',')), b(runtime), b(force)));
    }
    void DisableUnitFiles(const QStringList &files, bool runtime)
    {
        record(QStringLiteral("DisableUnitFiles %1 %2").arg(files.join(QLatin1Char(',')), b(runtime)));
    }

private:
    static QString b(bool v) { return v ? QStringLiteral("true") : QStringLiteral("false"); }
    void record(const QString &call)
    {
        calls << call;
        if (!failMethod.isEmpty() && call.startsWith(failMethod))
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.AccessDenied"), QStringLiteral("denied"));
    }
};

namespace {
App appWith(const QString &name, const QString &latest, const QString &notified = QString())
{
    App a = App::fromUrl(QStringLiteral("https://github.com/x/") + name);
    a.name = name;
    a.latestVersion = latest;
    a.notifiedVersion = notified;
    return a;
}

UpdateStatus status(UpdateState s)
{
    UpdateStatus st;
    st.state = s;
    return st;
}

Error waitFor(std::function<void(std::function<void(const Error &)>)> start)
{
    Error result = Error::make(Error::System, QStringLiteral("not finished"));
    bool done = false;
    start([&](const Error &e) {
        result = e;
        done = true;
    });
    if (!QTest::qWaitFor([&]() { return done; }, 5000))
        return Error::make(Error::System, QStringLiteral("test timeout"));
    return result;
}
} // namespace

class TestBackground : public QObject
{
    Q_OBJECT

    QProcess m_daemon;
    MockNotifications *m_notifications = nullptr;
    MockSystemd *m_systemd = nullptr;

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
        const QString address = QString::fromLatin1(m_daemon.readLine()).trimmed();
        QDBusConnection service = QDBusConnection::connectToBus(address, QStringLiteral("service"));
        QVERIFY(QDBusConnection::connectToBus(address, QStringLiteral("client")).isConnected());
        m_notifications = new MockNotifications;
        QVERIFY(service.registerObject(QStringLiteral("/org/freedesktop/Notifications"), m_notifications,
                                       QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService(QStringLiteral("org.freedesktop.Notifications")));
        m_systemd = new MockSystemd;
        QVERIFY(service.registerObject(QStringLiteral("/org/freedesktop/systemd1"), m_systemd,
                                       QDBusConnection::ExportAllSlots));
        QVERIFY(service.registerService(QStringLiteral("org.freedesktop.systemd1")));
    }

    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("service"));
        delete m_notifications;
        delete m_systemd;
        m_daemon.kill();
        m_daemon.waitForFinished();
    }

    void remoteActionEncoding()
    {
        // Same format as nemo-qml-plugin-notifications: space-separated
        // service, path, interface, method, then base64 QDataStream args.
        QCOMPARE(Notifier::encodeDBusCall("a.b", "/p", "a.b.i", "m", {}), QStringLiteral("a.b /p a.b.i m"));
        const QString encoded = Notifier::encodeDBusCall("a.b", "/p", "a.b.i", "m", {QStringLiteral("x")});
        const QStringList parts = encoded.split(QLatin1Char(' '));
        QCOMPARE(parts.size(), 5);
        QByteArray raw = QByteArray::fromBase64(parts.last().toLatin1());
        QDataStream stream(&raw, QIODevice::ReadOnly);
        QVariant decoded;
        stream >> decoded;
        QCOMPARE(decoded.toString(), QStringLiteral("x"));
    }

    void notifySendsSailfishHints()
    {
        Notifier notifier(client());
        NotificationRequest r;
        r.appName = QStringLiteral("Harpoon");
        r.summary = QStringLiteral("2 updates available");
        r.body = QStringLiteral("a, b");
        r.previewSummary = r.summary;
        r.itemCount = 2;
        r.category = QStringLiteral("x-nemo.software-update");
        r.remoteService = QStringLiteral("io.github.juhanilehtimaeki.harpoon");
        r.remotePath = QStringLiteral("/harpoon");
        r.remoteInterface = QStringLiteral("io.github.juhanilehtimaeki.harpoon");
        r.remoteMethod = QStringLiteral("showUpdates");
        const auto id = notifier.notify(r);
        QVERIFY2(id.ok(), qPrintable(id.error.message));
        QCOMPARE(m_notifications->calls.size(), 1);
        const auto &c = m_notifications->calls.first();
        QCOMPARE(c.summary, r.summary);
        QCOMPARE(c.actions, (QStringList{"default", ""}));
        QCOMPARE(c.hints.value(QStringLiteral("x-nemo-preview-summary")).toString(), r.summary);
        QCOMPARE(c.hints.value(QStringLiteral("x-nemo-item-count")).toInt(), 2);
        QCOMPARE(c.hints.value(QStringLiteral("category")).toString(), r.category);
        QCOMPARE(c.hints.value(QStringLiteral("x-nemo-remote-action-default")).toString(),
                 QStringLiteral("io.github.juhanilehtimaeki.harpoon /harpoon io.github.juhanilehtimaeki.harpoon showUpdates"));

        Notifier nowhere(client(), QStringLiteral("org.example.NoNotifications"));
        QCOMPARE(int(nowhere.notify(r).error.kind), int(Error::System));
    }

    void planNotifiesOncePerRelease()
    {
        QList<AppWithStatus> apps{
            {appWith(QStringLiteral("alpha"), QStringLiteral("2.0")), status(UpdateState::UpdateAvailable)},
            {appWith(QStringLiteral("beta"), QStringLiteral("1.0"), QStringLiteral("1.0")), status(UpdateState::UpdateAvailable)},
            {appWith(QStringLiteral("gamma"), QStringLiteral("3.0")), status(UpdateState::UpToDate)},
        };
        UpdateNotificationPlan plan = planUpdateNotification(apps);
        QVERIFY(plan.shouldNotify);
        QCOMPARE(plan.appsToMark.size(), 1);
        QCOMPARE(plan.appsToMark.first().name, QStringLiteral("alpha"));
        QCOMPARE(plan.appsToMark.first().notifiedVersion, QStringLiteral("2.0"));
        // The notification lists every pending update, not only new ones.
        QCOMPARE(plan.request.itemCount, 2);
        QCOMPARE(plan.request.body, QStringLiteral("alpha, beta"));
        QCOMPARE(plan.request.remoteMethod, QStringLiteral("showUpdates"));

        // Once marked, the same release does not notify again...
        apps[0].app = plan.appsToMark.first();
        QVERIFY(!planUpdateNotification(apps).shouldNotify);
        // ...but a newer one does.
        apps[0].app.latestVersion = QStringLiteral("2.1");
        QVERIFY(planUpdateNotification(apps).shouldNotify);
        // Nothing pending: nothing to say.
        QVERIFY(!planUpdateNotification({{appWith(QStringLiteral("x"), QStringLiteral("1")), status(UpdateState::UpToDate)}})
                     .shouldNotify);
    }

    void schedulerEnable()
    {
        QTemporaryDir config;
        BackgroundScheduler scheduler(client(), config.path());
        m_systemd->calls.clear();
        const Error e = waitFor([&](std::function<void(const Error &)> d) { scheduler.apply(true, 12, d); });
        QVERIFY2(e.ok(), qPrintable(e.message));
        QCOMPARE(m_systemd->calls, (QStringList{"Reload", "EnableUnitFiles harpoon-check.timer false true",
                                                "RestartUnit harpoon-check.timer replace"}));
        QFile dropIn(scheduler.dropInPath());
        QVERIFY(dropIn.open(QIODevice::ReadOnly));
        const QByteArray content = dropIn.readAll();
        QVERIFY(content.contains("OnUnitActiveSec=\nOnUnitActiveSec=12h\n"));
        QVERIFY(scheduler.dropInPath().startsWith(config.path() + QStringLiteral("/systemd/user/harpoon-check.timer.d/")));
    }

    void schedulerDisable()
    {
        QTemporaryDir config;
        BackgroundScheduler scheduler(client(), config.path());
        m_systemd->calls.clear();
        QVERIFY(waitFor([&](std::function<void(const Error &)> d) { scheduler.apply(false, 6, d); }).ok());
        QCOMPARE(m_systemd->calls, (QStringList{"Reload", "StopUnit harpoon-check.timer replace",
                                                "DisableUnitFiles harpoon-check.timer false"}));
    }

    void schedulerReportsFailures()
    {
        QTemporaryDir config;
        BackgroundScheduler scheduler(client(), config.path());
        m_systemd->failMethod = QStringLiteral("EnableUnitFiles");
        const Error e = waitFor([&](std::function<void(const Error &)> d) { scheduler.apply(true, 6, d); });
        m_systemd->failMethod.clear();
        QCOMPARE(int(e.kind), int(Error::System));
        QVERIFY(e.message.contains(QLatin1String("EnableUnitFiles")));
        QVERIFY(!m_systemd->calls.last().startsWith(QLatin1String("RestartUnit")));
    }

    void schedulerWithoutBusFailsFast()
    {
        QTemporaryDir config;
        BackgroundScheduler scheduler(QDBusConnection(QStringLiteral("none")), config.path());
        const Error e = waitFor([&](std::function<void(const Error &)> d) { scheduler.apply(true, 6, d); });
        QCOMPARE(int(e.kind), int(Error::System));
        QVERIFY(e.message.contains(QLatin1String("D-Bus")));
    }

    void intervalIsClamped()
    {
        QVERIFY(BackgroundScheduler::dropInContent(0).contains("=1h"));
        QVERIFY(BackgroundScheduler::dropInContent(10000).contains("=168h"));
    }

    void backupRoundTrip()
    {
        App a = appWith(QStringLiteral("alpha"), QStringLiteral("2.0"), QStringLiteral("2.0"));
        a.id = QStringLiteral("harbour-alpha");
        a.temporaryId = false;
        a.settings.set(Keys::includePrereleases, true);
        a.receipt.evr = QStringLiteral("2.0-1");
        a.lastError = QStringLiteral("old error");
        Backup backup;
        backup.apps << a << appWith(QStringLiteral("beta"), QStringLiteral("1.0"));
        backup.settings = {{QStringLiteral("installBackend"), QStringLiteral("handler")}};

        const auto parsed = Backup::fromJson(backup.toJson());
        QVERIFY2(parsed.ok(), qPrintable(parsed.error.message));
        QCOMPARE(parsed.value.apps.size(), 2);
        QCOMPARE(parsed.value.apps.first().toJson(), a.toJson());
        QCOMPARE(parsed.value.settings.value(QStringLiteral("installBackend")).toString(), QStringLiteral("handler"));

        // Merge: beta is already tracked (same URL, different case); alpha is new.
        App existingBeta = appWith(QStringLiteral("beta"), QStringLiteral("0.9"));
        existingBeta.url = existingBeta.url.toUpper().replace(QLatin1String("HTTPS"), QLatin1String("https"));
        const ImportResult merged = mergeBackupApps({existingBeta}, parsed.value.apps);
        QCOMPARE(merged.added.size(), 1);
        QCOMPARE(merged.skipped, QStringList{"beta"});
        const App &imported = merged.added.first();
        QVERIFY(!imported.receipt.isValid());       // device-specific state dropped
        QVERIFY(imported.notifiedVersion.isEmpty());
        QVERIFY(imported.lastError.isEmpty());
        QVERIFY(imported.settings.getBool(Keys::includePrereleases));
    }

    void backupRejectsOtherFiles()
    {
        QCOMPARE(int(Backup::fromJson("[]").error.kind), int(Error::Storage));
        QCOMPARE(int(Backup::fromJson("{\"apps\": []}").error.kind), int(Error::Storage));
        QVERIFY(Backup::fromJson("{\"format\":\"harpoon-backup\",\"schemaVersion\":9}").error.message.contains(
            QLatin1String("newer")));
    }
};

QTEST_GUILESS_MAIN(TestBackground)
#include "tst_background.moc"
