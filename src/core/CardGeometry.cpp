#include "core/CardGeometry.h"

#include "core/Units.h"

#include <QCoreApplication>
#include <QtMath>

#include <cmath>

namespace occ {

CardGeometry::CardGeometry() = default;

QVector<CardPreset> CardGeometry::presets()
{
    // Nominal ISO/IEC 7810 dimensions in millimetres.
    const auto tr = [](const char *s) {
        return QCoreApplication::translate("CardGeometry", s);
    };
    return {
        { QStringLiteral("iso-id1"),          tr("ISO/IEC 7810 ID-1 (CR80) - 85.60 x 53.98 mm"), 85.60, 53.98, false },
        { QStringLiteral("iso-id1-portrait"), tr("ISO/IEC 7810 ID-1 portrait - 53.98 x 85.60 mm"), 53.98, 85.60, false },
        { QStringLiteral("iso-id2"),          tr("ISO/IEC 7810 ID-2 - 105.00 x 74.00 mm"), 105.00, 74.00, false },
        { QStringLiteral("iso-id3"),          tr("ISO/IEC 7810 ID-3 (passport card) - 125.00 x 88.00 mm"), 125.00, 88.00, false },
        { QStringLiteral("cr79"),             tr("CR79 - 83.80 x 51.00 mm"), 83.80, 51.00, false },
        { QStringLiteral("iso-id1-bleed"),    tr("CR80 with 0.5 mm bleed - 85.60 x 53.98 mm"), 85.60, 53.98, false },
        { QStringLiteral("custom"),           tr("Custom size"), 85.60, 53.98, true },
    };
}

CardPreset CardGeometry::preset(const QString &id)
{
    const QVector<CardPreset> all = presets();
    for (const CardPreset &p : all) {
        if (p.id == id)
            return p;
    }
    return all.first();
}

QString CardGeometry::matchingPresetId(double widthMm, double heightMm)
{
    const QVector<CardPreset> all = presets();
    for (const CardPreset &p : all) {
        if (p.isCustom)
            continue;
        if (qAbs(p.widthMm - widthMm) < 0.005 && qAbs(p.heightMm - heightMm) < 0.005)
            return p.id;
    }
    return QStringLiteral("custom");
}

void CardGeometry::setWidthMm(double mm)
{
    m_widthMm = units::canonicalMm(qBound(kMinCardMm, mm, kMaxCardMm));
}

void CardGeometry::setHeightMm(double mm)
{
    m_heightMm = units::canonicalMm(qBound(kMinCardMm, mm, kMaxCardMm));
}

void CardGeometry::setSize(double widthMm, double heightMm)
{
    setWidthMm(widthMm);
    setHeightMm(heightMm);
    m_presetId = matchingPresetId(m_widthMm, m_heightMm);
}

void CardGeometry::setOrientation(bool landscape)
{
    if (isLandscape() == landscape)
        return;
    const double w = m_widthMm;
    m_widthMm = m_heightMm;
    m_heightMm = w;
    m_presetId = matchingPresetId(m_widthMm, m_heightMm);
}

void CardGeometry::setRenderDpi(int dpi)
{
    m_renderDpi = qBound(kMinDpi, dpi, kMaxDpi);
}

void CardGeometry::setBleedMm(double mm)
{
    m_bleedMm = units::canonicalMm(qBound(0.0, mm, 10.0));
}

double CardGeometry::aspectRatio() const
{
    return m_heightMm <= 0.0 ? 1.0 : m_widthMm / m_heightMm;
}

QSize CardGeometry::pixelSize() const
{
    return pixelSize(m_renderDpi);
}

QSize CardGeometry::pixelSize(int dpi) const
{
    const int w = qMax(1, qRound(units::mmToPx(m_widthMm, dpi)));
    const int h = qMax(1, qRound(units::mmToPx(m_heightMm, dpi)));
    return QSize(w, h);
}

QRectF CardGeometry::boundsMm() const
{
    return QRectF(0.0, 0.0, m_widthMm, m_heightMm);
}

QRectF CardGeometry::bleedBoundsMm() const
{
    if (m_bleedMm <= 0.0)
        return boundsMm();
    return QRectF(-m_bleedMm, -m_bleedMm,
                  m_widthMm + 2 * m_bleedMm, m_heightMm + 2 * m_bleedMm);
}

QSizeF CardGeometry::printableSizePx() const
{
    const double pxPerMm = units::pxPerMmFromDpi(m_renderDpi);
    const QRectF r = bleedBoundsMm();
    return QSizeF(qMax(1.0, r.width() * pxPerMm), qMax(1.0, r.height() * pxPerMm));
}


bool CardGeometry::isValid(QString *error) const
{
    const auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    //: %1 and %2 are numeric limits in millimetres.
    const QString range = QCoreApplication::translate(
        "CardGeometry", "Card dimensions must be between %1 and %2 mm.")
        .arg(kMinCardMm).arg(kMaxCardMm);
    if (m_widthMm < kMinCardMm || m_widthMm > kMaxCardMm)
        return fail(range);
    if (m_heightMm < kMinCardMm || m_heightMm > kMaxCardMm)
        return fail(range);
    if (m_renderDpi < kMinDpi || m_renderDpi > kMaxDpi) {
        return fail(QCoreApplication::translate(
            "CardGeometry", "Render resolution must be between %1 and %2 dpi.")
            .arg(kMinDpi).arg(kMaxDpi));
    }
    if (m_bleedMm < 0.0 || m_bleedMm > 10.0) {
        return fail(QCoreApplication::translate(
            "CardGeometry", "Bleed must be between 0 and 10 mm."));
    }
    if (error)
        error->clear();
    return true;
}

QJsonObject CardGeometry::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("preset"), m_presetId);
    o.insert(QStringLiteral("widthMm"), m_widthMm);
    o.insert(QStringLiteral("heightMm"), m_heightMm);
    o.insert(QStringLiteral("renderDpi"), m_renderDpi);
    o.insert(QStringLiteral("bleedMm"), m_bleedMm);
    return o;
}

bool CardGeometry::fromJson(const QJsonObject &json, QString *error)
{
    // Out-of-range values are rejected rather than clamped so that a corrupt
    // project can never silently produce a different card size.
    if (json.contains(QStringLiteral("preset")))
        m_presetId = json.value(QStringLiteral("preset")).toString(m_presetId);
    m_widthMm   = json.value(QStringLiteral("widthMm")).toDouble(m_widthMm);
    m_heightMm  = json.value(QStringLiteral("heightMm")).toDouble(m_heightMm);
    m_renderDpi = json.value(QStringLiteral("renderDpi")).toInt(m_renderDpi);
    m_bleedMm   = json.value(QStringLiteral("bleedMm")).toDouble(m_bleedMm);

    if (!std::isfinite(m_widthMm) || !std::isfinite(m_heightMm) || !std::isfinite(m_bleedMm)) {
        if (error) {
            *error = QCoreApplication::translate(
                "CardGeometry", "The card size stored in this project is not a valid number.");
        }
        return false;
    }
    if (m_presetId.isEmpty())
        m_presetId = matchingPresetId(m_widthMm, m_heightMm);
    if (m_presetId == QStringLiteral("iso-id1-bleed") && m_bleedMm <= 0.0)
        m_bleedMm = 0.5;
    return isValid(error);
}

CardGeometry CardGeometry::defaultGeometry()
{
    return isoId1();
}

CardGeometry CardGeometry::isoId1()
{
    CardGeometry g;
    g.setSize(85.60, 53.98);
    g.setRenderDpi(300);
    g.setPresetId(QStringLiteral("iso-id1"));
    return g;
}

CardGeometry CardGeometry::isoId1Portrait()
{
    CardGeometry g;
    g.setSize(53.98, 85.60);
    g.setRenderDpi(300);
    g.setPresetId(QStringLiteral("iso-id1-portrait"));
    return g;
}

} // namespace occ
