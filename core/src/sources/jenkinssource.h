#pragma once

#include "sources/source.h"

namespace Harpoon {

// Artifacts of a Jenkins job's last successful build (ObtainX
// lib/app_sources/jenkins.dart). Never auto-selected: choose it via override.
//
//   GET {job}/lastSuccessfulBuild/api/json
//
// The version (Release.tag) is the build number and the date its timestamp.
// Assets are artifacts[] {fileName, relativePath}; the changelog lists the
// commit messages of the build's change sets.
//
// Differences from upstream:
//  - Jobs inside folders and Jenkins under a path prefix are accepted:
//    https://ci.example.org/jenkins/job/folder/job/app (upstream only
//    matches https://host/job/{name}).
//  - Artifact URLs use the build number ({job}/{number}/artifact/{path})
//    instead of lastSuccessfulBuild, so a build finishing between the check
//    and the download cannot swap the file.
class JenkinsSource : public Source
{
public:
    QString id() const override { return QStringLiteral("Jenkins"); }
    QString displayName() const override { return QStringLiteral("Jenkins"); }
    QStringList defaultHosts() const override { return {}; }
    bool neverAutoSelect() const override { return true; }

    Result<QString> standardizeUrl(const QString &url) const override;
    void fetchReleases(const QString &standardUrl, const AppSettings &settings, HttpTransport &transport,
                       Callback done) override;
};

// Maps a build's api/json to a release.
Result<Release> parseJenkinsBuild(const QByteArray &json, const QString &jobUrl);

} // namespace Harpoon
