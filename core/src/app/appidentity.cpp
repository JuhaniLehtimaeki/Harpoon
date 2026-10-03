#include "app/appidentity.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

namespace Harpoon {

namespace {

QString resolveIcon(const QString &icon, const QString &iconsDir)
{
    if (icon.isEmpty())
        return QString();
    if (QFileInfo(icon).isAbsolute())
        return QFileInfo(icon).isFile() ? icon : QString();
    // Largest first: Image scales down cleanly with sourceSize.
    for (const char *size : {"172x172", "128x128", "108x108", "86x86"}) {
        for (const char *ext : {".png", ".svg"}) {
            const QString path = QStringLiteral("%1/%2/apps/%3%4").arg(iconsDir, QLatin1String(size), icon,
                                                                       QLatin1String(ext));
            if (QFileInfo(path).isFile())
                return path;
        }
    }
    return QString();
}

} // namespace

DesktopEntry findDesktopEntry(const QString &package, const QString &applicationsDir, const QString &iconsDir,
                              const QLocale &locale)
{
    DesktopEntry entry;
    if (package.isEmpty() || package.contains(QLatin1Char('/')))
        return entry;
    QFile file(applicationsDir + QLatin1Char('/') + package + QStringLiteral(".desktop"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return entry;

    const QString lang = locale.name();                       // fi_FI
    const QString shortLang = lang.section(QLatin1Char('_'), 0, 0); // fi
    QString name;
    QString localName;
    int localRank = 0; // 2: Name[fi_FI], 1: Name[fi]
    QString icon;
    bool inMainGroup = false;
    QTextStream in(&file);
    in.setCodec("UTF-8");
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inMainGroup = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inMainGroup || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Name"))
            name = value;
        else if (key == QLatin1String("Name[") + lang + QLatin1Char(']') && localRank < 2) {
            localName = value;
            localRank = 2;
        } else if (key == QLatin1String("Name[") + shortLang + QLatin1Char(']') && localRank < 1) {
            localName = value;
            localRank = 1;
        } else if (key == QLatin1String("Icon"))
            icon = value;
    }
    entry.name = localName.isEmpty() ? name : localName;
    entry.iconPath = resolveIcon(icon, iconsDir);
    return entry;
}

QString prettyAppName(const QString &raw)
{
    static const QRegularExpression packageLike(QStringLiteral("^[a-z0-9._-]+$"));
    if (!packageLike.match(raw).hasMatch())
        return raw; // already has capitals, spaces...
    QString base = raw;
    for (const char *prefix : {"harbour-", "openrepos-"})
        if (base.startsWith(QLatin1String(prefix)) && base.size() > int(qstrlen(prefix)))
            base = base.mid(int(qstrlen(prefix)));
    QStringList words;
    for (const QString &w : base.split(QRegularExpression(QStringLiteral("[-_.]+")), QString::SkipEmptyParts))
        words << w.left(1).toUpper() + w.mid(1);
    return words.isEmpty() ? raw : words.join(QLatin1Char(' '));
}

} // namespace Harpoon
