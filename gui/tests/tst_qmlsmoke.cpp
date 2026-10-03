// Loads the real QML UI offscreen against minimal stand-ins for
// Sailfish.Silica and Nemo.Notifications (silica-stub/), with a controller
// seeded with apps in every state. Any QML error or warning fails the test.
//
// This catches mistakes in Harpoon's own QML and C++ bindings: undefined
// names, wrong property types, broken JavaScript, missing files. It cannot
// prove that the real Silica components have the properties we use; that
// needs the SailfishOS SDK or a device.

#include "faketransport.h"
#include "fakerpmdb.h"

#include "applistmodel.h"
#include "harpooncontroller.h"
#include "harpoondbus.h"
#include "qrdecoder.h"
#include "qrimageprovider.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QQuickItemGrabResult>
#include <QQuickView>
#include <QTemporaryDir>
#include <QtQml>
#include <QtTest>

using namespace Harpoon;

// ---- EnterKey attached property (C++ in real Silica) ----
class EnterKeyAttached : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled MEMBER m_enabled NOTIFY changed)
    Q_PROPERTY(QString iconSource MEMBER m_iconSource NOTIFY changed)
public:
    explicit EnterKeyAttached(QObject *parent) : QObject(parent) {}
signals:
    void changed();
    void clicked();
private:
    bool m_enabled = true;
    QString m_iconSource;
};

class EnterKey : public QObject
{
    Q_OBJECT
public:
    static EnterKeyAttached *qmlAttachedProperties(QObject *object) { return new EnterKeyAttached(object); }
};
QML_DECLARE_TYPEINFO(EnterKey, QML_HAS_ATTACHED_PROPERTIES)

namespace {

// image://theme/... icons come from Silica on a device.
class ThemeImageProvider : public QQuickImageProvider
{
public:
    ThemeImageProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(Qt::gray);
        if (size)
            *size = image.size();
        return image;
    }
};

QStringList g_messages;

void collect(QtMsgType type, const QMessageLogContext &, const QString &message)
{
    if (type == QtDebugMsg || type == QtInfoMsg)
        return;
    // Qt 5.15 wants "function onFoo()" in Connections; Qt 5.6 on the phone
    // only understands the "onFoo:" form, so this warning is expected.
    if (message.contains(QLatin1String("Implicitly defined onFoo properties in Connections are deprecated")))
        return;
    // Installed icon paths only exist on a device.
    if (message.contains(QLatin1String("/usr/share/icons/hicolor/")))
        return;
    g_messages << message;
}

const QString kStubDir = QStringLiteral(HARPOON_STUB_DIR);
const QString kQmlDir = QStringLiteral(HARPOON_QML_DIR);
const QString kFixtureDir = QStringLiteral(HARPOON_GUI_FIXTURE_DIR);

void registerStubs()
{
    const char *silica = "Sailfish.Silica";
    const QStringList types{"ApplicationWindow", "Page", "Dialog", "DialogHeader", "PageHeader", "SilicaListView",
                            "SilicaFlickable", "PullDownMenu", "PushUpMenu", "MenuItem", "ContextMenu", "ListItem", "SectionHeader",
                            "ViewPlaceholder", "VerticalScrollDecorator", "Label", "InfoLabel", "LinkedLabel",
                            "BusyIndicator", "TextField", "PasswordField", "ComboBox", "TextSwitch", "ProgressBar",
                            "Button", "DetailItem", "CoverBackground", "CoverPlaceholder", "CoverActionList",
                            "CoverAction", "Orientation", "TruncationMode", "BusyIndicatorSize", "PageStatus",
                            "PageStackAction", "Formatter"};
    for (const QString &t : types)
        qmlRegisterType(QUrl::fromLocalFile(kStubDir + QLatin1Char('/') + t + QStringLiteral(".qml")), silica, 1, 0,
                        qPrintable(t));
    const QStringList singletons{"Theme", "Format", "Remorse", "Clipboard"};
    for (const QString &s : singletons)
        qmlRegisterSingletonType(QUrl::fromLocalFile(kStubDir + QLatin1Char('/') + s + QStringLiteral(".qml")), silica,
                                 1, 0, qPrintable(s));
    qmlRegisterUncreatableType<EnterKey>(silica, 1, 0, "EnterKey", QStringLiteral("attached only"));
    qmlRegisterType(QUrl::fromLocalFile(kStubDir + QStringLiteral("/Notification.qml")), "Nemo.Notifications", 1, 0,
                    "Notification");
    for (const char *picker : {"FilePickerPage", "ImagePickerPage"})
        qmlRegisterType(QUrl::fromLocalFile(kStubDir + QLatin1Char('/') + QLatin1String(picker) + QStringLiteral(".qml")),
                        "Sailfish.Pickers", 1, 0, picker);
    for (const char *media : {"Camera", "VideoOutput"})
        qmlRegisterType(QUrl::fromLocalFile(kStubDir + QLatin1Char('/') + QLatin1String(media) + QStringLiteral(".qml")),
                        "QtMultimedia", 5, 6, media);

    // Same registrations as gui/src/harpoon.cpp.
    const char *uri = "harbour.harpoon";
    qmlRegisterUncreatableType<AppListModel>(uri, 1, 0, "AppListModel", QStringLiteral("model"));
    qmlRegisterUncreatableType<HarpoonSettings>(uri, 1, 0, "HarpoonSettings", QStringLiteral("settings"));
    qmlRegisterUncreatableType<HarpoonController>(uri, 1, 0, "HarpoonController", QStringLiteral("controller"));
}

App seededApp(const QString &id, const QString &latest, bool temporary = false)
{
    App a = App::fromUrl(QStringLiteral("https://github.com/someone/") + id);
    if (!temporary) {
        a.id = id;
        a.temporaryId = false;
    }
    a.latestVersion = latest;
    a.latestTag = QStringLiteral("v") + latest;
    a.latestTitle = QStringLiteral("Release ") + latest;
    a.latestDate = QDateTime::fromString(QStringLiteral("2026-09-01T10:00:00Z"), Qt::ISODate);
    a.changelog = QStringLiteral("- Fixed things\n- Added things");
    a.releasePageUrl = QStringLiteral("https://github.com/someone/") + id + QStringLiteral("/releases/tag/v") + latest;
    a.lastCheck = QDateTime::currentDateTimeUtc();
    Asset asset;
    asset.name = id + QStringLiteral("-") + latest + QStringLiteral("-1.aarch64.rpm");
    asset.url = QStringLiteral("https://example.invalid/") + asset.name;
    asset.size = 1234;
    a.latestAssets << asset;
    return a;
}

RpmInfo rpm(const QString &name, const QString &evr)
{
    RpmInfo i;
    i.name = name;
    i.evr = parseEvr(evr);
    i.arch = QStringLiteral("aarch64");
    i.vendor = QStringLiteral("chum");
    return i;
}

} // namespace

class TestQmlSmoke : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    FakeTransport m_transport;
    FakeRpmDb m_db;
    std::unique_ptr<HarpoonSettings> m_settings;
    std::unique_ptr<BackgroundScheduler> m_scheduler;
    std::unique_ptr<HarpoonController> m_controller;
    HarpoonDBus m_dbus;
    QrDecoder m_qrDecoder;
    std::unique_ptr<QQuickView> m_view;

    QVariant eval(const QString &js)
    {
        QQmlExpression expr(m_view->engine()->rootContext(), m_view->rootObject(), js);
        const QVariant v = expr.evaluate();
        if (expr.hasError())
            g_messages << expr.error().toString();
        return v;
    }

    void settle()
    {
        for (int i = 0; i < 10; ++i)
            QTest::qWait(20);
    }

    void expectClean(const char *step)
    {
        settle();
        if (!g_messages.isEmpty()) {
            const QString all = g_messages.join(QLatin1Char('\n'));
            g_messages.clear();
            QFAIL(qPrintable(QStringLiteral("%1:\n%2").arg(QLatin1String(step), all)));
        }
    }

    QObject *currentPage() { return eval(QStringLiteral("pageStack.currentPage")).value<QObject *>(); }

    // pageStack.push() of a page file, with optional JS properties object.
    QObject *push(const QString &page, const QString &props = QStringLiteral("{}"))
    {
        const QString url = QUrl::fromLocalFile(kQmlDir + QStringLiteral("/pages/") + page).toString();
        return eval(QStringLiteral("pageStack.push('%1', %2)").arg(url, props)).value<QObject *>();
    }

    void popToList()
    {
        eval(QStringLiteral("pageStack.pop(pageStack.find(function(p) { return p.objectName === 'appListPage' }))"));
    }

private slots:
    void initTestCase()
    {
        registerStubs();
        AppStore store(m_dir.filePath(QStringLiteral("apps")));

        App alpha = seededApp(QStringLiteral("harbour-alpha"), QStringLiteral("1.1"));
        m_db.installed.insert(QStringLiteral("harbour-alpha"), rpm(QStringLiteral("harbour-alpha"), QStringLiteral("1.0-1")));
        QVERIFY(store.save(alpha).ok());

        App beta = seededApp(QStringLiteral("beta"), QStringLiteral("2.0"));
        beta.settings.set(Keys::trackOnly, true);
        beta.latestAssets.clear();
        QVERIFY(store.save(beta).ok());

        App gamma = seededApp(QStringLiteral("gamma"), QString(), true);
        gamma.latestVersion.clear();
        gamma.latestDate = QDateTime();
        gamma.latestAssets.clear();
        gamma.changelog.clear();
        gamma.lastError = QStringLiteral("Repository not found");
        QVERIFY(store.save(gamma).ok());

        App delta = seededApp(QStringLiteral("harbour-delta"), QStringLiteral("3.0"));
        delta.receipt.evr = QStringLiteral("3.0-1");
        delta.receipt.version = QStringLiteral("3.0");
        delta.receipt.installedAt = QDateTime::currentDateTimeUtc();
        m_db.installed.insert(QStringLiteral("harbour-delta"), rpm(QStringLiteral("harbour-delta"), QStringLiteral("3.0-1")));
        QVERIFY(store.save(delta).ok());

        App web = seededApp(QStringLiteral("webapp"), QStringLiteral("5.0"));
        web.url = QStringLiteral("https://downloads.example.org/webapp/");
        web.settings.set(Keys::customLinkFilterRegex, QStringLiteral("webapp-.*\\.rpm"));
        QVERIFY(store.save(web).ok());

        App repo = seededApp(QStringLiteral("repoapp"), QStringLiteral("1.2-3"));
        repo.url = QStringLiteral("https://repo.example.org/obs/sailfishos_5.0_aarch64");
        repo.sourceId = QStringLiteral("RpmMdRepo");
        repo.settings.set(Keys::packageName, QStringLiteral("repoapp"));
        QVERIFY(store.save(repo).ok());

        m_settings.reset(new HarpoonSettings(m_dir.filePath(QStringLiteral("config/harpoon.conf"))));
        m_scheduler.reset(new BackgroundScheduler(QDBusConnection(QStringLiteral("none")),
                                                  m_dir.filePath(QStringLiteral("xdg-config"))));
        ControllerEnvironment env;
        env.dataDir = store.directory();
        env.cacheDir = m_dir.filePath(QStringLiteral("cache"));
        env.device.arch = QStringLiteral("aarch64");
        env.device.osVersion = QStringLiteral("5.0.0.62");
        env.transport = &m_transport;
        env.runner = &m_db;
        env.settings = m_settings.get();
        env.scheduler = m_scheduler.get();
        env.backupDir = m_dir.filePath(QStringLiteral("documents"));
        m_controller.reset(new HarpoonController(env));
        m_controller->reload();
        QCOMPARE(m_controller->apps()->count(), 6);
        QCOMPARE(m_controller->apps()->updatesCount(), 2);

        qInstallMessageHandler(collect);
        m_view.reset(new QQuickView);
        m_view->rootContext()->setContextProperty(QStringLiteral("harpoon"), m_controller.get());
        m_view->rootContext()->setContextProperty(QStringLiteral("harpoonDBus"), &m_dbus);
        m_view->rootContext()->setContextProperty(QStringLiteral("qrDecoder"), &m_qrDecoder);
        m_view->engine()->addImageProvider(QStringLiteral("harpoonqr"), new QrImageProvider);
        m_view->engine()->addImageProvider(QStringLiteral("theme"), new ThemeImageProvider);
        m_view->setSource(QUrl::fromLocalFile(kQmlDir + QStringLiteral("/harpoon.qml")));
        QVERIFY2(m_view->status() == QQuickView::Ready, qPrintable(g_messages.join(QLatin1Char('\n'))));
        // Shown and active, so the scan page's camera and grabToImage() run.
        m_view->show();
        m_view->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
    }

    void cleanupTestCase()
    {
        m_view.reset();
        qInstallMessageHandler(nullptr);
    }

    void mainWindowAndCover()
    {
        expectClean("loading harpoon.qml");
        QCOMPARE(eval(QStringLiteral("pageStack.currentPage.objectName")).toString(), QStringLiteral("appListPage"));
        QVERIFY(eval(QStringLiteral("coverItem !== null")).toBool());
        // As harpoon.cpp does after loading: refresh stale apps (gamma was
        // never checked) while the list is showing.
        m_controller->checkStale(60);
        QTRY_VERIFY(!m_controller->checking());
        expectClean("after the startup check");
    }

    void appPages_data()
    {
        QTest::addColumn<QString>("appId");
        for (const char *id : {"harbour-alpha", "beta", "harbour-delta", "webapp", "repoapp"})
            QTest::newRow(id) << QString::fromLatin1(id);
        for (const AppListModel::Entry &e : m_controller->apps()->entries())
            if (e.app.name == QLatin1String("gamma"))
                QTest::newRow("gamma (temporary id)") << e.app.id;
    }

    void appPages()
    {
        QFETCH(QString, appId);
        QVERIFY(m_controller->apps()->indexOf(appId) >= 0);
        push(QStringLiteral("AppPage.qml"), QStringLiteral("{ appId: '%1' }").arg(appId));
        expectClean("AppPage");
        QCOMPARE(currentPage()->property("details").toMap().value(QStringLiteral("appId")).toString(), appId);

        QObject *settingsPage = push(QStringLiteral("AppSettingsPage.qml"), QStringLiteral("{ appId: '%1' }").arg(appId));
        expectClean("AppSettingsPage");
        QCOMPARE(settingsPage->property("_isWebPage").toBool(), appId == QLatin1String("webapp"));
        QCOMPARE(settingsPage->property("_isRepo").toBool(), appId == QLatin1String("repoapp"));
        settingsPage->setProperty("_advanced", true);
        expectClean("advanced settings");
        popToList();
        expectClean("popping back");
    }

    void busyStateUpdatesPages()
    {
        push(QStringLiteral("AppPage.qml"), QStringLiteral("{ appId: 'harbour-alpha' }"));
        m_controller->apps()->setBusy(QStringLiteral("harbour-alpha"), true, QStringLiteral("Downloading"), 0.5);
        expectClean("busy with progress");
        QVERIFY(currentPage()->property("details").toMap().value(QStringLiteral("busy")).toBool());
        m_controller->apps()->setBusy(QStringLiteral("harbour-alpha"), true, QStringLiteral("Installing"), -1);
        expectClean("busy indeterminate");
        m_controller->apps()->setBusy(QStringLiteral("harbour-alpha"), false);
        expectClean("idle again");
        popToList();
    }

    void perAppSettingChangesFlowBack()
    {
        push(QStringLiteral("AppSettingsPage.qml"), QStringLiteral("{ appId: 'harbour-alpha' }"));
        m_controller->setAppSetting(QStringLiteral("harbour-alpha"), QStringLiteral("includePrereleases"), true);
        m_controller->setAppSetting(QStringLiteral("harbour-alpha"), QStringLiteral("sortMethodChoice"), QStringLiteral("name"));
        expectClean("settings changed");
        // The signer rules appear once provenance is checked.
        m_controller->setAppSetting(QStringLiteral("harbour-alpha"), QStringLiteral("githubBuildVerificationMode"),
                                    QStringLiteral("audit"));
        m_controller->setAppSetting(QStringLiteral("harbour-alpha"), QStringLiteral("attestationRefRegEx"),
                                    QStringLiteral("refs/tags/.*"));
        expectClean("signer rules");
        QVERIFY(currentPage()->property("_changed").toBool());
        popToList();
        expectClean("leaving settings");
        // An advanced setting in use shows the advanced settings right away.
        QObject *again = push(QStringLiteral("AppSettingsPage.qml"), QStringLiteral("{ appId: 'harbour-alpha' }"));
        QVERIFY(again->property("_advanced").toBool());
        popToList();
    }

    void addDialog()
    {
        QObject *dialog = push(QStringLiteral("AddAppDialog.qml"));
        expectClean("AddAppDialog");
        QVERIFY(dialog);
        QVERIFY(!dialog->property("canAccept").toBool());
        QObject *field = nullptr;
        for (QObject *o : dialog->findChildren<QObject *>())
            if (o->property("label").toString() == QLatin1String("Repository URL"))
                field = o;
        QVERIFY(field);
        field->setProperty("text", QStringLiteral("https://codeberg.org/someone/thing"));
        expectClean("typing a URL");
        QVERIFY(dialog->property("canAccept").toBool());
        field->setProperty("text", QStringLiteral("https://github.com/someone/harbour-alpha"));
        expectClean("typing a duplicate URL");
        QVERIFY(!dialog->property("canAccept").toBool());

        // An RPM repository needs a package name before it can be added.
        QObject *sourceBox = nullptr;
        QObject *packageField = nullptr;
        for (QObject *o : dialog->findChildren<QObject *>()) {
            if (o->property("label").toString() == QLatin1String("Source type"))
                sourceBox = o;
            if (o->property("label").toString() == QLatin1String("Package name in the repository"))
                packageField = o;
        }
        QVERIFY(sourceBox && packageField);
        const QStringList ids = [this]() {
            QStringList out;
            for (const QVariant &v : m_controller->sources())
                out << v.toMap().value(QStringLiteral("id")).toString();
            return out;
        }();
        sourceBox->setProperty("currentIndex", ids.indexOf(QStringLiteral("RpmMdRepo")) + 1);
        field->setProperty("text", QStringLiteral("https://repo.example.org/other/"));
        expectClean("choosing an RPM repository");
        QVERIFY(dialog->property("_needsPackageName").toBool());
        QVERIFY(!dialog->property("canAccept").toBool());
        packageField->setProperty("text", QStringLiteral("harbour-thing"));
        expectClean("typing a package name");
        QVERIFY(dialog->property("canAccept").toBool());
        popToList();
    }

    QObject *findByProperty(QObject *root, const char *name, const QString &value)
    {
        for (QObject *o : root->findChildren<QObject *>())
            if (o->property(name).toString() == value)
                return o;
        return nullptr;
    }

    // The source id selected in the add dialog's "Source type" box.
    QString chosenSource(QObject *dialog)
    {
        QObject *box = findByProperty(dialog, "label", QStringLiteral("Source type"));
        const int index = box ? box->property("currentIndex").toInt() : -1;
        if (index <= 0)
            return QString();
        return m_controller->sources().value(index - 1).toMap().value(QStringLiteral("id")).toString();
    }

    void scanQrCodeIntoAddDialog()
    {
        QObject *dialog = push(QStringLiteral("AddAppDialog.qml"));
        expectClean("AddAppDialog");
        QObject *scanButton = findByProperty(dialog, "text", QStringLiteral("Scan QR code"));
        QVERIFY(scanButton);
        QVERIFY(QMetaObject::invokeMethod(scanButton, "clicked"));
        expectClean("opening the scanner");
        QObject *scanPage = currentPage();
        QCOMPARE(scanPage->objectName(), QStringLiteral("scanPage"));
        QVERIFY(scanPage->property("_scanning").toBool());

        QObject *viewfinder = nullptr;
        for (QObject *o : scanPage->findChildren<QObject *>())
            if (o->property("testImage").isValid())
                viewfinder = o;
        QVERIFY(viewfinder);

        // Not an app link: the scanner says so and keeps looking.
        viewfinder->setProperty("testImage", QUrl::fromLocalFile(kFixtureDir + QStringLiteral("/qr/javascript.png")));
        QTRY_VERIFY_WITH_TIMEOUT(!scanPage->property("_message").toString().isEmpty(), 10000);
        QCOMPARE(currentPage(), scanPage);
        expectClean("an unusable QR code");

        // A harpoon:// link from the viewfinder fills in the dialog.
        viewfinder->setProperty("testImage", QUrl::fromLocalFile(kFixtureDir + QStringLiteral("/qr/harpoon-link.png")));
        QTRY_COMPARE_WITH_TIMEOUT(currentPage(), dialog, 10000);
        expectClean("a scanned link");
        QObject *urlField = findByProperty(dialog, "label", QStringLiteral("Repository URL"));
        QCOMPARE(urlField->property("text").toString(), QStringLiteral("https://git.example.org/me/harbour-tides"));
        QCOMPARE(chosenSource(dialog), QStringLiteral("Forgejo"));
        QVERIFY(dialog->property("canAccept").toBool());

        // Reading a code from a saved image goes through the image picker.
        QVERIFY(QMetaObject::invokeMethod(findByProperty(dialog, "text", QStringLiteral("Scan QR code")), "clicked"));
        scanPage = currentPage();
        QVERIFY(QMetaObject::invokeMethod(findByProperty(scanPage, "text", QStringLiteral("Read from image")), "clicked"));
        expectClean("opening the image picker");
        QObject *picker = currentPage();
        QVERIFY(picker && picker != scanPage);
        picker->setProperty("selectedContentProperties",
                            QVariantMap{{QStringLiteral("filePath"), kFixtureDir + QStringLiteral("/qr/no-code.png")}});
        expectClean("an image without a code");
        QCOMPARE(scanPage->property("_message").toString(), QStringLiteral("No QR code found in the image"));
        picker->setProperty("selectedContentProperties",
                            QVariantMap{{QStringLiteral("filePath"), kFixtureDir + QStringLiteral("/qr/repo-url.png")}});
        expectClean("an image with a code");
        QCOMPARE(currentPage(), dialog);
        QCOMPARE(urlField->property("text").toString(),
                 QStringLiteral("https://github.com/sailfishos-chum/sailfishos-chum-gui"));
        QCOMPARE(chosenSource(dialog), QString());
        popToList();
        expectClean("leaving the dialog");
    }

    void shareAsQrCode()
    {
        // A GitHub app needs nothing but its URL.
        QObject *page = push(QStringLiteral("ShareQrPage.qml"), QStringLiteral("{ appId: 'harbour-alpha' }"));
        expectClean("ShareQrPage");
        QCOMPARE(page->property("link").toString(), QStringLiteral("https://github.com/someone/harbour-alpha"));
        QObject *image = nullptr;
        for (QObject *o : page->findChildren<QObject *>())
            if (o->property("sourceSize").isValid() && o->property("source").toString().startsWith(QLatin1String("image://harpoonqr/")))
                image = o;
        QVERIFY(image);
        QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready

        // The code on screen reads back as the link.
        QQuickItem *item = qobject_cast<QQuickItem *>(image);
        QSharedPointer<QQuickItemGrabResult> grab = item->grabToImage();
        QVERIFY(grab);
        QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
        QVERIFY(ready.wait(5000));
        QCOMPARE(QrDecoder::decode(grab->image()), QStringLiteral("https://github.com/someone/harbour-alpha"));

        QVERIFY(QMetaObject::invokeMethod(findByProperty(page, "text", QStringLiteral("Copy link")), "clicked"));
        expectClean("copying the link");
        QQmlExpression clipboard(qmlContext(page), page, QStringLiteral("Clipboard.text"));
        QCOMPARE(clipboard.evaluate().toString(), QStringLiteral("https://github.com/someone/harbour-alpha"));
        popToList();

        // An RPM repository needs the source and package in a harpoon:// link,
        // which reads back into the same app.
        page = push(QStringLiteral("ShareQrPage.qml"), QStringLiteral("{ appId: 'repoapp' }"));
        expectClean("ShareQrPage for a repository");
        const QString link = page->property("link").toString();
        QVERIFY(link.startsWith(QLatin1String("harpoon://add?")));
        const QVariantMap parsed = m_controller->parseAddLink(link);
        QCOMPARE(parsed.value(QStringLiteral("url")).toString(), QStringLiteral("https://repo.example.org/obs/sailfishos_5.0_aarch64"));
        QCOMPARE(parsed.value(QStringLiteral("sourceId")).toString(), QStringLiteral("RpmMdRepo"));
        QCOMPARE(parsed.value(QStringLiteral("packageName")).toString(), QStringLiteral("repoapp"));
        popToList();
        QVERIFY(m_controller->shareLink(QStringLiteral("no-such-app")).isEmpty());
    }

    void addLinkOverDBus()
    {
        m_dbus.openUrl({QStringLiteral("harpoon://add?url=https%3A%2F%2Frepo.example.org%2Fobs%2Fsfos&source=RpmMdRepo"
                                       "&package=harbour-tides")});
        expectClean("openUrl over D-Bus");
        QObject *dialog = currentPage();
        QCOMPARE(dialog->property("initialUrl").toString(), QStringLiteral("https://repo.example.org/obs/sfos"));
        QCOMPARE(chosenSource(dialog), QStringLiteral("RpmMdRepo"));
        QCOMPARE(findByProperty(dialog, "label", QStringLiteral("Package name in the repository"))->property("text").toString(),
                 QStringLiteral("harbour-tides"));
        QVERIFY(dialog->property("canAccept").toBool());

        // A bad link shows a banner and leaves the pages alone.
        QObject *banner = nullptr;
        for (QObject *o : m_view->rootObject()->findChildren<QObject *>(QString(), Qt::FindDirectChildrenOnly))
            if (o->property("publishCount").isValid())
                banner = o;
        QVERIFY(banner);
        const int published = banner->property("publishCount").toInt();
        m_dbus.openUrl({QStringLiteral("harpoon://add?url=javascript%3Aalert(1)")});
        expectClean("a bad link over D-Bus");
        QCOMPARE(banner->property("publishCount").toInt(), published + 1);
        QVERIFY(banner->property("previewSummary").toString().startsWith(QLatin1String("Cannot add app")));
        QCOMPARE(currentPage(), dialog);
        popToList();
    }

    void settingsAndAbout()
    {
        // Token fields only for sources that use tokens (all seeded apps are
        // on their sources' default hosts).
        QStringList keys;
        for (const QVariant &t : m_controller->tokenTargets())
            keys << t.toMap().value(QStringLiteral("key")).toString();
        keys.sort();
        QCOMPARE(keys, (QStringList{QStringLiteral("Forgejo"), QStringLiteral("GitHub"), QStringLiteral("GitLab")}));

        push(QStringLiteral("SettingsPage.qml"));
        expectClean("SettingsPage");
        m_settings->setInstallBackend(QStringLiteral("handler"));
        m_settings->setBackgroundChecks(false);
        expectClean("settings changed");
        push(QStringLiteral("AboutPage.qml"));
        // The icon path only exists on a device.
        settle();
        g_messages.erase(std::remove_if(g_messages.begin(), g_messages.end(),
                                        [](const QString &m) { return m.contains(QLatin1String("harpoon.png")); }),
                         g_messages.end());
        expectClean("AboutPage");
        popToList();
    }

    void backupFromSettingsPage()
    {
        QObject *settingsPage = push(QStringLiteral("SettingsPage.qml"));
        QVERIFY(settingsPage);
        expectClean("SettingsPage");
        QObject *banner = nullptr;
        QObject *importItem = nullptr;
        QObject *exportItem = nullptr;
        for (QObject *o : settingsPage->findChildren<QObject *>()) {
            if (o->property("publishCount").isValid())
                banner = o;
            if (o->property("text").toString() == QLatin1String("Import backup"))
                importItem = o;
            if (o->property("text").toString() == QLatin1String("Export backup"))
                exportItem = o;
        }
        QVERIFY(banner && importItem && exportItem);

        QVERIFY(QMetaObject::invokeMethod(exportItem, "clicked"));
        expectClean("Export backup");
        QCOMPARE(banner->property("publishCount").toInt(), 1);
        QVERIFY(banner->property("previewSummary").toString().startsWith(QLatin1String("Saved ")));
        const QString exported = banner->property("previewSummary").toString().mid(6);
        QVERIFY(QFile::exists(exported));

        QVERIFY(QMetaObject::invokeMethod(importItem, "clicked"));
        expectClean("opening the file picker");
        QObject *picker = currentPage();
        QVERIFY(picker && picker->property("nameFilters").isValid());
        picker->setProperty("selectedContentProperties", QVariantMap{{QStringLiteral("filePath"), exported}});
        expectClean("picking a backup");
        QCOMPARE(banner->property("publishCount").toInt(), 2);
        // Every app in the backup is already tracked.
        QVERIFY(banner->property("previewSummary").toString().contains(QLatin1String("already tracked")));

        emit m_controller->backgroundError(QStringLiteral("systemd is not reachable"));
        expectClean("background error");
        popToList();
    }

    void idChangeReachesEveryPage()
    {
        QString gammaId;
        for (const AppListModel::Entry &e : m_controller->apps()->entries())
            if (e.app.name == QLatin1String("gamma"))
                gammaId = e.app.id;
        QObject *appPage = push(QStringLiteral("AppPage.qml"), QStringLiteral("{ appId: '%1' }").arg(gammaId));
        QObject *settingsPage = push(QStringLiteral("AppSettingsPage.qml"), QStringLiteral("{ appId: '%1' }").arg(gammaId));
        expectClean("pages for a temporary id");
        emit m_controller->appIdChanged(gammaId, QStringLiteral("harbour-gamma"));
        expectClean("id change");
        QCOMPARE(appPage->property("appId").toString(), QStringLiteral("harbour-gamma"));
        QCOMPARE(settingsPage->property("appId").toString(), QStringLiteral("harbour-gamma"));
        popToList();
    }

    void dbusOpensApp()
    {
        m_dbus.showApp(QStringLiteral("harbour-delta"));
        expectClean("showApp over D-Bus");
        QCOMPARE(currentPage()->property("appId").toString(), QStringLiteral("harbour-delta"));
        m_dbus.showUpdates();
        expectClean("showUpdates over D-Bus");
        QCOMPARE(currentPage()->objectName(), QStringLiteral("appListPage"));
    }

    void removingAnAppWhileListed()
    {
        m_controller->removeApp(QStringLiteral("beta"));
        expectClean("removing an app");
        QCOMPARE(m_controller->apps()->count(), 5);
    }
};

QTEST_MAIN(TestQmlSmoke)
#include "tst_qmlsmoke.moc"
