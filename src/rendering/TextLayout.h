#pragma once

#include "core/CardTypes.h"

#include <QFont>
#include <QFontMetricsF>
#include <QMetaType>
#include <QPainterPath>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// TextLayout - deterministic, resolution independent text layout.
//
// Why this exists: Qt maps font point sizes through a paint device's logical
// DPI. That makes text metrics depend on whether we are drawing to a widget, an
// image, a printer or a PDF, which in turn makes the same card re-flow between
// the editor and the printout - exactly the failure mode this application must
// not have.
//
// Instead, glyph outlines are generated from a font at a FIXED reference pixel
// size and then scaled by an exact factor into millimetres. Layout therefore
// produces identical results on every device, at every resolution, and text is
// drawn as true vector outlines (which is also what gives sharp print output).
// ---------------------------------------------------------------------------
namespace occ {

struct TextSegment
{
    QString text;
    double  xMm = 0.0;        // left edge of the segment, relative to the line box
};

struct TextLine
{
    QVector<TextSegment> segments;
    double  widthMm = 0.0;
    QString plainText() const;
};

struct TextLayoutResult
{
    QVector<TextLine> lines;
    double lineAdvanceMm = 0.0;   // baseline to baseline
    double ascentMm = 0.0;
    double descentMm = 0.0;
    double totalHeightMm = 0.0;
    double widestLineMm = 0.0;
    bool   overflowWidth = false;    // a word was wider than the box
    bool   overflowHeight = false;   // the block is taller than the box
    bool   truncated = false;
};

class TextLayout
{
public:
    struct Params
    {
        double          boxWidthMm = 0.0;      // 0 disables wrapping
        HorizontalAlign hAlign = HorizontalAlign::Left;
        bool            wordWrap = true;
        double          letterSpacingMm = 0.0;
        double          lineSpacingPercent = 100.0;
    };

    // Reference pixel size used to build glyph outlines. Large enough that the
    // down-scaling error is far below a device pixel at any sensible output
    // resolution.
    static constexpr double kReferencePx = 256.0;

    // Lays out `text` (which may contain '\n') for the given font and box.
    static TextLayoutResult layout(const QString &text, double pointSize,
                                   const QString &family, bool bold, bool italic,
                                   const Params &params);

    // Glyph outlines for `text`, in millimetres, baseline at (0,0).
    static QPainterPath outline(const QString &text, double pointSize,
                                const QString &family, bool bold, bool italic);

    // Width of a single line in millimetres (kerning preserved).
    static double measureWidthMm(const QString &text, double pointSize,
                                 const QString &family, bool bold, bool italic);

    static double measureHeightMm(double pointSize, double lineSpacingPercent = 100.0);

    // Natural (unscaled) QFont at the reference pixel size. Exposed so the UI
    // can preview a font without duplicating the setup.
    static QFont referenceFont(const QString &family, bool bold, bool italic);

private:
    // A font pinned to kReferencePx together with its metrics and the factor
    // that converts reference pixels into millimetres for a given point size.
    struct MetricFont
    {
        QFont        font;
        QFontMetricsF metrics;
        double       mmPerPx = 1.0;

        // QFontMetricsF has no default constructor in Qt 6, so a metric font
        // cannot be default constructed either. Building it in one step is the
        // only correct way to hand out the pair.
        MetricFont(const QFont &fontValue, const QFontMetricsF &metricsValue,
                   double mmPerPxValue)
            : font(fontValue), metrics(metricsValue), mmPerPx(mmPerPxValue)
        {
        }
    };
    static MetricFont prepare(double pointSize, const QString &family, bool bold,
                              bool italic);
};

} // namespace occ

Q_DECLARE_METATYPE(occ::TextLayoutResult)
