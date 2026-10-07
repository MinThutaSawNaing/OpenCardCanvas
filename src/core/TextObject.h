#pragma once

#include "core/CardObject.h"

#include <QColor>
#include <QFont>

namespace occ {

// ---------------------------------------------------------------------------
// TextObject - a real, layout aware text block.
//
// The same layout code produces the editor glyphs, the preview, the PNG/PDF
// export and the printer output, so text can never re-flow between them.
//
// Supported: font family, point size, bold/italic/underline, horizontal and
// vertical alignment, line spacing, letter spacing, multi line text, word wrap
// and optional auto-shrink so a long value never silently overflows its box.
// ---------------------------------------------------------------------------
class TextObject : public CardObject
{
public:
    TextObject();

    // --- content ------------------------------------------------------------
    QString text() const { return m_text; }
    void setText(const QString &text) { m_text = text; }

    // --- font ---------------------------------------------------------------
    QString fontFamily() const { return m_fontFamily; }
    void setFontFamily(const QString &family) { m_fontFamily = family; }

    double fontSizePt() const { return m_fontSizePt; }
    void setFontSizePt(double pt);

    bool bold() const { return m_bold; }
    void setBold(bool on) { m_bold = on; }
    bool italic() const { return m_italic; }
    void setItalic(bool on) { m_italic = on; }
    bool underline() const { return m_underline; }
    void setUnderline(bool on) { m_underline = on; }

    // --- layout -------------------------------------------------------------
    HorizontalAlign horizontalAlign() const { return m_hAlign; }
    void setHorizontalAlign(HorizontalAlign a) { m_hAlign = a; }
    VerticalAlign verticalAlign() const { return m_vAlign; }
    void setVerticalAlign(VerticalAlign a) { m_vAlign = a; }

    // 100 % equals the font's natural line height.
    double lineSpacingPercent() const { return m_lineSpacingPercent; }
    void setLineSpacingPercent(double percent);

    // Extra tracking added between characters, in millimetres.
    double letterSpacingMm() const { return m_letterSpacingMm; }
    void setLetterSpacingMm(double mm);

    bool wordWrap() const { return m_wordWrap; }
    void setWordWrap(bool on) { m_wordWrap = on; }

    // When set, the text is reduced (down to 40 % of the requested size) so it
    // always fits inside its box. The requested size is preserved.
    bool autoShrink() const { return m_autoShrink; }
    void setAutoShrink(bool on) { m_autoShrink = on; }

    // --- colour -------------------------------------------------------------
    QColor color() const { return m_color; }
    void setColor(const QColor &color) { m_color = color; }

    QColor outlineColor() const { return m_outlineColor; }
    void setOutlineColor(const QColor &color) { m_outlineColor = color; }
    double outlineWidthMm() const { return m_outlineWidthMm; }
    void setOutlineWidthMm(double mm);

    // --- derived ------------------------------------------------------------
    // The text after placeholder expansion for the current render pass.
    QString resolvedText(const RenderContext &ctx) const;

protected:
    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

private:
    QString         m_text;
    QString         m_fontFamily = QStringLiteral("Arial");
    double          m_fontSizePt = 10.0;
    bool            m_bold = false;
    bool            m_italic = false;
    bool            m_underline = false;
    HorizontalAlign m_hAlign = HorizontalAlign::Left;
    VerticalAlign   m_vAlign = VerticalAlign::Top;
    double          m_lineSpacingPercent = 100.0;
    double          m_letterSpacingMm = 0.0;
    bool            m_wordWrap = true;
    bool            m_autoShrink = false;
    QColor          m_color = QColor(Qt::black);
    QColor          m_outlineColor = QColor(Qt::transparent);
    double          m_outlineWidthMm = 0.0;
};

} // namespace occ
