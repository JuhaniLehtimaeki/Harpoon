#pragma once

#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

// Builds real (empty) RPM packages with rpmbuild for tests.
class RpmFactory
{
public:
    static bool available() { return !QStandardPaths::findExecutable(QStringLiteral("rpmbuild")).isEmpty(); }

    // Returns the path of the built package, or an empty string on failure.
    QString build(const QString &name, const QString &version, const QString &release, const QString &arch,
                  const QString &vendor = QStringLiteral("chum"), int epoch = 0)
    {
        const QString top = m_dir.path() + QStringLiteral("/top-") + QString::number(m_counter++);
        QDir().mkpath(top);
        const QString specPath = top + QStringLiteral("/p.spec");
        QFile spec(specPath);
        spec.open(QIODevice::WriteOnly);
        QByteArray text = "Name: " + name.toUtf8() + "\nVersion: " + version.toUtf8() + "\nRelease: "
                          + release.toUtf8() + "\nSummary: Test package\nLicense: MIT\n";
        if (!vendor.isEmpty())
            text += "Vendor: " + vendor.toUtf8() + "\n";
        if (epoch > 0)
            text += "Epoch: " + QByteArray::number(epoch) + "\n";
        if (arch == QLatin1String("noarch"))
            text += "BuildArch: noarch\n";
        text += "%description\nTest\n%files\n";
        spec.write(text);
        spec.close();

        QStringList args{QStringLiteral("-bb"), QStringLiteral("--quiet"), QStringLiteral("--define"),
                         QStringLiteral("_topdir ") + top};
        if (arch != QLatin1String("noarch"))
            args << QStringLiteral("--target") << arch;
        args << specPath;
        QProcess p;
        p.start(QStringLiteral("rpmbuild"), args);
        if (!p.waitForFinished(60000) || p.exitCode() != 0)
            return QString();
        QDirIterator it(top + QStringLiteral("/RPMS"), {QStringLiteral("*.rpm")}, QDir::Files,
                        QDirIterator::Subdirectories);
        return it.hasNext() ? it.next() : QString();
    }

private:
    QTemporaryDir m_dir;
    int m_counter = 0;
};
