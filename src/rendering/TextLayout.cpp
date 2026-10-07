#include "rendering/TextLayout.h"

#include "core/Units.h"

#include <QFontMetricsF>
#include <QtMath>

#include <cmath>

namespace occ {

namespace {

constexpr QChar kSpace = QLatin1Char(' ');

bool isSpace(QChar c)
{
    return c == kSpace || c == QLatin1Char('\t');
}

} // namespace

QString TextLine::plainText() const
{
    QString s;
    for (const TextSegment &seg : segments)
        s += seg.text;
    return s;
}

QFont TextLayout::referenceFont(const QString &family, bool bold, bool italic)
{
    QFont f;
    if (!family.isEmpty())
        f.setFamily(family);
    f.setPixelSize(int(kReferencePx));
    f.setBold(bold);
    f.setItalic(italic);
    // Outlines are generated from the font, so hinting and kerning behaviour
    // must be identical on every machine.
    f.setHintingPreference(QFont::PreferNoHinting);
    f.setKerning(true);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

TextLayout::MetricFont TextLayout::prepare(double pointSize, const QString &family,
                                           bool bold, bool italic)
{
    const QFont font = referenceFont(family, bold, italic);
    const QFontMetricsF metrics(font);
    // One reference pixel corresponds to this many millimetres. A point is
    // 25.4/72 mm, so a `pointSize` pt glyph is pointSize*kMmPerPoint mm tall and
    // occupies kReferencePx reference pixels.
    const double desiredMm = units::pointsToMm(qMax(0.1, pointSize));
    return MetricFont(font, metrics, desiredMm / kReferencePx);
}

double TextLayout::measureWidthMm(const QString &text, double pointSize,
                                  const QString &family, bool bold, bool italic)
{
    const MetricFont mf = prepare(pointSize, family, bold, italic);
    return mf.metrics.horizontalAdvance(text) * mf.mmPerPx;
}

double TextLayout::measureHeightMm(double pointSize, double lineSpacingPercent)
{
    const MetricFont mf = prepare(pointSize, QString(), false, false);
    const double advance = mf.metrics.height() * mf.mmPerPx;
    return advance * qBound(10.0, lineSpacingPercent, 1000.0) / 100.0;
}

QPainterPath TextLayout::outline(const QString &text, double pointSize,
                                 const QString &family, bool bold, bool italic)
{
    const MetricFont mf = prepare(pointSize, family, bold, italic);
    QPainterPath path;
    if (text.isEmpty())
        return path;
    path.addText(QPointF(0.0, 0.0), mf.font, text);
    // Reference pixels -> millimetres.
    const QTransform t = QTransform::fromScale(mf.mmPerPx, mf.mmPerPx);
    return t.map(path);
}


TextLayoutResult TextLayout::layout(const QString &text, double pointSize,
                                    const QString &family, bool bold, bool italic,
                                    const Params &params)
{
    TextLayoutResult result;
    const MetricFont mf = prepare(pointSize, family, bold, italic);
    const double mmPerPx = mf.mmPerPx;
    const double boxWidth = qMax(0.0, params.boxWidthMm);
    const double letterSpacing = qMax(0.0, params.letterSpacingMm);
    const double lineSpacing = qBound(10.0, params.lineSpacingPercent, 1000.0) / 100.0;

    result.ascentMm = mf.metrics.ascent() * mmPerPx;
    result.descentMm = mf.metrics.descent() * mmPerPx;
    result.lineAdvanceMm = mf.metrics.height() * mmPerPx * lineSpacing;

    // Advance width of a string in millimetres, including letter spacing.
    const auto widthOf = [&](const QString &s) -> double {
        if (s.isEmpty())
            return 0.0;
        double w = mf.metrics.horizontalAdvance(s) * mmPerPx;
        if (letterSpacing > 0.0 && s.size() > 1)
            w += letterSpacing * double(s.size() - 1);
        return w;
    };
    // x offset of the n-th character inside a run, honouring letter spacing.
    const auto charOffsets = [&](const QString &s) -> QVector<double> {
        QVector<double> offsets(s.size(), 0.0);
        if (letterSpacing <= 0.0)
            return offsets;
        for (int i = 1; i < s.size(); ++i) {
            offsets[i] = mf.metrics.horizontalAdvance(s.left(i)) * mmPerPx
                         + letterSpacing * double(i);
        }
        return offsets;
    };
    // Splits a paragraph into wrap tokens ("words"); runs of spaces collapse.
    const auto tokenize = [](const QString &paragraph) -> QStringList {
        QStringList words;
        QString current;
        for (const QChar c : paragraph) {
            if (isSpace(c)) {
                if (!current.isEmpty()) {
                    words.append(current);
                    current.clear();
                }
            } else {
                current.append(c);
            }
        }
        if (!current.isEmpty())
            words.append(current);
        return words;
    };
    // Breaks a single word that is wider than the box into character chunks.
    const auto hardBreak = [&](const QString &word) -> QStringList {
        QStringList chunks;
        QString current;
        for (const QChar c : word) {
            const QString candidate = current + c;
            if (!current.isEmpty() && widthOf(candidate) > boxWidth) {
                chunks.append(current);
                current = QString(c);
            } else {
                current = candidate;
            }
        }
        if (!current.isEmpty())
            chunks.append(current);
        return chunks;
    };
    const auto renderWidthOf = [&](const QStringList &words) -> double {
        double w = 0.0;
        for (int i = 0; i < words.size(); ++i) {
            if (i > 0)
                w += widthOf(QString(kSpace));
            w += widthOf(words.at(i));
        }
        return w;
    };

    // Emits one visual line. `isLastOfParagraph` drives justification.
    const auto emitLine = [&](const QStringList &words, bool isLastOfParagraph) {
        TextLine line;
        if (words.isEmpty()) {
            result.lines.append(line);
            return;
        }
        const double naturalWidth = renderWidthOf(words);
        const bool justify = (params.hAlign == HorizontalAlign::Justify)
                             && !isLastOfParagraph && words.size() > 1
                             && naturalWidth < boxWidth;

        if (justify) {
            const double gap = (boxWidth - naturalWidth) / double(words.size() - 1);
            double x = 0.0;
            for (int i = 0; i < words.size(); ++i) {
                line.segments.append(TextSegment{ words.at(i), x });
                x += widthOf(words.at(i));
                if (i + 1 < words.size())
                    x += widthOf(QString(kSpace)) + gap;
            }
            line.widthMm = boxWidth;
        } else if (letterSpacing > 0.0) {
            // One segment per character so tracking is exact.
            double x = 0.0;
            for (int wi = 0; wi < words.size(); ++wi) {
                if (wi > 0) {
                    line.segments.append(TextSegment{ QString(kSpace), x });
                    x += widthOf(QString(kSpace));
                }
                const QString &word = words.at(wi);
                const QVector<double> offsets = charOffsets(word);
                for (int ci = 0; ci < word.size(); ++ci)
                    line.segments.append(TextSegment{ QString(word.at(ci)), x + offsets.at(ci) });
                x += widthOf(word);
            }
            line.widthMm = naturalWidth;
        } else {
            line.segments.append(TextSegment{ words.join(QString(kSpace)), 0.0 });
            line.widthMm = naturalWidth;
        }
        result.lines.append(line);
    };

    const QStringList paragraphs = text.split(QLatin1Char('\n'));
    for (int p = 0; p < paragraphs.size(); ++p) {
        const QString paragraph = paragraphs.at(p);

        if (!params.wordWrap || boxWidth <= 0.0) {
            // No wrapping: each paragraph is exactly one line.
            if (paragraph.isEmpty()) {
                emitLine(QStringList(), true);
            } else {
                const QStringList words = tokenize(paragraph);
                emitLine(words, true);
                if (renderWidthOf(words) > boxWidth + 1e-6)
                    result.overflowWidth = true;
            }
            continue;
        }

        if (paragraph.isEmpty()) {
            emitLine(QStringList(), true);
            continue;
        }

        // Greedy word wrap.
        const QStringList words = tokenize(paragraph);
        QStringList line;
        double lineWidth = 0.0;

        for (int i = 0; i < words.size(); ++i) {
            const QString &word = words.at(i);
            const double wordWidth = widthOf(word);
            const double spaceWidth = line.isEmpty() ? 0.0 : widthOf(QString(kSpace));
            const double candidate = lineWidth + spaceWidth + wordWidth;

            if (!line.isEmpty() && candidate > boxWidth + 1e-6) {
                emitLine(line, false);
                line.clear();
                lineWidth = 0.0;
            }

            if (line.isEmpty() && wordWidth > boxWidth + 1e-6) {
                // A single word that cannot fit: break it across lines.
                const QStringList chunks = hardBreak(word);
                for (int c = 0; c < chunks.size(); ++c) {
                    if (c + 1 < chunks.size())
                        emitLine(QStringList{ chunks.at(c) }, false);
                    else {
                        line = QStringList{ chunks.at(c) };
                        lineWidth = widthOf(chunks.at(c));
                    }
                }
                result.overflowWidth = true;
                continue;
            }

            lineWidth = line.isEmpty() ? wordWidth : (lineWidth + spaceWidth + wordWidth);
            line.append(word);
        }
        if (!line.isEmpty())
            emitLine(line, true);
    }

    result.totalHeightMm = result.lineAdvanceMm * double(result.lines.size());
    for (const TextLine &line : result.lines)
        result.widestLineMm = qMax(result.widestLineMm, line.widthMm);

    return result;
}

} // namespace occ
