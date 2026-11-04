#include "app/packagebuilds.h"

#include "app/installedmatch.h"

#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
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

// endsSegment: the words end a folder name of a URL ("…-beta.46-harbour/"),
// not a file name.
QString wordsPattern(QStringList words, bool endsSegment = false)
{
    words.sort();
    QStringList escaped;
    for (const QString &w : words)
        escaped << QRegularExpression::escape(w);
    // Whole words only: "store" must not match "restore".
    return QStringLiteral("(?i)[-_.+~](%1)%2").arg(escaped.join(QLatin1Char('|')),
                                                    endsSegment ? QStringLiteral("(/|$)") : QStringLiteral("([-_.+~]|$)"));
}

// The folder a file is in, last part of its URL's path before the file name.
QString folderOf(const QString &url)
{
    const QStringList segments = QUrl(url).path().split(QLatin1Char('/'), QString::SkipEmptyParts);
    return segments.size() >= 2 ? segments.at(segments.size() - 2) : QString();
}

QString packageNameOf(const QString &fileName)
{
    return rpmNameFromFileName(fileName);
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
    QMap<QString, QList<Asset>> byPackage;
    for (const Asset &asset : assets) {
        const QString name = packageNameOf(asset.name);
        if (!name.isEmpty())
            byPackage[name] << asset;
    }
    QList<Asset> group;
    for (auto it = byPackage.constBegin(); it != byPackage.constEnd(); ++it)
        if (it.value().size() > 1) {
            group = it.value();
            if (packageName)
                *packageName = it.key();
            break;
        }
    if (group.isEmpty())
        return {};

    // What tells the builds apart: their file names, or when those are the
    // same, the folders they are in.
    bool sameNames = false;
    for (int i = 0; i < group.size(); ++i)
        for (int j = i + 1; j < group.size(); ++j)
            if (group.at(i).name == group.at(j).name)
                sameNames = true;
    QList<QSet<QString>> words;
    for (const Asset &a : group)
        words << wordsOf(sameNames ? folderOf(a.url) : a.name);
    // The words only one build has.
    QList<QSet<QString>> own;
    for (int i = 0; i < group.size(); ++i) {
        QSet<QString> mine = words.at(i);
        for (int j = 0; j < group.size(); ++j)
            if (j != i)
                mine.subtract(words.at(j));
        own << mine;
    }

    QList<BuildChoice> choices;
    for (int i = 0; i < group.size(); ++i) {
        BuildChoice c;
        c.assetName = group.at(i).name;
        c.key = group.at(i).url;
        QStringList label = own.at(i).values();
        label.sort();
        c.label = label.join(QLatin1Char(' '));
        QSet<QString> others;
        for (int j = 0; j < group.size(); ++j)
            if (j != i)
                others.unite(own.at(j));
        if (!others.isEmpty()) {
            // Leave out the other builds: the release's other packages stay.
            c.filter = wordsPattern(others.values(), sameNames);
            c.invert = true;
        } else if (!own.at(i).isEmpty()) {
            c.filter = wordsPattern(own.at(i).values(), sameNames);
        } else {
            // Told apart only by numbers: this exact file.
            c.filter = QStringLiteral("^%1$").arg(QRegularExpression::escape(sameNames ? group.at(i).url : group.at(i).name));
        }
        choices << c;
    }
    return choices;
}

} // namespace Harpoon
