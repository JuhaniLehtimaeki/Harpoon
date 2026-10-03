#pragma once

#include <QLocale>
#include <QString>

namespace Harpoon {

// How an installed app presents itself on the phone, from its .desktop file
// in /usr/share/applications.
struct DesktopEntry
{
    QString name;     // Name, localised when the file has Name[<locale>]
    QString iconPath; // a launcher icon file, largest available; empty when none
    bool isValid() const { return !name.isEmpty(); }
};

// Reads <applicationsDir>/<package>.desktop and resolves its Icon in
// <iconsDir> (hicolor sizes) or as an absolute path.
DesktopEntry findDesktopEntry(const QString &package, const QString &applicationsDir, const QString &iconsDir,
                              const QLocale &locale = QLocale());

// A readable name from a repository or package name, for apps that are not
// installed: "harbour-foil-auth" -> "Foil Auth", "sailfishos-chum-gui" ->
// "Sailfishos Chum Gui". Names that already look like titles are kept.
QString prettyAppName(const QString &raw);

} // namespace Harpoon
