#pragma once

#include "sources/source.h"

namespace Harpoon {

// One package in an rpm-md repository's tracked set: the analogue of
// ObtainX's fdroidrepo.dart for OBS, Chum, OpenRepos-style or any
// createrepo_c repository. Harpoon-only; never auto-selected.
//
//   GET {base}/repodata/repomd.xml        location of <data type="primary">
//   GET {base}/{primary location}         primary.xml.gz (or plain .xml)
//
// URL: the repository base, i.e. the directory that contains repodata/
// (".../repodata/" or ".../repodata/repomd.xml" is trimmed). The package is
// the packageName setting, or the "package" query parameter of the URL
// (https://repo.example.org/sailfishos_5.0_aarch64/?package=harbour-foo).
//
// Each <package> of that name (source packages excluded) becomes an asset:
// location href (resolved against xml:base or the base URL), size package,
// checksum type="sha256" -> Asset.sha256, time file -> Asset.updatedAt.
// Assets are grouped by EVR into one release each, tag "ver-rel" or
// "epoch:ver-rel" when the epoch is non-zero.
//
// Ordering: releases are returned newest first by rpm EVR order
// (compareEvr). Release.date is the newest file time of the EVR, raised
// where needed so that dates strictly increase with EVR (a later rebuild of
// an older EVR, or equal timestamps, would otherwise reorder them). The
// default "date" sort, "none" and the selector's fallback therefore all
// follow EVR order.
//
// The primary file's sha256 from repomd.xml is verified (the stored file's
// checksum, or the open-checksum for a server that sends it with
// Content-Encoding so that it arrives inflated). gzip is inflated
// with zlib; zstd, xz and bzip2 are reported as unsupported compression.
class RpmMdRepoSource : public Source
{
public:
    QString id() const override { return QStringLiteral("RpmMdRepo"); }
    QString displayName() const override { return QStringLiteral("RPM repository (rpm-md)"); }
    QStringList defaultHosts() const override { return {}; }
    bool neverAutoSelect() const override { return true; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;

    // The base URL without the query, no trailing slash.
    static QString repoBase(const QString &standardUrl);
};

struct RepoMdData
{
    QString href;   // location href, relative to the repository base
    QString sha256;     // checksum of the file as stored (compressed), if given
    QString openSha256; // checksum of the uncompressed XML, if given
};

// The <data type="primary"> entry of repomd.xml.
Result<RepoMdData> parseRepoMd(const QByteArray &xml);

// Inflates gzip data; returns other data unchanged when it does not look
// compressed. zstd/xz/bzip2 are an error.
Result<QByteArray> decompressMetadata(const QByteArray &data);

// Releases for packageName from primary.xml, newest EVR first (see above).
Result<QList<Release>> parseRpmMdPrimary(const QByteArray &xml, const QString &packageName,
                                         const QString &repoBase);

} // namespace Harpoon
