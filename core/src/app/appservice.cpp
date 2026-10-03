#include "app/appservice.h"

#include "app/harpoonsettings.h"

#include <QStandardPaths>

namespace Harpoon {

QString defaultDownloadDirectory()
{
    const QByteArray env = qgetenv("HARPOON_CACHE_DIR");
    return (!env.isEmpty() ? QString::fromLocal8Bit(env) : QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
           + QStringLiteral("/downloads");
}

QHash<QString, QVariantMap> sourceConfigs(const HarpoonSettings &settings, const QStringList &sourceIds, bool environment)
{
    QHash<QString, QVariantMap> configs;
    const QVariantMap tokens = settings.tokens();
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it)
        configs.insert(it.key(), {{QStringLiteral("token"), it.value()}});
    if (environment) {
        for (const QString &id : sourceIds) {
            const QByteArray env = qgetenv("HARPOON_TOKEN_" + id.toUpper().toLatin1());
            if (!env.isEmpty())
                configs.insert(id, {{QStringLiteral("token"), QString::fromUtf8(env)}});
        }
    }
    return configs;
}

App recordAfterInstall(const App &current, const InstallResult &result, bool idTaken)
{
    App record = current;
    record.receipt = result.app.receipt;
    if (!idTaken) {
        record.id = result.app.id;
        record.temporaryId = false;
    }
    return record;
}

} // namespace Harpoon
