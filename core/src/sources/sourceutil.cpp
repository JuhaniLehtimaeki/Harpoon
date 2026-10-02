#include "sources/sourceutil.h"

#include "net/ratelimit.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

namespace Harpoon {

Error httpErrorFor(const HttpResponse &response, const QString &sourceName, const QString &notFoundMessage)
{
    if (response.isNetworkError())
        return Error::make(Error::Network, response.networkError);
    if (response.status == 429)
        return rateLimitError(response, QDateTime::currentMSecsSinceEpoch());
    Error e = response.status == 404
                  ? Error::make(Error::NotFound, notFoundMessage)
                  : Error::make(Error::Http, QStringLiteral("%1 returned HTTP %2 %3")
                                                 .arg(sourceName)
                                                 .arg(response.status)
                                                 .arg(response.reasonPhrase)
                                                 .trimmed());
    e.httpStatus = response.status;
    return e;
}

QString resolveUrl(const QString &base, const QString &reference)
{
    const QString ref = reference.trimmed();
    const QUrl refUrl(ref);
    if (refUrl.isValid() && !refUrl.isRelative() && !refUrl.host().isEmpty())
        return ref;
    return QUrl(base).resolved(refUrl).toString(QUrl::FullyEncoded);
}

QString decodeUrl(const QString &url)
{
    return QUrl::fromPercentEncoding(url.toUtf8());
}

QString lastPathSegment(const QString &url)
{
    const QStringList parts = QUrl(url).path(QUrl::FullyDecoded).split(QLatin1Char('/'));
    for (int i = parts.size() - 1; i >= 0; --i)
        if (!parts.at(i).isEmpty())
            return parts.at(i);
    return QString();
}

bool urlPathEndsWithRpm(const QString &url)
{
    return QUrl(url).path().endsWith(QLatin1String(".rpm"), Qt::CaseInsensitive);
}

QString decodeHtmlEntities(const QString &text)
{
    if (!text.contains(QLatin1Char('&')))
        return text;
    static const QRegularExpression entity(QStringLiteral("&(#[0-9]+|#[xX][0-9a-fA-F]+|[a-zA-Z]+);"));
    QString out;
    int last = 0;
    auto it = entity.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1);
        QString replacement;
        if (name.startsWith(QLatin1String("#x")) || name.startsWith(QLatin1String("#X"))) {
            bool ok = false;
            const uint code = name.mid(2).toUInt(&ok, 16);
            if (ok)
                replacement = QString::fromUcs4(&code, 1);
        } else if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint code = name.mid(1).toUInt(&ok, 10);
            if (ok)
                replacement = QString::fromUcs4(&code, 1);
        } else if (name == QLatin1String("amp")) {
            replacement = QStringLiteral("&");
        } else if (name == QLatin1String("lt")) {
            replacement = QStringLiteral("<");
        } else if (name == QLatin1String("gt")) {
            replacement = QStringLiteral(">");
        } else if (name == QLatin1String("quot")) {
            replacement = QStringLiteral("\"");
        } else if (name == QLatin1String("apos")) {
            replacement = QStringLiteral("'");
        } else if (name == QLatin1String("nbsp")) {
            replacement = QStringLiteral(" ");
        }
        out += replacement.isNull() ? m.captured(0) : replacement;
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

QList<HtmlLink> extractAnchorLinks(const QString &html)
{
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"),
                                             QRegularExpression::DotMatchesEverythingOption);
    // The text runs to </a>, the next <a> (unclosed anchors) or the end.
    static const QRegularExpression anchor(QStringLiteral("<a(\\s[^>]*)?>(.*?)(?=</a\\s*>|<a[\\s>]|$)"),
                                           QRegularExpression::CaseInsensitiveOption
                                               | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression href(QStringLiteral("(?:^|\\s)href\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+))"),
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));

    QString doc = html;
    doc.remove(comments);

    QList<HtmlLink> links;
    auto it = anchor.globalMatch(doc);
    while (it.hasNext()) {
        const auto m = it.next();
        const auto h = href.match(m.captured(1));
        if (!h.hasMatch())
            continue;
        HtmlLink link;
        link.url = decodeHtmlEntities(h.captured(1) + h.captured(2) + h.captured(3)).trimmed();
        QString text = m.captured(2);
        text.remove(tags);
        link.text = decodeHtmlEntities(text).replace(spaces, QStringLiteral(" ")).trimmed();
        links << link;
    }
    return links;
}

QStringList extractUrlsInText(const QString &text)
{
    static const QRegularExpression url(QStringLiteral("(?:https?|ftp)://[^\\s\"'<>]+"));
    QStringList out;
    auto it = url.globalMatch(text);
    while (it.hasNext())
        out << it.next().captured(0);
    return out;
}

QString shortSha256(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex().left(12));
}

QDateTime parseRfc822Date(const QString &text)
{
    static const QRegularExpression pattern(
        QStringLiteral("(\\d{1,2})\\s+([A-Za-z]{3})[a-z]*\\s+(\\d{2,4})\\s+(\\d{1,2}):(\\d{2})(?::(\\d{2}))?"
                       "\\s*([+-]\\d{2}:?\\d{2}|[A-Za-z]+)?"));
    const auto m = pattern.match(text.trimmed());
    if (!m.hasMatch())
        return QDateTime();
    static const QStringList months = {"jan", "feb", "mar", "apr", "may", "jun",
                                       "jul", "aug", "sep", "oct", "nov", "dec"};
    const int month = months.indexOf(m.captured(2).toLower()) + 1;
    int year = m.captured(3).toInt();
    if (year < 100)
        year += year < 70 ? 2000 : 1900;
    const QDate date(year, month, m.captured(1).toInt());
    const QTime time(m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt());
    if (month == 0 || !date.isValid() || !time.isValid())
        return QDateTime();

    int offsetSecs = 0;
    QString zone = m.captured(7);
    if (zone.startsWith(QLatin1Char('+')) || zone.startsWith(QLatin1Char('-'))) {
        zone.remove(QLatin1Char(':'));
        const int hhmm = zone.mid(1).toInt();
        offsetSecs = ((hhmm / 100) * 3600 + (hhmm % 100) * 60) * (zone.startsWith(QLatin1Char('-')) ? -1 : 1);
    }
    // Named zones other than GMT/UT/Z are rare in feeds; treat them as UTC.
    return QDateTime(date, time, Qt::UTC).addSecs(-offsetSecs);
}

Result<QList<RssItem>> parseRssItems(const QByteArray &xml)
{
    QList<RssItem> items;
    QXmlStreamReader reader(xml);
    bool inItem = false;
    RssItem item;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isEndElement() && reader.name() == QLatin1String("item") && inItem) {
            items << item;
            inItem = false;
            continue;
        }
        if (!reader.isStartElement())
            continue;
        const QStringRef name = reader.name();
        if (name == QLatin1String("item") && reader.namespaceUri().isEmpty()) {
            item = RssItem();
            inItem = true;
            continue;
        }
        if (!inItem)
            continue;
        if (name == QLatin1String("content") && reader.namespaceUri() == QLatin1String("http://search.yahoo.com/mrss/")) {
            bool ok = false;
            const qint64 size = reader.attributes().value(QLatin1String("filesize")).toString().toLongLong(&ok);
            if (ok)
                item.mediaSize = size;
            continue;
        }
        if (!reader.namespaceUri().isEmpty())
            continue;
        QString *field = name == QLatin1String("title")     ? &item.title
                         : name == QLatin1String("link")    ? &item.link
                         : name == QLatin1String("guid")    ? &item.guid
                         : name == QLatin1String("pubDate") ? &item.pubDate
                                                            : nullptr;
        if (field)
            *field = reader.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
    }
    if (reader.hasError())
        return Result<QList<RssItem>>::failure(
            Error::make(Error::Parse, QStringLiteral("Invalid RSS feed: %1").arg(reader.errorString())));
    return Result<QList<RssItem>>::success(items);
}

} // namespace Harpoon
