#include "app/harpoonsettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace Harpoon {

namespace {
const QString kBackend = QStringLiteral("install/backend");
const QString kBackgroundChecks = QStringLiteral("updates/backgroundChecks");
const QString kInterval = QStringLiteral("updates/intervalHours");
const QString kNotify = QStringLiteral("updates/notify");
const QString kAutoUpdate = QStringLiteral("updates/autoUpdate");
const QString kTokenGroup = QStringLiteral("tokens");
} // namespace

QString HarpoonSettings::defaultFilePath()
{
    const QByteArray env = qgetenv("HARPOON_CONFIG_DIR");
    const QString dir = !env.isEmpty() ? QString::fromLocal8Bit(env)
                                       : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return dir + QStringLiteral("/harpoon.conf");
}

HarpoonSettings::HarpoonSettings(const QString &filePath, QObject *parent) : QObject(parent)
{
    const QString path = filePath.isEmpty() ? defaultFilePath() : filePath;
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_settings.reset(new QSettings(path, QSettings::IniFormat));
    // Tokens live here; keep the file private even outside the sandbox.
    if (!QFile::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly))
            f.close();
    }
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QString HarpoonSettings::filePath() const
{
    return m_settings->fileName();
}

QString HarpoonSettings::installBackend() const
{
    const QString b = m_settings->value(kBackend, QStringLiteral("packagekit")).toString();
    return b == QLatin1String("handler") ? b : QStringLiteral("packagekit");
}

void HarpoonSettings::setInstallBackend(const QString &backend)
{
    if (backend == installBackend())
        return;
    m_settings->setValue(kBackend, backend == QLatin1String("handler") ? backend : QStringLiteral("packagekit"));
    emit changed();
}

bool HarpoonSettings::backgroundChecks() const
{
    return m_settings->value(kBackgroundChecks, true).toBool();
}

void HarpoonSettings::setBackgroundChecks(bool enabled)
{
    if (enabled == backgroundChecks())
        return;
    m_settings->setValue(kBackgroundChecks, enabled);
    emit changed();
}

int HarpoonSettings::checkIntervalHours() const
{
    return qBound(1, m_settings->value(kInterval, 6).toInt(), 168);
}

void HarpoonSettings::setCheckIntervalHours(int hours)
{
    hours = qBound(1, hours, 168);
    if (hours == checkIntervalHours())
        return;
    m_settings->setValue(kInterval, hours);
    emit changed();
}

bool HarpoonSettings::notifyUpdates() const
{
    return m_settings->value(kNotify, true).toBool();
}

void HarpoonSettings::setNotifyUpdates(bool enabled)
{
    if (enabled == notifyUpdates())
        return;
    m_settings->setValue(kNotify, enabled);
    emit changed();
}

bool HarpoonSettings::autoUpdate() const
{
    return m_settings->value(kAutoUpdate, false).toBool();
}

void HarpoonSettings::setAutoUpdate(bool enabled)
{
    if (enabled == autoUpdate())
        return;
    m_settings->setValue(kAutoUpdate, enabled);
    emit changed();
}

QString HarpoonSettings::token(const QString &sourceId) const
{
    return m_settings->value(kTokenGroup + QLatin1Char('/') + sourceId).toString();
}

void HarpoonSettings::setToken(const QString &sourceId, const QString &token)
{
    const QString key = kTokenGroup + QLatin1Char('/') + sourceId;
    if (token.trimmed().isEmpty())
        m_settings->remove(key);
    else
        m_settings->setValue(key, token.trimmed());
    m_settings->sync();
    emit tokensChanged();
}

QVariantMap HarpoonSettings::tokens() const
{
    QVariantMap out;
    m_settings->beginGroup(kTokenGroup);
    for (const QString &key : m_settings->childKeys())
        out.insert(key, m_settings->value(key));
    m_settings->endGroup();
    return out;
}

} // namespace Harpoon

namespace Harpoon {

QVariantMap HarpoonSettings::exportable(bool includeTokens) const
{
    QVariantMap out{
        {QStringLiteral("installBackend"), installBackend()},
        {QStringLiteral("backgroundChecks"), backgroundChecks()},
        {QStringLiteral("checkIntervalHours"), checkIntervalHours()},
        {QStringLiteral("notifyUpdates"), notifyUpdates()},
        {QStringLiteral("autoUpdate"), autoUpdate()},
    };
    if (includeTokens)
        out.insert(QStringLiteral("tokens"), tokens());
    return out;
}

void HarpoonSettings::restore(const QVariantMap &values)
{
    if (values.contains(QStringLiteral("installBackend")))
        setInstallBackend(values.value(QStringLiteral("installBackend")).toString());
    if (values.contains(QStringLiteral("backgroundChecks")))
        setBackgroundChecks(values.value(QStringLiteral("backgroundChecks")).toBool());
    if (values.contains(QStringLiteral("checkIntervalHours")))
        setCheckIntervalHours(values.value(QStringLiteral("checkIntervalHours")).toInt());
    if (values.contains(QStringLiteral("notifyUpdates")))
        setNotifyUpdates(values.value(QStringLiteral("notifyUpdates")).toBool());
    if (values.contains(QStringLiteral("autoUpdate")))
        setAutoUpdate(values.value(QStringLiteral("autoUpdate")).toBool());
    const QVariantMap tokens = values.value(QStringLiteral("tokens")).toMap();
    for (auto it = tokens.constBegin(); it != tokens.constEnd(); ++it)
        setToken(it.key(), it.value().toString());
}

} // namespace Harpoon
