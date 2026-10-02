#include "rpmfactory.h"
#include "pkg/rpminspector.h"

#include <QtTest>

using namespace Harpoon;

class TestRpmInspector : public QObject
{
    Q_OBJECT
private slots:
    void parseOutput()
    {
        const QByteArray out = "package foo is not installed\n"
                               "@@harbour-x\t(none)\t1.2\t3.sfos\taarch64\t(none)\tA summary\twith tab\n"
                               "@@harbour-x\t2\t1.3\t1\taarch64\tmeego\tNewer\n";
        const QList<RpmInfo> infos = RpmInspector::parse(out);
        QCOMPARE(infos.size(), 2);
        QCOMPARE(infos.at(0).name, QStringLiteral("harbour-x"));
        QCOMPARE(infos.at(0).evr.epoch, 0);
        QCOMPARE(infos.at(0).evr.toString(), QStringLiteral("1.2-3.sfos"));
        QCOMPARE(infos.at(0).vendor, QString());
        QCOMPARE(infos.at(0).summary, QStringLiteral("A summary\twith tab"));
        QCOMPARE(infos.at(1).evr.toString(), QStringLiteral("2:1.3-1"));
        QCOMPARE(infos.at(1).vendor, QStringLiteral("meego"));
        QCOMPARE(infos.at(1).nevra(), QStringLiteral("harbour-x-2:1.3-1.aarch64"));
    }

    void realPackageFile()
    {
        if (!RpmFactory::available())
            QSKIP("rpmbuild not installed");
        RpmFactory factory;
        const QString path = factory.build(QStringLiteral("harbour-demo"), QStringLiteral("1.4.2"),
                                           QStringLiteral("3"), QStringLiteral("aarch64"),
                                           QStringLiteral("meego"), 1);
        QVERIFY(!path.isEmpty());
        SystemProcessRunner runner;
        RpmInspector inspector(runner);
        const auto info = inspector.inspectFile(path);
        QVERIFY2(info.ok(), qPrintable(info.error.message));
        QCOMPARE(info.value.name, QStringLiteral("harbour-demo"));
        QCOMPARE(info.value.evr.epoch, 1);
        QCOMPARE(info.value.evr.version, QStringLiteral("1.4.2"));
        QCOMPARE(info.value.evr.release, QStringLiteral("3"));
        QCOMPARE(info.value.arch, QStringLiteral("aarch64"));
        QCOMPARE(info.value.vendor, QStringLiteral("meego"));
        QCOMPARE(info.value.summary, QStringLiteral("Test package"));
    }

    void notAnRpm()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty())
            QSKIP("rpm not installed");
        QTemporaryFile f;
        QVERIFY(f.open());
        f.write("this is not an rpm");
        f.flush();
        SystemProcessRunner runner;
        RpmInspector inspector(runner);
        QCOMPARE(int(inspector.inspectFile(f.fileName()).error.kind), int(Error::Package));
        QCOMPARE(int(inspector.inspectFile(QStringLiteral("/nonexistent.rpm")).error.kind), int(Error::Package));
    }

    void notInstalled()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty())
            QSKIP("rpm not installed");
        SystemProcessRunner runner;
        RpmInspector inspector(runner);
        const auto r = inspector.installedPackage(QStringLiteral("harpoon-surely-not-installed"));
        QVERIFY(r.ok());
        QVERIFY(r.value.name.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRpmInspector)
#include "tst_rpminspector.moc"
