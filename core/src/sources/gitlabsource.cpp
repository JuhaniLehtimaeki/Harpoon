#include "sources/gitlabsource.h"

#include "sources/sourceutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace Harpoon {

namespace {

QDateTime parseIsoDate(const QJsonValue &value)
{
    if (!value.isString())
        return QDateTime();
    // Drop fractional seconds ("12:00:00.123Z"): not every Qt 5 parses them.
    static const QRegularExpression fraction(QStringLiteral("(T\\d{2}:\\d{2}:\\d{2})\\.\\d+"));
    QString text = value.toString();
    text.replace(fraction, QStringLiteral("\\1"));
    const QDateTime dt = QDateTime::fromString(text, Qt::ISODate);
    return dt.isValid() ? dt.toUTC() : QDateTime();
}

QString origin(const QString &standardUrl)
{
    const QUrl url(standardUrl);
    return QStringLiteral("%1://%2").arg(url.scheme(), url.authority());
}

void addOrReplace(QList<Asset> &assets, const Asset &asset)
{
    for (Asset &existing : assets) {
        if (existing.name == asset.name) {
            existing = asset;
            return;
        }
    }
    assets << asset;
}

} // namespace

Result<QString> GitLabSource::standardizeUrl(const QString &input) const
{
    // Everything after the "/-/" separator is a sub-page of the project.
    QStringList segments = preStandardizeUrl(input).split(QLatin1Char('/'));
    const int cut = segments.indexOf(QStringLiteral("-"));
    if (cut > 0)
        segments = segments.mid(0, cut);
    auto result = standardizeWithRegex(segments.join(QLatin1Char('/')), QStringLiteral("/[^/?#]+(/[^/?#]+){1,20}"));
    if (result.ok() && result.value.endsWith(QLatin1String(".git"), Qt::CaseInsensitive))
        result.value.chop(4);
    return result;
}

QString GitLabSource::apiBaseUrl(const QString &standardUrl)
{
    const QString path = QUrl(standardUrl).path().mid(1);
    return origin(standardUrl) + QStringLiteral("/api/v4/projects/")
           + QString::fromLatin1(QUrl::toPercentEncoding(path));
}

QString GitLabSource::withToken(const QString &url) const
{
    const QString token = configToken();
    if (token.isEmpty())
        return url;
    return url + (url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + QStringLiteral("private_token=")
           + QString::fromLatin1(QUrl::toPercentEncoding(token));
}

QString GitLabSource::authorizedAssetUrl(const QString &assetUrl) const
{
    return withToken(assetUrl);
}

HttpRequest GitLabSource::request(const QString &url, const QString &standardUrl) const
{
    HttpRequest req;
    req.url = withToken(url);
    req.setHeader("Accept", "application/json");
    // Cloudflare in front of some instances rejects requests without one.
    req.setHeader("Referer", origin(standardUrl).toUtf8());
    return req;
}

void GitLabSource::fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                                 Callback done)
{
    const QString api = apiBaseUrl(standardUrl);
    const bool trackOnly = settings.getBool(Keys::trackOnly);
    const QString name = displayName();

    transport.get(request(api, standardUrl), [this, api, standardUrl, trackOnly, name, &transport, settings,
                                              done](const HttpResponse &projectResponse) {
        FetchResult result;
        if (projectResponse.status != 200) {
            result.error = httpErrorFor(projectResponse, name, QStringLiteral("Project not found"));
            done(result);
            return;
        }
        const QJsonObject project = QJsonDocument::fromJson(projectResponse.body).object();
        const qint64 projectId = static_cast<qint64>(project.value(QLatin1String("id")).toDouble(-1));
        if (projectId < 0) {
            result.error = Error::make(Error::Parse, QStringLiteral("Unexpected API response: no project id"));
            done(result);
            return;
        }

        // Track-only apps follow tags, as upstream (tags carry their release notes).
        const QString path = trackOnly ? QStringLiteral("/repository/tags") : QStringLiteral("/releases");
        transport.get(request(api + path + QStringLiteral("?per_page=100"), standardUrl),
                      [this, api, standardUrl, settings, &transport, projectId, trackOnly, name,
                       done](const HttpResponse &response) {
                          FetchResult releases;
                          if (response.status != 200) {
                              releases.error = httpErrorFor(response, name, QStringLiteral("Project not found"));
                              done(releases);
                              return;
                          }
                          const auto parsed = parseGitLabReleases(response.body, standardUrl, projectId, trackOnly);
                          releases.releases = parsed.value;
                          releases.error = parsed.error;
                          if (releases.error.ok() && releases.releases.isEmpty())
                              releases.error = Error::make(Error::NoReleases,
                                                           trackOnly ? QStringLiteral("The project has no tags")
                                                                     : noReleasesMessage());
                          // Nothing to install in the releases: the package
                          // registry may have it.
                          bool anyRpm = false;
                          for (const Release &r : releases.releases)
                              for (const Asset &a : r.assets)
                                  if (a.name.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive))
                                      anyRpm = true;
                          if (trackOnly || anyRpm || releases.error.kind == Error::Parse
                              || releases.error.kind == Error::Network) {
                              done(releases);
                              return;
                          }
                          addPackageRegistry(api, standardUrl, settings, transport, releases, done);
                      });
    });
}

namespace {

QString normalizedTag(const QString &tag)
{
    QString t = tag.trimmed().toLower();
    if (t.size() > 1 && t.startsWith(QLatin1Char('v')) && t.at(1).isDigit())
        t = t.mid(1);
    return t;
}

bool isPrereleaseVersion(const QString &version)
{
    static const QRegularExpression marker(QStringLiteral("(^|[-.+~])(dev|snapshot|nightly)([-.+~0-9]|$)"),
                                           QRegularExpression::CaseInsensitiveOption);
    return marker.match(version).hasMatch();
}

QString encoded(const QString &segment)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(segment));
}

} // namespace

Result<QList<GitLabPackage>> parseGitLabPackages(const QByteArray &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return Result<QList<GitLabPackage>>::failure(
            Error::make(Error::Parse, QStringLiteral("Unexpected package registry response")));
    QList<GitLabPackage> out;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject o = value.toObject();
        const QString status = o.value(QLatin1String("status")).toString(QStringLiteral("default"));
        if (status != QLatin1String("default"))
            continue; // being deleted, hidden, failed
        GitLabPackage p;
        p.id = static_cast<qint64>(o.value(QLatin1String("id")).toDouble(0));
        p.name = o.value(QLatin1String("name")).toString();
        p.version = o.value(QLatin1String("version")).toString();
        p.created = parseIsoDate(o.value(QLatin1String("created_at")));
        p.webPath = o.value(QLatin1String("_links")).toObject().value(QLatin1String("web_path")).toString();
        if (p.id > 0 && !p.name.isEmpty() && !p.version.isEmpty())
            out << p;
    }
    return Result<QList<GitLabPackage>>::success(out);
}

QList<GitLabPackageGroup> groupGitLabPackages(const QList<GitLabPackage> &packages)
{
    // Words that mark a pre-release, never a variant of the same release.
    static const QSet<QString> notVariants{QStringLiteral("dev"),     QStringLiteral("snapshot"), QStringLiteral("nightly"),
                                           QStringLiteral("alpha"),   QStringLiteral("beta"),     QStringLiteral("preview"),
                                           QStringLiteral("pre"),     QStringLiteral("rc"),       QStringLiteral("final")};
    static const QRegularExpression word(QStringLiteral("[-.]([A-Za-z]{3,})(?=[-.]|$)"));

    QSet<QString> known; // name + "\n" + version
    for (const GitLabPackage &p : packages)
        known.insert(p.name + QLatin1Char('\n') + p.version);

    QList<GitLabPackageGroup> groups;
    for (const GitLabPackage &p : packages) {
        // "0.6.0-beta.46-harbour" is "0.6.0-beta.46" with the word "harbour".
        QString base = p.version;
        auto it = word.globalMatch(p.version);
        while (it.hasNext()) {
            const auto m = it.next();
            if (notVariants.contains(m.captured(1).toLower()))
                continue;
            const QString without = p.version.left(m.capturedStart(0)) + p.version.mid(m.capturedEnd(0));
            if (known.contains(p.name + QLatin1Char('\n') + without)) {
                base = without;
                break;
            }
        }
        GitLabPackageGroup *group = nullptr;
        for (GitLabPackageGroup &g : groups)
            if (g.version == base)
                group = &g;
        if (!group) {
            groups << GitLabPackageGroup();
            group = &groups.last();
            group->version = base;
            group->prerelease = isPrereleaseVersion(base);
        }
        group->packages << p;
        if (!group->created.isValid() || p.created > group->created)
            group->created = p.created;
    }
    std::stable_sort(groups.begin(), groups.end(),
                     [](const GitLabPackageGroup &a, const GitLabPackageGroup &b) { return a.created > b.created; });
    return groups;
}

Result<QList<Asset>> parseGitLabPackageFiles(const QByteArray &json, const QString &apiBase, const GitLabPackage &package)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return Result<QList<Asset>>::failure(Error::make(Error::Parse, QStringLiteral("Unexpected package files response")));
    QList<Asset> assets;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject o = value.toObject();
        Asset asset;
        asset.name = o.value(QLatin1String("file_name")).toString();
        if (asset.name.isEmpty())
            continue;
        asset.url = apiBase + QStringLiteral("/packages/generic/") + encoded(package.name) + QLatin1Char('/')
                    + encoded(package.version) + QLatin1Char('/') + encoded(asset.name);
        asset.size = static_cast<qint64>(o.value(QLatin1String("size")).toDouble(-1));
        asset.sha256 = o.value(QLatin1String("file_sha256")).toString().toLower();
        asset.updatedAt = parseIsoDate(o.value(QLatin1String("created_at")));
        // The same file name can exist in several versions of a package: the
        // URL tells them apart, so they are not merged by name.
        assets << asset;
    }
    return Result<QList<Asset>>::success(assets);
}

void GitLabSource::addPackageRegistry(const QString &api, const QString &standardUrl, const AppSettings &settings,
                                      HttpTransport &transport, FetchResult releases, Callback done)
{
    const bool includePrereleases = settings.getBool(Keys::includePrereleases);
    transport.get(request(api + QStringLiteral("/packages?package_type=generic&per_page=100&order_by=created_at&sort=desc"),
                          standardUrl),
                  [this, api, standardUrl, includePrereleases, &transport, releases, done](const HttpResponse &response) {
                      // No registry (disabled, or no access): what the releases said stands.
                      const auto packages = response.status == 200 ? parseGitLabPackages(response.body)
                                                                   : Result<QList<GitLabPackage>>::failure(Error());
                      if (!packages.ok()) {
                          done(releases);
                          return;
                      }
                      // The newest few versions; pre-releases only for people who want them.
                      QList<GitLabPackageGroup> groups;
                      for (const GitLabPackageGroup &g : groupGitLabPackages(packages.value))
                          if ((includePrereleases || !g.prerelease) && groups.size() < 5)
                              groups << g;
                      if (groups.isEmpty()) {
                          done(releases);
                          return;
                      }

                      // Everything the answer needs, filled in as requests return.
                      struct State
                      {
                          FetchResult result;
                          QList<QList<Asset>> assets; // per group
                          QHash<QString, QJsonObject> tags; // normalised name -> tag
                          int pending = 0;
                      };
                      auto state = std::make_shared<State>();
                      state->result = releases;
                      for (int i = 0; i < groups.size(); ++i)
                          state->assets << QList<Asset>();
                      state->pending = 1; // the tags
                      for (const GitLabPackageGroup &g : groups)
                          state->pending += g.packages.size();

                      auto finish = [state, groups, standardUrl, done]() {
                          if (--state->pending > 0)
                              return;
                          FetchResult out = state->result;
                          const QString host = origin(standardUrl);
                          for (int i = 0; i < groups.size(); ++i) {
                              const GitLabPackageGroup &g = groups.at(i);
                              if (state->assets.at(i).isEmpty())
                                  continue;
                              const QJsonObject tag = state->tags.value(normalizedTag(g.version));
                              const QString tagName = tag.value(QLatin1String("name")).toString();
                              // A release of that tag already (its source archive): the builds join it.
                              Release *release = nullptr;
                              for (Release &r : out.releases)
                                  if (normalizedTag(r.tag) == normalizedTag(g.version))
                                      release = &r;
                              Release added;
                              if (!release) {
                                  added.tag = tagName.isEmpty() ? g.version : tagName;
                                  added.date = g.created;
                                  added.prerelease = g.prerelease;
                                  added.changelog = tag.value(QLatin1String("message")).toString().trimmed();
                                  added.pageUrl = tagName.isEmpty()
                                                      ? host + g.packages.first().webPath
                                                      : standardUrl + QStringLiteral("/-/tags/") + encoded(tagName);
                                  release = &added;
                              }
                              for (const Asset &a : state->assets.at(i))
                                  release->assets << a;
                              if (release == &added)
                                  out.releases << added;
                          }
                          out.error = Error(); // there are builds now
                          if (out.releases.isEmpty())
                              out.error = Error::make(Error::NoReleases, noReleasesMessage());
                          done(out);
                      };

                      // Notes and tag names: optional.
                      transport.get(request(api + QStringLiteral("/repository/tags?per_page=100"), standardUrl),
                                    [state, finish](const HttpResponse &tagsResponse) {
                                        if (tagsResponse.status == 200) {
                                            const QJsonArray arr = QJsonDocument::fromJson(tagsResponse.body).array();
                                            for (const QJsonValue &v : arr) {
                                                const QJsonObject t = v.toObject();
                                                state->tags.insert(normalizedTag(t.value(QLatin1String("name")).toString()), t);
                                            }
                                        }
                                        finish();
                                    });
                      for (int i = 0; i < groups.size(); ++i)
                          for (const GitLabPackage &package : groups.at(i).packages)
                              transport.get(request(api + QStringLiteral("/packages/") + QString::number(package.id)
                                                        + QStringLiteral("/package_files?per_page=100"),
                                                    standardUrl),
                                            [state, finish, i, api, package](const HttpResponse &filesResponse) {
                                                if (filesResponse.status == 200) {
                                                    const auto files = parseGitLabPackageFiles(filesResponse.body, api, package);
                                                    if (files.ok())
                                                        state->assets[i] << files.value;
                                                }
                                                finish();
                                            });
                  });
}

Result<QList<Release>> parseGitLabReleases(const QByteArray &json, const QString &standardUrl, qint64 projectId,
                                           bool fromTags)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
        return Result<QList<Release>>::failure(Error::make(
            Error::Parse, QStringLiteral("Unexpected API response: %1")
                              .arg(err.error != QJsonParseError::NoError ? err.errorString()
                                                                         : QStringLiteral("not a JSON array"))));

    const QString host = origin(standardUrl);
    static const QRegularExpression uploads(QStringLiteral("\\]\\((/uploads/[^)\\s]+\\.rpm)\\)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression artifact(QStringLiteral("^%1/-/jobs/[0-9]+/artifacts/file/[^/]+")
                                          .arg(QRegularExpression::escape(standardUrl)));

    QList<Release> releases;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject r = value.toObject();
        const QJsonObject commit = r.value(QLatin1String("commit")).toObject();
        const QJsonObject tagRelease = r.value(QLatin1String("release")).toObject();

        Release release;
        release.tag = r.value(QLatin1String(fromTags ? "name" : "tag_name")).toString();
        if (release.tag.isEmpty())
            release.tag = r.value(QLatin1String("name")).toString();
        if (!fromTags)
            release.title = r.value(QLatin1String("name")).toString();
        if (release.tag.isEmpty() && release.title.isEmpty())
            continue;

        for (const QJsonValue &v : {r.value(QLatin1String("description")), tagRelease.value(QLatin1String("description")),
                                    r.value(QLatin1String("message")), commit.value(QLatin1String("message"))}) {
            if (v.isString() && !v.toString().trimmed().isEmpty()) {
                release.changelog = v.toString().trimmed();
                break;
            }
        }
        for (const QJsonValue &v : {r.value(QLatin1String("released_at")), r.value(QLatin1String("created_at")),
                                    commit.value(QLatin1String("created_at"))}) {
            release.date = parseIsoDate(v);
            if (release.date.isValid())
                break;
        }
        release.prerelease = r.value(QLatin1String("upcoming_release")).toBool();
        release.pageUrl = r.value(QLatin1String("_links")).toObject().value(QLatin1String("self")).toString();
        if (release.pageUrl.isEmpty())
            release.pageUrl = standardUrl + (fromTags ? QStringLiteral("/-/tags/") : QStringLiteral("/-/releases/"))
                              + QString::fromLatin1(QUrl::toPercentEncoding(release.tag));

        const QJsonArray links = r.value(QLatin1String("assets")).toObject().value(QLatin1String("links")).toArray();
        for (const QJsonValue &linkValue : links) {
            const QJsonObject link = linkValue.toObject();
            Asset asset;
            asset.url = link.value(QLatin1String("direct_asset_url")).toString();
            if (asset.url.isEmpty())
                asset.url = link.value(QLatin1String("url")).toString();
            if (asset.url.isEmpty())
                continue;
            asset.name = link.value(QLatin1String("name")).toString().trimmed();
            const QString fileName = lastPathSegment(asset.url);
            if (asset.name.isEmpty()
                || (!asset.name.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive)
                    && fileName.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive)))
                asset.name = fileName;
            if (asset.name.isEmpty())
                continue;
            addOrReplace(release.assets, asset);
        }

        auto it = uploads.globalMatch(release.changelog);
        while (it.hasNext()) {
            Asset asset;
            asset.url = QStringLiteral("%1/-/project/%2%3").arg(host).arg(projectId).arg(it.next().captured(1));
            asset.name = lastPathSegment(asset.url);
            addOrReplace(release.assets, asset);
        }

        // CI job artifacts: the /file/ page is HTML, /raw/ is the file itself.
        for (Asset &asset : release.assets)
            if (artifact.match(asset.url).hasMatch())
                asset.url.replace(asset.url.indexOf(QLatin1String("/file/"), standardUrl.size()), 6,
                                  QStringLiteral("/raw/"));

        releases << release;
    }
    return Result<QList<Release>>::success(releases);
}

} // namespace Harpoon

namespace Harpoon {

void GitLabSource::prepareDownload(const Asset &asset, const AppSettings &, const QString &standardUrl,
                                   DownloadRequest &request) const
{
    if (isOwnOrigin(asset.url, standardUrl))
        request.url = authorizedAssetUrl(asset.url);
}

} // namespace Harpoon
