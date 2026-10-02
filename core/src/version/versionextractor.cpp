#include "version/versionextractor.h"

#include <QRegularExpression>

namespace Harpoon {

Result<QString> extractVersion(const QString &regex, const QString &matchGroup, const QString &input)
{
    if (regex.isEmpty())
        return Result<QString>::success(input);

    const QRegularExpression pattern(regex);
    if (!pattern.isValid())
        return Result<QString>::failure(Error::make(
            Error::InvalidSetting, QStringLiteral("Invalid version extraction regex: %1").arg(pattern.errorString())));

    QRegularExpressionMatch last;
    auto it = pattern.globalMatch(input);
    while (it.hasNext())
        last = it.next();
    if (!last.hasMatch())
        return Result<QString>::failure(Error::make(
            Error::NoVersion, QStringLiteral("Version extraction regex did not match \"%1\"").arg(input)));

    QString tmpl = matchGroup.trimmed();
    if (tmpl.isEmpty())
        tmpl = QStringLiteral("$0");
    else if (QRegularExpression(QStringLiteral("^\\d+$")).match(tmpl).hasMatch())
        tmpl.prepend(QLatin1Char('$'));

    // Replace $N with group N. A backslash escapes the dollar sign.
    static const QRegularExpression groupRef(QStringLiteral("(\\\\*)\\$(\\d+)"));
    QString output;
    int previousEnd = 0;
    bool anyGroup = false;
    auto refs = groupRef.globalMatch(tmpl);
    while (refs.hasNext()) {
        const auto ref = refs.next();
        output += tmpl.mid(previousEnd, ref.capturedStart(0) - previousEnd);
        const QString slashes = ref.captured(1);
        if (slashes.size() % 2 == 1) {
            // Escaped: keep a literal "$N" (dropping one escaping backslash).
            output += slashes.left(slashes.size() - 1) + QLatin1Char('$') + ref.captured(2);
        } else {
            output += slashes.left(slashes.size() / 2);
            output += last.captured(ref.captured(2).toInt());
            anyGroup = true;
        }
        previousEnd = ref.capturedEnd(0);
    }
    output += tmpl.mid(previousEnd);

    if (!anyGroup || output.trimmed().isEmpty())
        return Result<QString>::failure(Error::make(
            Error::NoVersion, QStringLiteral("Version extraction produced an empty version from \"%1\"").arg(input)));
    return Result<QString>::success(output);
}

} // namespace Harpoon
