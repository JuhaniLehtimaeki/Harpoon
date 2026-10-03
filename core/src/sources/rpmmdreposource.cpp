#include "sources/rpmmdreposource.h"

#include "sources/sourceutil.h"
#include "version/rpmversion.h"

#include <QCryptographicHash>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <zlib.h>

#include <algorithm>

namespace Harpoon {

namespace {

// Inflated primary.xml. Large distribution repositories exceed this; the
// repositories apps publish are a few MB, and the phone has little memory.
const qint64 kMaxMetadataSize = 96LL * 1024 * 1024;

int algorithmRank(const QString &type)
{
    if (type == QLatin1String("sha512"))
        return 3;
    if (type == QLatin1String("sha384"))
        return 2;
    if (type == QLatin1String("sha1") || type == QLatin1String("sha"))
        return 1;
    return 0;
}

QCryptographicHash::Algorithm hashFor(const QString &type)
{
    if (type == QLatin1String("sha512"))
        return QCryptographicHash::Sha512;
    if (type == QLatin1String("sha384"))
        return QCryptographicHash::Sha384;
    return QCryptographicHash::Sha1;
}

const char *kRepoNs = "http://linux.duke.edu/metadata/repo";
const char *kCommonNs = "http://linux.duke.edu/metadata/common";

Result<QByteArray> gunzip(const QByteArray &input)
{
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        return Result<QByteArray>::failure(Error::make(Error::Parse, QStringLiteral("zlib initialisation failed")));

    QByteArray out;
    char buffer[64 * 1024];
    Error error;
    for (;;) {
        stream.next_out = reinterpret_cast<Bytef *>(buffer);
        stream.avail_out = sizeof(buffer);
        const int ret = inflate(&stream, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            error = Error::make(Error::Parse, ret == Z_BUF_ERROR ? QStringLiteral("Repository metadata is truncated")
                                                                 : QStringLiteral("Repository metadata is corrupt"));
            break;
        }
        out.append(buffer, static_cast<int>(sizeof(buffer) - stream.avail_out));
        if (out.size() > kMaxMetadataSize) {
            error = Error::make(Error::Parse, QStringLiteral("Repository metadata is too large"));
            break;
        }
        if (ret == Z_STREAM_END) {
            if (stream.avail_in == 0)
                break;
            inflateReset(&stream); // concatenated gzip members
        }
    }
    inflateEnd(&stream);
    if (!error.ok())
        return Result<QByteArray>::failure(error);
    return Result<QByteArray>::success(out);
}

} // namespace

Result<QByteArray> decompressMetadata(const QByteArray &data)
{
    if (data.startsWith("\x1f\x8b"))
        return gunzip(data);
    QString format;
    if (data.startsWith("\x28\xb5\x2f\xfd"))
        format = QStringLiteral("zstd");
    else if (data.startsWith(QByteArray("\xfd" "7zXZ\x00", 6)))
        format = QStringLiteral("xz");
    else if (data.startsWith("BZh"))
        format = QStringLiteral("bzip2");
    if (!format.isEmpty())
        return Result<QByteArray>::failure(Error::make(
            Error::Parse, QStringLiteral("Unsupported repository metadata compression: %1 (only gzip and plain XML "
                                         "are supported)")
                              .arg(format)));
    return Result<QByteArray>::success(data);
}

Result<QString> RpmMdRepoSource::standardizeUrl(const QString &input) const
{
    const QUrl url(preStandardizeUrl(input));
    const QString scheme = url.scheme().toLower();
    if (!url.isValid() || url.host().isEmpty()
        || (scheme != QLatin1String("http") && scheme != QLatin1String("https")))
        return Result<QString>::failure(
            Error::make(Error::InvalidUrl, QStringLiteral("Not a valid repository URL: %1").arg(input)));

    QString path = url.path();
    for (const char *suffix : {"/repodata/repomd.xml", "/repodata/", "/repodata"})
        if (path.endsWith(QLatin1String(suffix)))
            path.chop(static_cast<int>(qstrlen(suffix)));
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);

    QUrl standard(url);
    standard.setPath(path);
    standard.setFragment(QString());
    // Only the package parameter is meaningful.
    const QString package = QUrlQuery(url).queryItemValue(QStringLiteral("package"));
    QUrlQuery query;
    if (!package.isEmpty())
        query.addQueryItem(QStringLiteral("package"), package);
    standard.setQuery(query);
    return Result<QString>::success(standard.toString());
}

QString RpmMdRepoSource::repoBase(const QString &standardUrl)
{
    QUrl url(standardUrl);
    url.setQuery(QString());
    QString base = url.toString();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

void RpmMdRepoSource::fetchReleases(const QString &standardUrl, const AppSettings &settings,
                                    HttpTransport &transport, Callback done)
{
    QString package = settings.getString(Keys::packageName).trimmed();
    if (package.isEmpty())
        package = QUrlQuery(QUrl(standardUrl)).queryItemValue(QStringLiteral("package")).trimmed();
    if (package.isEmpty()) {
        FetchResult result;
        result.error = Error::make(Error::InvalidSetting,
                                   QStringLiteral("Set the package name to track in this repository (packageName)"));
        done(result);
        return;
    }

    const QString base = repoBase(standardUrl);
    HttpRequest request;
    request.url = base + QStringLiteral("/repodata/repomd.xml");
    transport.get(request, [this, base, package, &transport, done](const HttpResponse &response) {
        FetchResult result;
        if (response.status != 200) {
            result.error = httpErrorFor(response, displayName(),
                                        QStringLiteral("No rpm-md repository at %1 (repodata/repomd.xml not found)").arg(base));
            done(result);
            return;
        }
        const auto repomd = parseRepoMd(response.body);
        if (!repomd.ok()) {
            result.error = repomd.error;
            done(result);
            return;
        }

        HttpRequest primaryRequest;
        primaryRequest.url = resolveUrl(base + QLatin1Char('/'), repomd.value.href);
        const RepoMdData expected = repomd.value;
        transport.get(primaryRequest, [this, base, package, expected, done](const HttpResponse &primary) {
            FetchResult out;
            if (primary.status != 200) {
                out.error = httpErrorFor(primary, displayName(), QStringLiteral("Repository metadata not found"));
                done(out);
                return;
            }
            // repomd.xml's checksum binds the metadata to the repository; the
            // file itself may be the stored (compressed) or the open one.
            bool matches = true;
            if (!expected.sha256.isEmpty()) {
                const QString actual =
                    QString::fromLatin1(QCryptographicHash::hash(primary.body, QCryptographicHash::Sha256).toHex());
                matches = actual == expected.sha256 || actual == expected.openSha256;
            } else if (!expected.otherChecksum.isEmpty()) {
                const QString actual = QString::fromLatin1(
                    QCryptographicHash::hash(primary.body, hashFor(expected.otherAlgorithm)).toHex());
                matches = actual == expected.otherChecksum || actual == expected.otherOpenChecksum;
            }
            if (!matches) {
                out.error = Error::make(Error::Checksum,
                                        QStringLiteral("Repository metadata does not match its checksum in repomd.xml"));
                done(out);
                return;
            }
            const auto xml = decompressMetadata(primary.body);
            if (!xml.ok()) {
                out.error = xml.error;
                done(out);
                return;
            }
            const auto releases = parseRpmMdPrimary(xml.value, package, base);
            out.releases = releases.value;
            out.error = releases.error;
            if (out.error.ok() && out.releases.isEmpty())
                out.error = Error::make(Error::NoReleases,
                                        QStringLiteral("Package %1 is not in the repository").arg(package));
            done(out);
        });
    });
}

Result<RepoMdData> parseRepoMd(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    bool inPrimary = false;
    RepoMdData data;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement() && reader.name() == QLatin1String("data") && inPrimary) {
            if (!data.href.isEmpty())
                return Result<RepoMdData>::success(data);
            inPrimary = false;
        }
        if (!reader.isStartElement() || reader.namespaceUri() != QLatin1String(kRepoNs))
            continue;
        if (reader.name() == QLatin1String("data")) {
            inPrimary = reader.attributes().value(QLatin1String("type")) == QLatin1String("primary");
            data = RepoMdData();
        } else if (inPrimary && reader.name() == QLatin1String("location")) {
            data.href = reader.attributes().value(QLatin1String("href")).toString();
        } else if (inPrimary && (reader.name() == QLatin1String("checksum")
                                 || reader.name() == QLatin1String("open-checksum"))) {
            const bool uncompressed = reader.name() == QLatin1String("open-checksum");
            const QString type = reader.attributes().value(QLatin1String("type")).toString().toLower();
            const QString text = reader.readElementText().trimmed().toLower();
            if (type == QLatin1String("sha256")) {
                (uncompressed ? data.openSha256 : data.sha256) = text;
            } else if (algorithmRank(type) > 0
                       && (data.otherAlgorithm.isEmpty() || data.otherAlgorithm == type
                           || algorithmRank(type) > algorithmRank(data.otherAlgorithm))) {
                if (data.otherAlgorithm != type) {
                    data.otherChecksum.clear();
                    data.otherOpenChecksum.clear();
                }
                data.otherAlgorithm = type;
                (uncompressed ? data.otherOpenChecksum : data.otherChecksum) = text;
            }
        }
    }
    if (reader.hasError())
        return Result<RepoMdData>::failure(
            Error::make(Error::Parse, QStringLiteral("Invalid repomd.xml: %1").arg(reader.errorString())));
    return Result<RepoMdData>::failure(Error::make(Error::Parse, QStringLiteral("repomd.xml lists no primary metadata")));
}

Result<QList<Release>> parseRpmMdPrimary(const QByteArray &xml, const QString &packageName, const QString &repoBase)
{
    struct Entry
    {
        Evr evr;
        Asset asset;
    };
    QList<Entry> entries;

    QXmlStreamReader reader(xml);
    bool inPackage = false;
    QString name, arch, locationBase;
    Entry entry;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement() && inPackage && reader.name() == QLatin1String("package")
            && reader.namespaceUri() == QLatin1String(kCommonNs)) {
            inPackage = false;
            if (name == packageName && arch != QLatin1String("src") && arch != QLatin1String("nosrc")
                && !entry.asset.url.isEmpty()) {
                const QString base = locationBase.isEmpty() ? repoBase + QLatin1Char('/') : locationBase;
                entry.asset.url = resolveUrl(base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/'),
                                             entry.asset.url);
                entry.asset.name = lastPathSegment(entry.asset.url);
                entries << entry;
            }
            continue;
        }
        if (!reader.isStartElement() || reader.namespaceUri() != QLatin1String(kCommonNs))
            continue;
        const QStringRef element = reader.name();
        const QXmlStreamAttributes attrs = reader.attributes();
        if (element == QLatin1String("package")) {
            inPackage = attrs.value(QLatin1String("type")) == QLatin1String("rpm");
            name.clear();
            arch.clear();
            locationBase.clear();
            entry = Entry();
        } else if (!inPackage) {
            continue;
        } else if (element == QLatin1String("format")) {
            reader.skipCurrentElement(); // file lists, provides, requires
        } else if (element == QLatin1String("name")) {
            name = reader.readElementText().trimmed();
            if (name != packageName) {
                inPackage = false; // ignore the rest of this package
            }
        } else if (element == QLatin1String("arch")) {
            arch = reader.readElementText().trimmed();
        } else if (element == QLatin1String("version")) {
            entry.evr.epoch = attrs.value(QLatin1String("epoch")).toString().toInt();
            entry.evr.version = attrs.value(QLatin1String("ver")).toString();
            entry.evr.release = attrs.value(QLatin1String("rel")).toString();
        } else if (element == QLatin1String("checksum")) {
            const bool sha256 = attrs.value(QLatin1String("type")) == QLatin1String("sha256");
            const QString text = reader.readElementText().trimmed().toLower();
            if (sha256)
                entry.asset.sha256 = text;
        } else if (element == QLatin1String("time")) {
            bool ok = false;
            const qint64 secs = attrs.value(QLatin1String("file")).toString().toLongLong(&ok);
            if (ok && secs > 0)
                entry.asset.updatedAt = QDateTime::fromMSecsSinceEpoch(secs * 1000, Qt::UTC);
        } else if (element == QLatin1String("size")) {
            bool ok = false;
            const qint64 size = attrs.value(QLatin1String("package")).toString().toLongLong(&ok);
            if (ok)
                entry.asset.size = size;
        } else if (element == QLatin1String("location")) {
            entry.asset.url = attrs.value(QLatin1String("href")).toString();
            locationBase = attrs.value(QLatin1String("http://www.w3.org/XML/1998/namespace"), QLatin1String("base"))
                               .toString();
        }
    }
    if (reader.hasError())
        return Result<QList<Release>>::failure(
            Error::make(Error::Parse, QStringLiteral("Invalid primary.xml: %1").arg(reader.errorString())));

    // Group by EVR, oldest first.
    std::stable_sort(entries.begin(), entries.end(),
                     [](const Entry &a, const Entry &b) { return compareEvr(a.evr, b.evr) < 0; });
    QList<Release> ascending;
    for (int i = 0; i < entries.size(); ++i) {
        const Entry &e = entries.at(i);
        if (i == 0 || compareEvr(entries.at(i - 1).evr, e.evr) != 0) {
            Release release;
            release.tag = e.evr.toString();
            release.title = packageName + QLatin1Char('-') + release.tag;
            release.pageUrl = repoBase;
            ascending << release;
        }
        Release &release = ascending.last();
        release.assets << e.asset;
        if (e.asset.updatedAt.isValid() && (!release.date.isValid() || e.asset.updatedAt > release.date))
            release.date = e.asset.updatedAt;
    }

    // Make dates strictly increase with EVR so a date sort keeps EVR order.
    QDateTime previous;
    for (Release &release : ascending) {
        if (previous.isValid() && (!release.date.isValid() || release.date <= previous))
            release.date = previous.addSecs(1);
        else if (!release.date.isValid())
            release.date = QDateTime::fromMSecsSinceEpoch(0, Qt::UTC);
        previous = release.date;
    }
    std::reverse(ascending.begin(), ascending.end());
    return Result<QList<Release>>::success(ascending);
}

} // namespace Harpoon
