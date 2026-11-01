#include "app/packagebuilds.h"

#include "app/installedmatch.h"

#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace Harpoon {

namespace {

// Words in a file name that contain a letter: "harbour", "openrepos",
// "alpha"; not "1", "8" or "rpm".
QSet<QString> wordsOf(const QString &fileName)
{
    static const QRegularExpression separators(QStringLiteral("[-_.+~]+"));
    static const QRegularExpression letter(QStringLiteral("[A-Za-z]"));
    QSet<QString> words;
    QString base = fileName;
    if (base.endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive))
        base.chop(4);
    for (const QString &w : base.split(separators))
        if (w.contains(letter))
            words.insert(w.toLower());
    return words;
}

QString wordsPattern(QStringList words)
{
    words.sort();
    QStringList escaped;
    for (const QString &w : words)
        escaped << QRegularExpression::escape(w);
    // Whole words only: "store" must not match "restore".
    return QStringLiteral("(?i)[-_.+~](%1)([-_.+~]|$)").arg(escaped.join(QLatin1Char('|')));
}

QString packageNameOf(const QString &fileName)
{
    const RpmFileName parsed = parseRpmFileName(fileName);
    return parsed.isValid() ? parsed.name : rpmNameFromFileName(fileName);
}

} // namespace

RpmFileName parseRpmFileName(const QString &fileName)
{
    static const QRegularExpression pattern(
        QStringLiteral("^(.+)-([^-]+)-([^-]+)\\.(aarch64|armv7hl|armv7l|armv7tnhl|i486|i586|i686|x86_64|noarch)"
                       "(?:[-_.][^/]*)?\\.rpm$"));
    RpmFileName out;
    const auto m = pattern.match(QFileInfo(fileName).fileName());
    if (!m.hasMatch())
        return out;
    out.name = m.captured(1);
    out.evr.version = m.captured(2);
    out.evr.release = m.captured(3);
    out.arch = m.captured(4);
    return out;
}

QList<BuildChoice> buildChoices(const QList<Asset> &assets, QString *packageName)
{
    QMap<QString, QStringList> byPackage;
    for (const Asset &asset : assets) {
        const QString name = packageNameOf(asset.name);
        if (!name.isEmpty())
            byPackage[name] << asset.name;
    }
    QStringList files;
    for (auto it = byPackage.constBegin(); it != byPackage.constEnd(); ++it)
        if (it.value().size() > 1) {
            files = it.value();
            if (packageName)
                *packageName = it.key();
            break;
        }
    if (files.isEmpty())
        return {};

    QList<QSet<QString>> words;
    for (const QString &f : files)
        words << wordsOf(f);
    // The words only one build has.
    QList<QSet<QString>> own;
    for (int i = 0; i < files.size(); ++i) {
        QSet<QString> mine = words.at(i);
        for (int j = 0; j < files.size(); ++j)
            if (j != i)
                mine.subtract(words.at(j));
        own << mine;
    }

    QList<BuildChoice> choices;
    for (int i = 0; i < files.size(); ++i) {
        BuildChoice c;
        c.assetName = files.at(i);
        QStringList label = own.at(i).values();
        label.sort();
        c.label = label.join(QLatin1Char(' '));
        QSet<QString> others;
        for (int j = 0; j < files.size(); ++j)
            if (j != i)
                others.unite(own.at(j));
        if (!others.isEmpty()) {
            // Leave out the other builds: the release's other packages stay.
            c.filter = wordsPattern(others.values());
            c.invert = true;
        } else if (!own.at(i).isEmpty()) {
            c.filter = wordsPattern(own.at(i).values());
        } else {
            // Told apart only by numbers: this exact file.
            c.filter = QStringLiteral("^%1$").arg(QRegularExpression::escape(files.at(i)));
        }
        choices << c;
    }
    return choices;
}

} // namespace Harpoon
