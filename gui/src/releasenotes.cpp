#include "releasenotes.h"

#include <QRegularExpression>
#include <QStringList>

namespace Harpoon {

namespace {

QString escaped(const QString &text)
{
    QString out = text;
    out.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    out.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    out.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    out.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return out;
}

// Inline markup in one (already escaped) line.
QString inlineMarkup(QString line)
{
    // [text](https://...) -> link; other schemes stay plain text.
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]+)\\]\\((https?://[^)\\s]+)\\)"));
    line.replace(link, QStringLiteral("<a href=\"\\2\">\\1</a>"));
    // Bare URLs that are not already inside a link.
    static const QRegularExpression bare(QStringLiteral("(^|[\\s(])(https?://[^\\s<)]+)"));
    line.replace(bare, QStringLiteral("\\1<a href=\"\\2\">\\2</a>"));
    static const QRegularExpression bold(QStringLiteral("\\*\\*([^*]+)\\*\\*|__([^_]+)__"));
    line.replace(bold, QStringLiteral("<b>\\1\\2</b>"));
    static const QRegularExpression code(QStringLiteral("`([^`]+)`"));
    line.replace(code, QStringLiteral("\\1"));
    return line;
}

} // namespace

QString releaseNotesToStyledText(const QString &markdown)
{
    QString text = markdown;
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    // HTML in release notes (<details>, <img>, comments): keep only the text.
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"),
                                             QRegularExpression::DotMatchesEverythingOption);
    text.remove(comments);
    static const QRegularExpression tags(QStringLiteral("<[^>\\n]+>"));
    text.remove(tags);

    static const QRegularExpression heading(QStringLiteral("^#{1,6}\\s+(.*?)\\s*#*$"));
    static const QRegularExpression bullet(QStringLiteral("^(\\s*)[-*+]\\s+(.*)$"));
    static const QRegularExpression rule(QStringLiteral("^\\s*([-*_])\\s*(\\1\\s*){2,}$"));
    QStringList out;
    bool lastBlank = true;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed().isEmpty() ? QString() : raw;
        if (line.isEmpty() || rule.match(line).hasMatch()) {
            if (!lastBlank)
                out << QString(); // one paragraph break, however many blank lines
            lastBlank = true;
            continue;
        }
        lastBlank = false;
        const auto h = heading.match(line.trimmed());
        if (h.hasMatch()) {
            out << QStringLiteral("<b>") + inlineMarkup(escaped(h.captured(1))) + QStringLiteral("</b>");
            continue;
        }
        const auto b = bullet.match(line);
        if (b.hasMatch()) {
            const QString indent = b.captured(1).size() >= 2 ? QStringLiteral("&nbsp;&nbsp;&nbsp;") : QString();
            out << indent + QStringLiteral("• ") + inlineMarkup(escaped(b.captured(2).trimmed()));
            continue;
        }
        out << inlineMarkup(escaped(line.trimmed()));
    }
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out.join(QStringLiteral("<br>"));
}

} // namespace Harpoon
