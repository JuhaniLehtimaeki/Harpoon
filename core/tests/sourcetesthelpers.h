#pragma once

#include "faketransport.h"
#include "pipeline/releasepipeline.h"
#include "sources/source.h"

#include <QFile>

// Shared by the per-source tests: runs a fetch against a FakeTransport (which
// answers synchronously) and returns the result.
inline Harpoon::FetchResult fetchFrom(Harpoon::Source &source, FakeTransport &transport, const QString &url,
                                      const Harpoon::AppSettings &settings = Harpoon::AppSettings())
{
    Harpoon::FetchResult out;
    bool called = false;
    source.fetchReleases(url, settings, transport, [&](const Harpoon::FetchResult &r) {
        out = r;
        called = true;
    });
    if (!called)
        qFatal("callback not called");
    return out;
}

inline Harpoon::Result<Harpoon::LatestRelease> latestFrom(Harpoon::Source &source, FakeTransport &transport,
                                                          const QString &url, const Harpoon::AppSettings &settings,
                                                          const QString &arch)
{
    Harpoon::DeviceInfo device;
    device.arch = arch;
    Harpoon::Result<Harpoon::LatestRelease> out;
    bool called = false;
    Harpoon::fetchLatestRelease(source, transport, url, settings, device,
                                [&](const Harpoon::Result<Harpoon::LatestRelease> &r) {
                                    out = r;
                                    called = true;
                                });
    if (!called)
        qFatal("callback not called");
    return out;
}

inline QByteArray readFixture(const QString &name)
{
    QFile f(QStringLiteral(HARPOON_FIXTURE_DIR "/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

inline QStringList assetNames(const Harpoon::Release &release)
{
    QStringList out;
    for (const Harpoon::Asset &a : release.assets)
        out << a.name;
    return out;
}
