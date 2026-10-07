#include "rendering/RenderContext.h"

#include "personalization/TemplateEngine.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QtMath>

namespace occ {

QImage makeRenderTarget(const QSizeF &sizePx, const RenderContext &ctx)
{
    const int w = qMax(1, qCeil(sizePx.width()));
    const int h = qMax(1, qCeil(sizePx.height()));
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    // Setting the physical size is what makes point sized fonts exact: Qt maps
    // a point size through the paint device's logical DPI.
    const int dotsPerMeter = qMax(1, qRound(ctx.pxPerMm() * 1000.0));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    return image;
}

QImage RenderContext::image(const QString &assetId, const ObjectId &owner) const
{
    if (assetId.isEmpty())
        return QImage();
    if (!m_images) {
        addWarning(QCoreApplication::translate(
            "RenderContext", "No image store is available, so an image could not be drawn."),
            owner);
        return QImage();
    }
    if (!m_images->has(assetId)) {
        // Report each missing asset once per render pass.
        if (!m_reportedAssets.contains(assetId)) {
            m_reportedAssets.append(assetId);
            const QString name = m_images->description(assetId);
            addWarning(QCoreApplication::translate(
                           "RenderContext", "The image \"%1\" is missing from the project "
                                            "and could not be drawn.").arg(name),
                       owner);
        }
        return QImage();
    }
    return m_images->image(assetId);
}

QString RenderContext::resolveText(const QString &text, const ObjectId &owner) const
{
    if (!m_placeholders || text.isEmpty())
        return text;
    const TemplateEngine::Result r = TemplateEngine::expandDetailed(text, *m_placeholders);
    for (const QString &missing : r.missingKeys) {
        addWarning(QCoreApplication::translate(
                       "RenderContext", "The placeholder \"{{%1}}\" has no value for this data "
                                        "record and was left unchanged.").arg(missing),
                   owner);
    }
    return r.text;
}

QFont RenderContext::makeFont(double pointSize, const QString &family, bool bold,
                              bool italic, bool underline) const
{
    QFont font;
    if (!family.isEmpty())
        font.setFamily(family);
    font.setPointSizeF(qMax(0.5, pointSize));
    font.setBold(bold);
    font.setItalic(italic);
    font.setUnderline(underline);
    font.setStyleStrategy(QFont::PreferAntialias);
    // Predictable metrics: no font hinting induced advance changes.
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setKerning(true);
    return font;
}

QFontMetricsF RenderContext::metrics(const QFont &font, const QPainter &painter) const
{
    const QPaintDevice *device = painter.device();
    if (device)
        return QFontMetricsF(font, device);
    // Fallback: metrics in device pixels at the context DPI.
    QImage probe(1, 1, QImage::Format_ARGB32_Premultiplied);
    probe.setDotsPerMeterX(qMax(1, qRound(deviceDpi() * 1000.0 / 25.4)));
    probe.setDotsPerMeterY(probe.dotsPerMeterX());
    return QFontMetricsF(font, &probe);
}

bool RenderContext::fontFamilyAvailable(const QString &family, const ObjectId &owner) const
{
    if (family.isEmpty())
        return true;
    const QStringList families = QFontDatabase::families();
    const bool available = families.contains(family, Qt::CaseInsensitive);
    if (!available && !m_reportedFontFamilies.contains(family, Qt::CaseInsensitive)) {
        m_reportedFontFamilies.append(family);
        addWarning(QCoreApplication::translate(
                       "RenderContext", "The font \"%1\" is not installed on this computer. "
                                        "A substitute font was used.").arg(family),
                   owner);
    }
    return available;
}

void RenderContext::addWarning(const QString &message, const ObjectId &objectId) const
{
    for (const Warning &w : m_warnings) {
        if (w.message == message && w.objectId == objectId)
            return; // do not spam the same warning hundreds of times
    }
    m_warnings.append(Warning{ message, objectId });
}

} // namespace occ
