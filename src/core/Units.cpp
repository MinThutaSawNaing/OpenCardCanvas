#include "core/Units.h"

#include <QRegularExpression>
#include <QtMath>

namespace occ {
namespace units {

namespace {

const QRegularExpression &lengthRe()
{
    static const QRegularExpression re(
        QStringLiteral(R"(^\s*([+-]?\d+(?:[.,]\d+)?)\s*(mm|millimet(?:er|re)s?|cm|in|inch(?:es)?|"|mil|thou)?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

QString formatLengthNumber(double mm, DisplayUnit unit, int decimals)
{
    const double value = (unit == DisplayUnit::Inches) ? mmToInch(mm) : mm;
    return QString::number(value, 'f', qBound(0, decimals, 6));
}

QString formatLength(double mm, DisplayUnit unit, int decimals, bool withSuffix)
{
    const QString number = formatLengthNumber(mm, unit, decimals);
    if (!withSuffix)
        return number;
    return number + QLatin1Char(' ') + unitSuffix(unit);
}

double parseLength(const QString &text, DisplayUnit fallbackUnit, bool *ok)
{
    if (ok)
        *ok = false;

    const QRegularExpressionMatch m = lengthRe().match(text);
    if (!m.hasMatch())
        return 0.0;

    QString numberText = m.captured(1);
    numberText.replace(QLatin1Char(','), QLatin1Char('.'));
    bool numberOk = false;
    double value = numberText.toDouble(&numberOk);
    if (!numberOk)
        return 0.0;

    const QString suffix = m.captured(2).toLower();
    double millimetres = 0.0;
    if (suffix.isEmpty()) {
        millimetres = (fallbackUnit == DisplayUnit::Inches) ? inchToMm(value) : value;
    } else if (suffix == QLatin1String("mm") || suffix.startsWith(QLatin1String("millimet"))) {
        millimetres = value;
    } else if (suffix == QLatin1String("cm")) {
        millimetres = value * 10.0;
    } else if (suffix == QLatin1String("in") || suffix.startsWith(QLatin1String("inch"))
               || suffix == QLatin1String("\"")) {
        millimetres = inchToMm(value);
    } else if (suffix == QLatin1String("mil") || suffix == QLatin1String("thou")) {
        millimetres = inchToMm(value / 1000.0);
    } else {
        return 0.0;
    }

    if (ok)
        *ok = true;
    return millimetres;
}

QString unitSuffix(DisplayUnit unit)
{
    return unit == DisplayUnit::Inches ? QStringLiteral("in") : QStringLiteral("mm");
}

int parseResolutionDpi(const QString &resolution, int fallback)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*(\d+)\s*x\s*(\d+)\s*$)"),
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(resolution);
    if (!m.hasMatch())
        return fallback;
    bool ok = false;
    const int dpi = m.captured(1).toInt(&ok);
    if (!ok || dpi < 72 || dpi > 2400)
        return fallback;
    return dpi;
}

} // namespace units
} // namespace occ
