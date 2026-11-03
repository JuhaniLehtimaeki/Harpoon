#include "app/installedmatch.h"

#include "app/packagebuilds.h"
#include "pkg/rpminspector.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace Harpoon {

QString rpmNameFromFileName(const QString &fileName)
{
    // Known arches first: they also allow a suffix ("…aarch64-openrepos.rpm").
    const RpmFileName parsed = parseRpmFileName(fileName);
    if (parsed.isValid())
        return isValidRpmName(parsed.name) ? parsed.name : QString();
    static const QRegularExpression nevra(QStringLiteral("^(.+)-[^-]+-[^-]+\\.[A-Za-z0-9_]+\\.rpm$"));
    const auto m = nevra.match(QFileInfo(fileName).fileName());
    return m.hasMatch() && isValidRpmName(m.captured(1)) ? m.captured(1) : QString();
}

bool adoptInstalledPackage(App &app, const std::function<bool(const QString &)> &isInstalled,
                           const QStringList &taken)
{
    if (!app.temporaryId)
        return false;
    QStringList candidates;
    for (const Asset &asset : app.latestAssets) {
        const QString name = rpmNameFromFileName(asset.name);
        if (!name.isEmpty() && !candidates.contains(name) && !taken.contains(name) && isInstalled(name))
            candidates << name;
    }
    if (candidates.isEmpty())
        return false;
    // The app, not one of its parts: harbour-foo rather than harbour-foo-data,
    // whichever the release lists first.
    const QString repoName = QUrl(app.url).path().section(QLatin1Char('/'), -1);
    QString chosen;
    for (const QString &name : candidates)
        if (name.compare(repoName, Qt::CaseInsensitive) == 0)
            chosen = name;
    for (int i = 0; i < candidates.size() && chosen.isEmpty(); ++i) {
        bool part = false;
        for (const QString &other : candidates)
            if (other != candidates.at(i) && candidates.at(i).startsWith(other + QLatin1Char('-')))
                part = true;
        if (!part)
            chosen = candidates.at(i);
    }
    app.id = chosen.isEmpty() ? candidates.first() : chosen;
    app.temporaryId = false;
    return true;
}

} // namespace Harpoon
