#include "app/addlink.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace Harpoon {

namespace {

Result<AddLink> invalid(const QString &why)
{
    return Result<AddLink>::failure(Error::make(Error::InvalidUrl, why));
}

bool isWebUrl(const QUrl &url)
{
    return url.isValid() && !url.host().isEmpty()
           && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http"));
}

} // namespace

QString AddLink::toString() const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("url"), QString::fromLatin1(QUrl::toPercentEncoding(url)));
    if (!sourceId.isEmpty())
        query.addQueryItem(QStringLiteral("source"), sourceId);
    if (!packageName.isEmpty())
        query.addQueryItem(QStringLiteral("package"), packageName);
    return QStringLiteral("harpoon://add?") + query.toString(QUrl::FullyEncoded);
}

Result<AddLink> parseAddLink(const QString &input)
{
    const QString text = input.trimmed();
    if (text.isEmpty())
        return invalid(QStringLiteral("Nothing to add"));

    const QUrl url(text, QUrl::StrictMode);
    if (isWebUrl(url)) {
        AddLink link;
        link.url = text;
        return Result<AddLink>::success(link);
    }
    if (url.scheme() != QLatin1String("harpoon"))
        return invalid(QStringLiteral("Not a Harpoon link or a web address"));
    if (url.host() != QLatin1String("add"))
        return invalid(QStringLiteral("Unknown Harpoon link: %1").arg(url.host()));

    const QUrlQuery query(url);
    AddLink link;
    link.url = query.queryItemValue(QStringLiteral("url"), QUrl::FullyDecoded).trimmed();
    link.sourceId = query.queryItemValue(QStringLiteral("source"), QUrl::FullyDecoded).trimmed();
    link.packageName = query.queryItemValue(QStringLiteral("package"), QUrl::FullyDecoded).trimmed();

    if (!isWebUrl(QUrl(link.url, QUrl::StrictMode)))
        return invalid(QStringLiteral("The link does not contain a web address"));
    static const QRegularExpression sourcePattern(QStringLiteral("^[A-Za-z][A-Za-z0-9]{0,31}$"));
    if (!link.sourceId.isEmpty() && !sourcePattern.match(link.sourceId).hasMatch())
        return invalid(QStringLiteral("Invalid source in link"));
    static const QRegularExpression packagePattern(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]{0,127}$"));
    if (!link.packageName.isEmpty() && !packagePattern.match(link.packageName).hasMatch())
        return invalid(QStringLiteral("Invalid package name in link"));
    return Result<AddLink>::success(link);
}

} // namespace Harpoon
