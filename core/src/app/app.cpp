#include "app/app.h"

#include "pkg/rpminspector.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

namespace {

QString dateToJson(const QDateTime &dt)
{
    return dt.isValid() ? dt.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime dateFromJson(const QJsonValue &v)
{
    const QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODate);
    return dt.isValid() ? dt.toUTC() : QDateTime();
}

QJsonArray toArray(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list)
        a.append(s);
    return a;
}

QStringList fromArray(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &item : v.toArray())
        out << item.toString();
    return out;
}

QJsonObject assetToJson(const Asset &a)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), a.name);
    o.insert(QStringLiteral("url"), a.url);
    if (!a.apiUrl.isEmpty())
        o.insert(QStringLiteral("apiUrl"), a.apiUrl);
    if (a.size >= 0)
        o.insert(QStringLiteral("size"), double(a.size));
    if (!a.sha256.isEmpty())
        o.insert(QStringLiteral("sha256"), a.sha256);
    if (a.updatedAt.isValid())
        o.insert(QStringLiteral("updatedAt"), dateToJson(a.updatedAt));
    return o;
}

Asset assetFromJson(const QJsonObject &o)
{
    Asset a;
    a.name = o.value(QStringLiteral("name")).toString();
    a.url = o.value(QStringLiteral("url")).toString();
    a.apiUrl = o.value(QStringLiteral("apiUrl")).toString();
    a.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble(-1));
    a.sha256 = o.value(QStringLiteral("sha256")).toString();
    a.updatedAt = dateFromJson(o.value(QStringLiteral("updatedAt")));
    return a;
}

} // namespace

QJsonObject App::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("temporaryId"), temporaryId);
    o.insert(QStringLiteral("url"), url);
    o.insert(QStringLiteral("sourceId"), sourceId);
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("author"), author);
    o.insert(QStringLiteral("settings"), QJsonObject::fromVariantMap(settings.values()));
    o.insert(QStringLiteral("addedAt"), dateToJson(addedAt));

    QJsonObject latest;
    latest.insert(QStringLiteral("version"), latestVersion);
    latest.insert(QStringLiteral("tag"), latestTag);
    latest.insert(QStringLiteral("title"), latestTitle);
    latest.insert(QStringLiteral("date"), dateToJson(latestDate));
    latest.insert(QStringLiteral("changelog"), changelog);
    latest.insert(QStringLiteral("pageUrl"), releasePageUrl);
    latest.insert(QStringLiteral("prerelease"), latestPrerelease);
    QJsonArray assets;
    for (const Asset &a : latestAssets)
        assets.append(assetToJson(a));
    latest.insert(QStringLiteral("assets"), assets);
    o.insert(QStringLiteral("latest"), latest);

    o.insert(QStringLiteral("lastCheck"), dateToJson(lastCheck));
    o.insert(QStringLiteral("lastError"), lastError);
    if (waitingForBuilds)
        o.insert(QStringLiteral("waitingForBuilds"), true);

    if (receipt.isValid()) {
        QJsonObject r;
        r.insert(QStringLiteral("version"), receipt.version);
        r.insert(QStringLiteral("tag"), receipt.tag);
        r.insert(QStringLiteral("evr"), receipt.evr);
        r.insert(QStringLiteral("assetNames"), toArray(receipt.assetNames));
        r.insert(QStringLiteral("packageNames"), toArray(receipt.packageNames));
        r.insert(QStringLiteral("sha256s"), toArray(receipt.sha256s));
        r.insert(QStringLiteral("installedAt"), dateToJson(receipt.installedAt));
        if (!receipt.verification.isEmpty())
            r.insert(QStringLiteral("verification"), receipt.verification);
        o.insert(QStringLiteral("receipt"), r);
    }
    if (!acknowledgedVersion.isEmpty())
        o.insert(QStringLiteral("acknowledgedVersion"), acknowledgedVersion);
    if (!notifiedVersion.isEmpty())
        o.insert(QStringLiteral("notifiedVersion"), notifiedVersion);
    return o;
}

Result<App> App::fromJson(const QJsonObject &o)
{
    const int schema = o.value(QStringLiteral("schemaVersion")).toInt(0);
    if (schema < 1 || schema > kSchemaVersion)
        return Result<App>::failure(
            Error::make(Error::Storage, QStringLiteral("Unsupported app record schema %1").arg(schema)));

    App app;
    app.id = o.value(QStringLiteral("id")).toString();
    app.url = o.value(QStringLiteral("url")).toString();
    if (app.id.isEmpty() || app.url.isEmpty())
        return Result<App>::failure(Error::make(Error::Storage, QStringLiteral("App record lacks id or url")));
    app.temporaryId = o.value(QStringLiteral("temporaryId")).toBool();
    // The id is a file name in the store and an argument to rpm: a record
    // (or backup) must not smuggle anything else in.
    if (!isValidId(app.id, app.temporaryId))
        return Result<App>::failure(
            Error::make(Error::Storage, QStringLiteral("App record has an invalid id: %1").arg(app.id.left(80))));
    app.sourceId = o.value(QStringLiteral("sourceId")).toString();
    app.name = o.value(QStringLiteral("name")).toString();
    app.author = o.value(QStringLiteral("author")).toString();
    app.settings = AppSettings(o.value(QStringLiteral("settings")).toObject().toVariantMap());
    app.addedAt = dateFromJson(o.value(QStringLiteral("addedAt")));

    const QJsonObject latest = o.value(QStringLiteral("latest")).toObject();
    app.latestVersion = latest.value(QStringLiteral("version")).toString();
    app.latestTag = latest.value(QStringLiteral("tag")).toString();
    app.latestTitle = latest.value(QStringLiteral("title")).toString();
    app.latestDate = dateFromJson(latest.value(QStringLiteral("date")));
    app.changelog = latest.value(QStringLiteral("changelog")).toString();
    app.releasePageUrl = latest.value(QStringLiteral("pageUrl")).toString();
    app.latestPrerelease = latest.value(QStringLiteral("prerelease")).toBool();
    for (const QJsonValue &a : latest.value(QStringLiteral("assets")).toArray())
        app.latestAssets << assetFromJson(a.toObject());

    app.lastCheck = dateFromJson(o.value(QStringLiteral("lastCheck")));
    app.lastError = o.value(QStringLiteral("lastError")).toString();
    app.waitingForBuilds = o.value(QStringLiteral("waitingForBuilds")).toBool();

    const QJsonObject r = o.value(QStringLiteral("receipt")).toObject();
    app.receipt.version = r.value(QStringLiteral("version")).toString();
    app.receipt.tag = r.value(QStringLiteral("tag")).toString();
    app.receipt.evr = r.value(QStringLiteral("evr")).toString();
    app.receipt.assetNames = fromArray(r.value(QStringLiteral("assetNames")));
    app.receipt.packageNames = fromArray(r.value(QStringLiteral("packageNames")));
    app.receipt.sha256s = fromArray(r.value(QStringLiteral("sha256s")));
    app.receipt.installedAt = dateFromJson(r.value(QStringLiteral("installedAt")));
    app.receipt.verification = r.value(QStringLiteral("verification")).toString();
    app.acknowledgedVersion = o.value(QStringLiteral("acknowledgedVersion")).toString();
    app.notifiedVersion = o.value(QStringLiteral("notifiedVersion")).toString();
    return Result<App>::success(app);
}

bool App::isValidId(const QString &id, bool temporary)
{
    static const QRegularExpression temporaryPattern(QStringLiteral("^tmp-[0-9a-f]{12}$"));
    return temporary ? temporaryPattern.match(id).hasMatch() : isValidRpmName(id);
}

QString App::temporaryIdFor(const QString &url)
{
    const QByteArray hash = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("tmp-") + QString::fromLatin1(hash.left(12));
}

App App::fromUrl(const QString &standardUrl, const QString &sourceId)
{
    App app;
    app.url = standardUrl;
    app.sourceId = sourceId;
    app.id = temporaryIdFor(standardUrl);
    app.temporaryId = true;
    app.addedAt = QDateTime::currentDateTimeUtc();
    QStringList parts;
    for (const QString &p : QUrl(standardUrl).path().split(QLatin1Char('/')))
        if (!p.isEmpty())
            parts << p;
    app.name = parts.isEmpty() ? standardUrl : parts.last();
    if (parts.size() >= 2)
        app.author = parts.at(parts.size() - 2);
    return app;
}

} // namespace Harpoon
