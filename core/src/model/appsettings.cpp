#include "model/appsettings.h"

namespace Harpoon {

bool AppSettings::getBool(const char *key, bool defaultValue) const
{
    const auto it = m_values.constFind(QString::fromLatin1(key));
    if (it == m_values.constEnd() || !it->isValid())
        return defaultValue;
    if (it->type() == QVariant::String) {
        const QString s = it->toString().trimmed().toLower();
        if (s == QLatin1String("true") || s == QLatin1String("1"))
            return true;
        if (s == QLatin1String("false") || s == QLatin1String("0"))
            return false;
        return defaultValue;
    }
    return it->toBool();
}

QString AppSettings::getString(const char *key, const QString &defaultValue) const
{
    const auto it = m_values.constFind(QString::fromLatin1(key));
    if (it == m_values.constEnd() || !it->isValid() || it->isNull())
        return defaultValue;
    return it->toString();
}

} // namespace Harpoon
