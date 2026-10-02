#include "version/rpmversion.h"

namespace Harpoon {

namespace {

// rpm uses ASCII-only character classes (risdigit/risalpha).
bool isDigit(QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }
bool isAlpha(QChar c)
{
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'));
}
bool isAlnum(QChar c) { return isDigit(c) || isAlpha(c); }

} // namespace

QString Evr::toString() const
{
    QString out;
    if (epoch != 0)
        out = QString::number(epoch) + QLatin1Char(':');
    out += version;
    if (!release.isEmpty())
        out += QLatin1Char('-') + release;
    return out;
}

Evr parseEvr(const QString &evr)
{
    Evr out;
    QString rest = evr.trimmed();
    const int colon = rest.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        bool ok = false;
        const int epoch = rest.left(colon).toInt(&ok);
        out.epoch = ok ? epoch : 0;
        rest = rest.mid(colon + 1);
    }
    const int dash = rest.lastIndexOf(QLatin1Char('-'));
    if (dash >= 0) {
        out.version = rest.left(dash);
        out.release = rest.mid(dash + 1);
    } else {
        out.version = rest;
    }
    return out;
}

int rpmVerCmp(const QString &a, const QString &b)
{
    if (a == b)
        return 0;

    int i = 0;
    int j = 0;
    const int n = a.size();
    const int m = b.size();
    auto at = [](const QString &s, int k) { return k < s.size() ? s.at(k) : QChar(); };
    const QChar tilde(QLatin1Char('~'));
    const QChar caret(QLatin1Char('^'));

    while (i < n || j < m) {
        while (i < n && !isAlnum(a.at(i)) && a.at(i) != tilde && a.at(i) != caret)
            ++i;
        while (j < m && !isAlnum(b.at(j)) && b.at(j) != tilde && b.at(j) != caret)
            ++j;

        // '~' sorts before everything, even the end of the string.
        if (at(a, i) == tilde || at(b, j) == tilde) {
            if (at(a, i) != tilde)
                return 1;
            if (at(b, j) != tilde)
                return -1;
            ++i;
            ++j;
            continue;
        }
        // '^' sorts after the end of the string but before any other segment.
        if (at(a, i) == caret || at(b, j) == caret) {
            if (i >= n)
                return -1;
            if (j >= m)
                return 1;
            if (at(a, i) != caret)
                return 1;
            if (at(b, j) != caret)
                return -1;
            ++i;
            ++j;
            continue;
        }

        if (i >= n || j >= m)
            break;

        int ei = i;
        int ej = j;
        bool isNum;
        if (isDigit(a.at(i))) {
            while (ei < n && isDigit(a.at(ei)))
                ++ei;
            while (ej < m && isDigit(b.at(ej)))
                ++ej;
            isNum = true;
        } else {
            while (ei < n && isAlpha(a.at(ei)))
                ++ei;
            while (ej < m && isAlpha(b.at(ej)))
                ++ej;
            isNum = false;
        }

        // Segments of different types: numeric is newer.
        if (ej == j)
            return isNum ? 1 : -1;

        QString segA = a.mid(i, ei - i);
        QString segB = b.mid(j, ej - j);
        if (isNum) {
            // Leading zeros are insignificant ("0" and "00" both become "").
            while (segA.startsWith(QLatin1Char('0')))
                segA.remove(0, 1);
            while (segB.startsWith(QLatin1Char('0')))
                segB.remove(0, 1);
            if (segA.size() != segB.size())
                return segA.size() > segB.size() ? 1 : -1;
        }
        const int c = QString::compare(segA, segB, Qt::CaseSensitive);
        if (c != 0)
            return c < 0 ? -1 : 1;

        i = ei;
        j = ej;
    }

    if (i >= n && j >= m)
        return 0;
    return i >= n ? -1 : 1;
}

int compareEvr(const Evr &a, const Evr &b)
{
    if (a.epoch != b.epoch)
        return a.epoch < b.epoch ? -1 : 1;
    const int v = rpmVerCmp(a.version, b.version);
    if (v != 0)
        return v;
    if (a.release.isEmpty() || b.release.isEmpty())
        return 0;
    return rpmVerCmp(a.release, b.release);
}

} // namespace Harpoon
