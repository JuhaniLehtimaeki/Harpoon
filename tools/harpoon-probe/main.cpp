// Developer tool: resolve the latest installable release for a forge URL.
//
//   harpoon-probe https://github.com/owner/repo [--arch aarch64] [--sfos 5.0.0.62]
//                 [--source Forgejo] [--prereleases] [--track-only] [--set key=value]...
//
// A token for the matched source can be passed in HARPOON_TOKEN.

#include "net/networktransport.h"
#include "pipeline/releasepipeline.h"
#include "sources/sourceregistry.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

using namespace Harpoon;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("harpoon-probe"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Resolve the latest installable release for a forge URL."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("Repository URL"));
    QCommandLineOption archOpt(QStringLiteral("arch"), QStringLiteral("Device RPM arch (default: detect)."),
                               QStringLiteral("arch"));
    QCommandLineOption sfosOpt(QStringLiteral("sfos"), QStringLiteral("SailfishOS version (default: detect)."),
                               QStringLiteral("version"));
    QCommandLineOption sourceOpt(QStringLiteral("source"),
                                 QStringLiteral("Force a source id (for self-hosted forges)."), QStringLiteral("id"));
    QCommandLineOption preOpt(QStringLiteral("prereleases"), QStringLiteral("Include prereleases."));
    QCommandLineOption trackOpt(QStringLiteral("track-only"), QStringLiteral("Do not require an installable asset."));
    QCommandLineOption setOpt(QStringLiteral("set"), QStringLiteral("Set an app setting."), QStringLiteral("key=value"));
    parser.addOptions({archOpt, sfosOpt, sourceOpt, preOpt, trackOpt, setOpt});
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
    if (parser.positionalArguments().size() != 1)
        parser.showHelp(1);

    SourceRegistry registry;
    const auto match = registry.match(parser.positionalArguments().first(), parser.value(sourceOpt));
    if (!match.ok()) {
        err << match.error.message << '\n';
        err.flush();
        return 2;
    }
    const QByteArray token = qgetenv("HARPOON_TOKEN");
    if (!token.isEmpty())
        match.value.source->setConfig({{QStringLiteral("token"), QString::fromUtf8(token)}});

    DeviceInfo device = DeviceInfo::detect();
    if (parser.isSet(archOpt))
        device.arch = DeviceInfo::normalizeArch(parser.value(archOpt));
    if (parser.isSet(sfosOpt))
        device.osVersion = parser.value(sfosOpt);

    AppSettings settings;
    settings.set(Keys::includePrereleases, parser.isSet(preOpt));
    settings.set(Keys::trackOnly, parser.isSet(trackOpt));
    for (const QString &kv : parser.values(setOpt)) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq > 0)
            settings.set(kv.left(eq).toUtf8().constData(), kv.mid(eq + 1));
    }

    out << "Source:   " << match.value.source->displayName() << " (" << match.value.source->id() << ")\n"
        << "URL:      " << match.value.standardUrl << "\n"
        << "Device:   " << device.arch << ", SailfishOS " << (device.osVersion.isEmpty() ? QStringLiteral("?") : device.osVersion)
        << '\n';

    NetworkTransport transport;
    int exitCode = 0;
    fetchLatestRelease(*match.value.source, transport, match.value.standardUrl, settings, device,
                       [&](const Result<LatestRelease> &result) {
                           if (!result.ok()) {
                               err << "Error: " << result.error.message << '\n';
                               err.flush();
                               exitCode = 3;
                           } else {
                               const LatestRelease &l = result.value;
                               out << "Release:  " << l.release.label();
                               if (!l.release.title.isEmpty() && l.release.title != l.release.tag)
                                   out << " \"" << l.release.title << "\"";
                               out << (l.release.prerelease ? " [prerelease]" : "") << "\n"
                                   << "Version:  " << l.version << "\n"
                                   << "Date:     " << l.release.date.toString(Qt::ISODate) << "\n"
                                   << "Assets:\n";
                               for (const Asset &a : l.assets)
                                   out << "  " << a.name << "  (" << a.size << " bytes)\n    " << a.url << "\n";
                               if (l.assets.isEmpty())
                                   out << "  (none)\n";
                               out.flush();
                           }
                           QCoreApplication::exit(exitCode);
                       });
    app.exec();
    return exitCode;
}
