#include "app/installedmatch.h"

#include "pkg/rpminspector.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace Harpoon {

QString rpmNameFromFileName(const QString &fileName)
{
    static const QRegularExpression nevra(QStringLiteral("^(.+)-[^-]+-[^-]+\\.[A-Za-z0-9_]+\\.rpm$"));
    const auto m = nevra.match(QFileInfo(fileName).fileName());
    return m.hasMatch() && isValidRpmName(m.captured(1)) ? m.captured(1) : QString();
}

bool adoptInstalledPackage(App &app, const std::function<bool(const QString &)> &isInstalled,
                           const QStringList &taken)
{
    if (!app.temporaryId)
        return false;
    for (const Asset &asset : app.latestAssets) {
        const QString name = rpmNameFromFileName(asset.name);
        if (name.isEmpty() || taken.contains(name) || !isInstalled(name))
            continue;
        app.id = name;
        app.temporaryId = false;
        return true;
    }
    return false;
}

} // namespace Harpoon
