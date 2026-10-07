#include "utils/TextUtils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>

namespace occ {
namespace text {

namespace {

QString textTr(const char *text)
{
    return QCoreApplication::translate("TextUtils", text);
}

bool isDigit(QChar c)
{
    return c.isDigit();
}

// Advances past a run of digits and returns the numeric value. Leading zeros are
// ignored for the comparison, which is what makes "Layer 007" sort next to
// "Layer 7" instead of before it.
quint64 readNumber(const QString &s, int *index)
{
    quint64 value = 0;
    bool overflow = false;
    int i = *index;
    while (i < s.size() && isDigit(s.at(i))) {
        const quint64 digit = quint64(s.at(i).unicode() - u'0');
        if (value > (std::numeric_limits<quint64>::max() - digit) / 10)
            overflow = true;
        else if (!overflow)
            value = value * 10 + digit;
        ++i;
    }
    if (overflow)
        value = std::numeric_limits<quint64>::max();
    *index = i;
    return value;
}

// Item 2, 3, ... of a name, so that a thousand-card batch does not stop on the
// second card because a file with that name already exists.
QString suffixed(const QString &base, const QString &extension, int number)
{
    return QStringLiteral("%1 (%2)%3").arg(base).arg(number).arg(extension);
}

const QStringList &reservedDeviceNames()
{
    static const QStringList names = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9"),
    };
    return names;
}

} // namespace

int naturalCompare(const QString &left, const QString &right)
{
    int li = 0;
    int ri = 0;
    const int ls = left.size();
    const int rs = right.size();

    while (li < ls && ri < rs) {
        const QChar lc = left.at(li);
        const QChar rc = right.at(ri);

        if (isDigit(lc) && isDigit(rc)) {
            const quint64 ln = readNumber(left, &li);
            const quint64 rn = readNumber(right, &ri);
            if (ln != rn)
                return ln < rn ? -1 : 1;
            continue;
        }

        const QChar lf = lc.toCaseFolded();
        const QChar rf = rc.toCaseFolded();
        if (lf != rf)
            return lf < rf ? -1 : 1;
        ++li;
        ++ri;
    }

    if (li < ls)
        return 1;
    if (ri < rs)
        return -1;
    return 0;
}

bool naturalLessThan(const QString &a, const QString &b)
{
    return naturalCompare(a, b) < 0;
}

QStringList naturallySorted(const QStringList &values)
{
    QStringList sorted = values;
    std::stable_sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return naturalCompare(a, b) < 0;
    });
    return sorted;
}

QString elideMiddle(const QString &text, int maxCharacters, const QString &ellipsis)
{
    if (maxCharacters <= 0)
        return QString();
    if (text.size() <= maxCharacters)
        return text;

    // Not even room for the ellipsis plus one character on each side.
    if (maxCharacters <= ellipsis.size()) {
        const int take = std::max(0, maxCharacters);
        return text.left(take);
    }

    const int keep = maxCharacters - ellipsis.size();
    const int leftCount = (keep + 1) / 2;
    const int rightCount = keep - leftCount;

    // Never split a surrogate pair: a lone half shows up as a replacement
    // glyph, which looks like data corruption in a layer list.
    int leftEnd = leftCount;
    if (leftEnd > 0 && leftEnd < text.size() && text.at(leftEnd).isLowSurrogate()
        && text.at(leftEnd - 1).isHighSurrogate()) {
        --leftEnd;
    }
    int rightStart = text.size() - rightCount;
    if (rightStart > 0 && rightStart < text.size()
        && text.at(rightStart).isLowSurrogate()
        && text.at(rightStart - 1).isHighSurrogate()) {
        --rightStart;
    }
    rightStart = std::max(leftEnd, rightStart);

    return text.left(leftEnd) + ellipsis + text.mid(rightStart);
}

QString formatBytes(qint64 bytes)
{
    const bool negative = bytes < 0;
    double value = double(negative ? -bytes : bytes);
    const char *unit = "B";
    const char *const units[] = { "B", "KB", "MB", "GB", "TB" };
    int index = 0;
    while (value >= 1024.0 && index < 4) {
        value /= 1024.0;
        ++index;
    }
    unit = units[index];
    // Bytes are never fractional, everything above a kilobyte gets one decimal.
    const QString number = index == 0 ? QString::number(qint64(value))
                                      : QString::number(value, 'f', 1);
    const QString text = QStringLiteral("%1 %2").arg(number, QString::fromLatin1(unit));
    return negative ? QStringLiteral("-") + text : text;
}

QString formatCount(qint64 count)
{
    QLocale locale = QLocale::system();
    return locale.toString(count);
}

QString sanitiseFileName(const QString &name, const QString &fallback)
{
    QString cleaned;
    cleaned.reserve(name.size());
    for (const QChar c : name) {
        const ushort u = c.unicode();
        // Windows forbids < > : " / \ | ? * and every control character.
        if (u < 0x20 || u == u'<' || u == u'>' || u == u':' || u == u'"'
            || u == u'/' || u == u'\\' || u == u'|' || u == u'?' || u == u'*') {
            cleaned.append(QLatin1Char('_'));
        } else {
            cleaned.append(c);
        }
    }

    // Windows also strips trailing dots and spaces, so removing them here keeps
    // "the name we show" and "the name on disk" identical.
    while (!cleaned.isEmpty()
           && (cleaned.endsWith(QLatin1Char('.')) || cleaned.endsWith(QLatin1Char(' ')))) {
        cleaned.chop(1);
    }
    cleaned = cleaned.trimmed();

    if (cleaned.isEmpty())
        cleaned = fallback;
    if (cleaned.isEmpty())
        cleaned = QStringLiteral("file");
    if (cleaned.size() > 120)
        cleaned = cleaned.left(120);
    while (!cleaned.isEmpty()
           && (cleaned.endsWith(QLatin1Char('.')) || cleaned.endsWith(QLatin1Char(' ')))) {
        cleaned.chop(1);
    }
    if (cleaned.isEmpty())
        cleaned = QStringLiteral("file");
    return cleaned;
}

bool isSafeFileName(const QString &name)
{
    if (name.isEmpty())
        return false;
    if (name == QStringLiteral(".") || name == QStringLiteral(".."))
        return false;
    for (const QChar c : name) {
        const ushort u = c.unicode();
        if (u < 0x20 || u == u'<' || u == u'>' || u == u':' || u == u'"'
            || u == u'/' || u == u'\\' || u == u'|' || u == u'?' || u == u'*') {
            return false;
        }
    }
    if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        return false;

    // A device name is a legal looking file name that Windows cannot create.
    const QString stem = name.section(QLatin1Char('.'), 0, 0).toUpper();
    if (reservedDeviceNames().contains(stem))
        return false;
    return true;
}

QString uniqueFileName(const QString &directory, const QString &desiredName)
{
    const QString safe = sanitiseFileName(desiredName, QStringLiteral("file"));
    const QFileInfo info(safe);
    QString base = info.completeBaseName();
    QString extension = info.suffix();
    if (!extension.isEmpty())
        extension.prepend(QLatin1Char('.'));
    if (base.isEmpty())
        base = QStringLiteral("file");

    const QDir dir(directory);
    const auto exists = [&dir](const QString &fileName) {
        return dir.exists(fileName);
    };

    QString candidate = base + extension;
    if (!exists(candidate))
        return candidate;

    for (int i = 2; i < 100000; ++i) {
        candidate = suffixed(base, extension, i);
        if (!exists(candidate))
            return candidate;
    }
    // Absurdly unlikely; a timestamp keeps the function total.
    return suffixed(base,
                    extension,
                    int(QDateTime::currentMSecsSinceEpoch() % 1000000));
}

QStringList keysNotIn(const QStringList &keys, const QStringList &known)
{
    QSet<QString> knownFolded;
    for (const QString &k : known)
        knownFolded.insert(k.toCaseFolded());

    QStringList missing;
    for (const QString &k : keys) {
        if (!knownFolded.contains(k.toCaseFolded()))
            missing.append(k);
    }
    return missing;
}

QString joinedForHumans(const QStringList &values, int maxItems)
{
    if (values.isEmpty())
        return QString();
    if (maxItems < 2)
        maxItems = 2;

    QStringList shown = values;
    int hidden = 0;
    if (shown.size() > maxItems) {
        hidden = shown.size() - maxItems;
        shown = shown.mid(0, maxItems);
    }

    QString result;
    if (shown.size() == 1) {
        result = shown.first();
    } else {
        const QString last = shown.takeLast();
        result = shown.join(textTr(", "));
        result += textTr(" and ") + last;
    }

    if (hidden > 0) {
        result += textTr(" and %1 more").arg(hidden);
    }
    return result;
}

} // namespace text
} // namespace occ
