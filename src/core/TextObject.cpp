#include "core/TextObject.h"

#include "rendering/RenderContext.h"
#include "rendering/TextLayout.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

namespace occ {

namespace {

constexpr double kMinFontPt = 1.0;
constexpr double kMaxFontPt = 400.0;

QColor colorFromJson(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isString())
        return fallback;
    const QColor c(value.toString());
    return c.isValid() ? c : fallback;
}

} // namespace

TextObject::TextObject() : CardObject(ObjectType::Text, ObjectId::createUuid())
{
    setRectMm(QRectF(5.0, 5.0, 40.0, 8.0));
    m_name = defaultName();
}

void TextObject::setFontSizePt(double pt)
{
    if (!std::isfinite(pt))
        return;
    m_fontSizePt = qBound(kMinFontPt, pt, kMaxFontPt);
}

void TextObject::setLineSpacingPercent(double percent)
{
    if (!std::isfinite(percent))
        return;
    m_lineSpacingPercent = qBound(10.0, percent, 1000.0);
}

void TextObject::setLetterSpacingMm(double mm)
{
    if (!std::isfinite(mm))
        return;
    m_letterSpacingMm = qBound(-2.0, mm, 20.0);
}

void TextObject::setOutlineWidthMm(double mm)
{
    if (!std::isfinite(mm))
        return;
    m_outlineWidthMm = qBound(0.0, mm, 5.0);
}

QString TextObject::resolvedText(const RenderContext &ctx) const
{
    return ctx.resolveText(m_text, id());
}

void TextObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    const double boxW = widthMm();
    const double boxH = heightMm();
    if (boxW <= 0.0 || boxH <= 0.0)
        return;

    const QString content = resolvedText(ctx);
    if (content.isEmpty())
        return;

    // Missing fonts are reported once per render pass rather than silently
    // substituting without telling the user.
    ctx.fontFamilyAvailable(m_fontFamily, id());

    TextLayout::Params params;
    params.boxWidthMm = boxW;
    params.hAlign = m_hAlign;
    params.wordWrap = m_wordWrap;
    params.letterSpacingMm = m_letterSpacingMm;
    params.lineSpacingPercent = m_lineSpacingPercent;

    double usedPt = m_fontSizePt;
    TextLayoutResult layout =
        TextLayout::layout(content, usedPt, m_fontFamily, m_bold, m_italic, params);

    if (m_autoShrink) {
        // Reduce the size until the block fits, never below 40 % of the request.
        const double floorPt = m_fontSizePt * 0.4;
        int guard = 0;
        while (layout.totalHeightMm > boxH + 1e-6 && usedPt > floorPt && guard++ < 80) {
            usedPt = qMax(floorPt, usedPt - qMax(0.25, usedPt * 0.05));
            layout = TextLayout::layout(content, usedPt, m_fontFamily, m_bold, m_italic, params);
        }
    }

    if (layout.totalHeightMm > boxH + 1e-6) {
        ctx.addWarning(QCoreApplication::translate(
                           "TextObject",
                           "The text does not fit inside its box and was clipped. "
                           "Enlarge the text box or reduce the font size."),
                       id());
    }

    painter.save();
    painter.setClipRect(QRectF(0.0, 0.0, boxW, boxH));

    double y = 0.0;
    switch (m_vAlign) {
    case VerticalAlign::Top:    y = 0.0; break;
    case VerticalAlign::Middle: y = qMax(0.0, (boxH - layout.totalHeightMm) / 2.0); break;
    case VerticalAlign::Bottom: y = qMax(0.0, boxH - layout.totalHeightMm); break;
    }

    QPen outlinePen(m_outlineColor);
    outlinePen.setWidthF(qMax(0.01, m_outlineWidthMm));
    outlinePen.setJoinStyle(Qt::RoundJoin);
    const bool drawOutline = m_outlineWidthMm > 0.0 && m_outlineColor.alpha() > 0;
    const double underlineThickness = qMax(0.05, layout.ascentMm * 0.06);

    for (const TextLine &line : layout.lines) {
        double x = 0.0;
        switch (m_hAlign) {
        case HorizontalAlign::Left:
        case HorizontalAlign::Justify:
            x = 0.0;
            break;
        case HorizontalAlign::Center:
            x = (boxW - line.widthMm) / 2.0;
            break;
        case HorizontalAlign::Right:
            x = boxW - line.widthMm;
            break;
        }

        const double baseline = y + layout.ascentMm;

        for (const TextSegment &segment : line.segments) {
            if (segment.text.isEmpty())
                continue;
            const QPainterPath path =
                TextLayout::outline(segment.text, usedPt, m_fontFamily, m_bold, m_italic)
                    .translated(x + segment.xMm, baseline);
            if (drawOutline)
                painter.strokePath(path, outlinePen);
            painter.fillPath(path, m_color);
        }

        if (m_underline && line.widthMm > 0.0) {
            const double underlineY = baseline + layout.descentMm * 0.35;
            painter.fillRect(QRectF(x, underlineY, line.widthMm, underlineThickness), m_color);
        }

        y += layout.lineAdvanceMm;
    }

    painter.restore();
}

QJsonObject TextObject::propertiesToJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("text"), m_text);
    o.insert(QStringLiteral("fontFamily"), m_fontFamily);
    o.insert(QStringLiteral("fontSizePt"), m_fontSizePt);
    o.insert(QStringLiteral("bold"), m_bold);
    o.insert(QStringLiteral("italic"), m_italic);
    o.insert(QStringLiteral("underline"), m_underline);
    o.insert(QStringLiteral("hAlign"), names::horizontalAlign(m_hAlign));
    o.insert(QStringLiteral("vAlign"), names::verticalAlign(m_vAlign));
    o.insert(QStringLiteral("lineSpacingPercent"), m_lineSpacingPercent);
    o.insert(QStringLiteral("letterSpacingMm"), m_letterSpacingMm);
    o.insert(QStringLiteral("wordWrap"), m_wordWrap);
    o.insert(QStringLiteral("autoShrink"), m_autoShrink);
    o.insert(QStringLiteral("color"), m_color.name(QColor::HexArgb));
    o.insert(QStringLiteral("outlineColor"), m_outlineColor.name(QColor::HexArgb));
    o.insert(QStringLiteral("outlineWidthMm"), m_outlineWidthMm);
    return o;
}

bool TextObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    const auto tr = [](const char *s) {
        return QCoreApplication::translate("TextObject", s);
    };

    m_text = json.value(QStringLiteral("text")).toString(m_text);

    const QString family = json.value(QStringLiteral("fontFamily")).toString();
    m_fontFamily = family.isEmpty() ? m_fontFamily : family;

    // Reject nonsensical font sizes instead of drawing unusable text.
    const QJsonValue sizeValue = json.value(QStringLiteral("fontSizePt"));
    if (sizeValue.isDouble()) {
        const double pt = sizeValue.toDouble();
        if (!std::isfinite(pt) || pt < kMinFontPt || pt > kMaxFontPt) {
            if (error) {
                *error = tr("The font size of a text object is outside the supported range.");
            }
            return false;
        }
        m_fontSizePt = pt;
    }

    m_bold      = json.value(QStringLiteral("bold")).toBool(m_bold);
    m_italic    = json.value(QStringLiteral("italic")).toBool(m_italic);
    m_underline = json.value(QStringLiteral("underline")).toBool(m_underline);

    HorizontalAlign h = m_hAlign;
    if (names::horizontalAlignFromString(json.value(QStringLiteral("hAlign")).toString(), &h))
        m_hAlign = h;
    VerticalAlign v = m_vAlign;
    if (names::verticalAlignFromString(json.value(QStringLiteral("vAlign")).toString(), &v))
        m_vAlign = v;

    setLineSpacingPercent(json.value(QStringLiteral("lineSpacingPercent")).toDouble(m_lineSpacingPercent));
    setLetterSpacingMm(json.value(QStringLiteral("letterSpacingMm")).toDouble(m_letterSpacingMm));
    m_wordWrap   = json.value(QStringLiteral("wordWrap")).toBool(m_wordWrap);
    m_autoShrink = json.value(QStringLiteral("autoShrink")).toBool(m_autoShrink);

    m_color = colorFromJson(json.value(QStringLiteral("color")), m_color);
    m_outlineColor = colorFromJson(json.value(QStringLiteral("outlineColor")), m_outlineColor);
    setOutlineWidthMm(json.value(QStringLiteral("outlineWidthMm")).toDouble(m_outlineWidthMm));
    return true;
}

QString TextObject::defaultName() const
{
    return QCoreApplication::translate("TextObject", "Text");
}

CardObjectPtr TextObject::cloneImpl() const
{
    auto copy = std::make_unique<TextObject>();
    copy->m_text = m_text;
    copy->m_fontFamily = m_fontFamily;
    copy->m_fontSizePt = m_fontSizePt;
    copy->m_bold = m_bold;
    copy->m_italic = m_italic;
    copy->m_underline = m_underline;
    copy->m_hAlign = m_hAlign;
    copy->m_vAlign = m_vAlign;
    copy->m_lineSpacingPercent = m_lineSpacingPercent;
    copy->m_letterSpacingMm = m_letterSpacingMm;
    copy->m_wordWrap = m_wordWrap;
    copy->m_autoShrink = m_autoShrink;
    copy->m_color = m_color;
    copy->m_outlineColor = m_outlineColor;
    copy->m_outlineWidthMm = m_outlineWidthMm;
    return copy;
}

} // namespace occ
