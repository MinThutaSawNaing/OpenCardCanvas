#include "rendering/ExportRenderer.h"

#include "core/CardDocument.h"
#include "core/CardGeometry.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/Units.h"
#include "personalization/BatchRenderer.h"
#include "personalization/CsvImporter.h"
#include "personalization/DataMapper.h"
#include "project/AssetStore.h"
#include "utils/TextUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

namespace occ {

namespace {

QString exportTr(const char *text)
{
    return QCoreApplication::translate("ExportRenderer", text);
}

// True when anything on the side refers to an image the store does not have.
// Used to tell the caller that a PDF is geometrically complete but is missing
// artwork - the one thing a PDF reader cannot tell you.
bool sideIsMissingAssets(const CardSide &side, const AssetStore *assets)
{
    const auto missing = [assets](const QString &assetId) {
        return !assetId.isEmpty() && (!assets || !assets->has(assetId));
    };

    if (side.background().kind == CardSide::Background::Kind::Image
        && missing(side.background().assetId)) {
        return true;
    }

    // Groups can hold images too, so the walk descends into them.
    QVector<const CardObject *> pending;
    for (CardObject *object : side.objects())
        pending.append(object);
    while (!pending.isEmpty()) {
        const CardObject *object = pending.takeLast();
        if (!object)
            continue;
        if (const auto *image = dynamic_cast<const ImageObject *>(object)) {
            if (missing(image->assetId()))
                return true;
        }
        if (const auto *group = dynamic_cast<const GroupObject *>(object)) {
            for (CardObject *child : group->children())
                pending.append(child);
        }
    }
    return false;
}

// "cards/employee" + "_front" + ".png". A base path that already carries one of
// our extensions is used as the stem, so "card.png" becomes "card_front.png"
// rather than "card.png_front.png".
QString sidePath(const QString &basePath, const QString &suffix,
                 ExportRenderer::Format format)
{
    const QString extension = ExportRenderer::extensionFor(format);
    QString stem = basePath;
    const QString existing = QFileInfo(basePath).suffix().toLower();
    if (existing == QLatin1String("png") || existing == QLatin1String("jpg")
        || existing == QLatin1String("jpeg") || existing == QLatin1String("pdf")) {
        stem = basePath.left(int(basePath.size() - existing.size()) - 1);
    }
    return stem + suffix + extension;
}

ExportRenderer::Result saveImage(const QImage &image, const QString &path,
                                 const ExportRenderer::Options &options)
{
    ExportRenderer::Result result;
    if (image.isNull()) {
        result.error = exportTr("The card could not be drawn, so nothing was written "
                                "to \"%1\".")
                           .arg(QFileInfo(path).fileName());
        result.technicalDetail = QStringLiteral("render produced a null image");
        return result;
    }

    QImageWriter writer(path);
    if (options.format == ExportRenderer::Format::Jpeg)
        writer.setQuality(qBound(1, options.jpegQuality, 100));

    if (!writer.write(image)) {
        result.error = exportTr("\"%1\" could not be written.\n%2")
                           .arg(QFileInfo(path).fileName(), writer.errorString());
        result.technicalDetail = QStringLiteral("QImageWriter: ") + writer.errorString();
        return result;
    }

    result.ok = true;
    result.filesWritten.append(path);
    return result;
}

} // namespace

QString ExportRenderer::extensionFor(Format format)
{
    switch (format) {
    case Format::Png:  return QStringLiteral(".png");
    case Format::Jpeg: return QStringLiteral(".jpg");
    case Format::Pdf:  return QStringLiteral(".pdf");
    }
    return QStringLiteral(".png");
}

ExportRenderer::Format ExportRenderer::formatForPath(const QString &path, Format fallback)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png"))
        return Format::Png;
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return Format::Jpeg;
    if (suffix == QLatin1String("pdf"))
        return Format::Pdf;
    return fallback;
}

QString ExportRenderer::formatName(Format format)
{
    switch (format) {
    case Format::Png:  return exportTr("PNG image");
    case Format::Jpeg: return exportTr("JPEG image");
    case Format::Pdf:  return exportTr("PDF document");
    }
    return exportTr("image");
}

ExportRenderer::Result ExportRenderer::exportSide(const CardDocument &doc, CardSideId side,
                                                 const QString &path,
                                                 const Options &options)
{
    Result result;
    if (path.trimmed().isEmpty()) {
        result.error = exportTr("No destination was given, so nothing was exported.");
        result.technicalDetail = QStringLiteral("exportSide: empty path");
        return result;
    }

    const QDir directory = QFileInfo(path).absoluteDir();
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        result.error = exportTr("The folder \"%1\" does not exist and could not be "
                                "created.")
                           .arg(directory.absolutePath());
        result.technicalDetail = QStringLiteral("mkpath failed: ")
                                 + directory.absolutePath();
        return result;
    }

    if (options.format == Format::Pdf) {
        // Only the requested side is written.
        Options pdfOptions = options;
        pdfOptions.frontSide = (side == CardSideId::Front);
        pdfOptions.backSide = (side == CardSideId::Back);
        QString error;
        bool hardwarePending = false;
        if (!exportPdf(doc, path, pdfOptions, &error, &hardwarePending)) {
            result.error = error;
            result.technicalDetail = QStringLiteral("exportPdf failed");
            return result;
        }
        result.ok = true;
        result.filesWritten.append(path);
        return result;
    }

    CardRenderer::Options renderOptions;
    renderOptions.dpi = options.dpi;
    renderOptions.includeBleed = options.includeBleed;
    renderOptions.forPrinting = false;

    const QImage image = CardRenderer::renderSide(doc, side, renderOptions, nullptr);
    return saveImage(image, path, options);
}

ExportRenderer::Result ExportRenderer::exportBoth(const CardDocument &doc,
                                                 const QString &basePath,
                                                 const Options &options)
{
    Result result;
    if (basePath.trimmed().isEmpty()) {
        result.error = exportTr("No destination was given, so nothing was exported.");
        result.technicalDetail = QStringLiteral("exportBoth: empty path");
        return result;
    }

    if (options.format == Format::Pdf) {
        Options pdfOptions = options;
        pdfOptions.frontSide = true;
        pdfOptions.backSide = true;
        const QString path = basePath.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)
                                 ? basePath
                                 : basePath + extensionFor(Format::Pdf);
        QString error;
        bool hardwarePending = false;
        if (!exportPdf(doc, path, pdfOptions, &error, &hardwarePending)) {
            result.error = error;
            result.technicalDetail = QStringLiteral("exportPdf failed");
            return result;
        }
        result.ok = true;
        result.filesWritten.append(path);
        return result;
    }

    if (!options.frontSide && !options.backSide) {
        result.error = exportTr("Neither side was selected, so nothing was exported.");
        result.technicalDetail = QStringLiteral("exportBoth: no side selected");
        return result;
    }

    if (options.frontSide) {
        const Result front = exportSide(doc, CardSideId::Front,
                                        sidePath(basePath, QStringLiteral("_front"),
                                                 options.format),
                                        options);
        if (!front.ok) {
            result.error = front.error;
            result.technicalDetail = front.technicalDetail;
            return result;
        }
        result.filesWritten += front.filesWritten;
    }
    if (options.backSide) {
        const Result back = exportSide(doc, CardSideId::Back,
                                       sidePath(basePath, QStringLiteral("_back"),
                                                options.format),
                                       options);
        if (!back.ok) {
            result.error = back.error;
            result.technicalDetail = back.technicalDetail;
            return result;
        }
        result.filesWritten += back.filesWritten;
    }

    result.ok = true;
    return result;
}

bool ExportRenderer::exportPdf(const CardDocument &doc, const QString &path,
                               const Options &options, QString *error,
                               bool *hardwarePending)
{
    if (hardwarePending)
        *hardwarePending = false;

    if (path.trimmed().isEmpty()) {
        if (error)
            *error = exportTr("No destination was given, so nothing was exported.");
        return false;
    }
    if (options.format != Format::Pdf) {
        if (error)
            *error = exportTr("The PDF export was called for a non-PDF format.");
        return false;
    }

    const CardGeometry &geom = doc.geometry();
    QString geometryError;
    if (!geom.isValid(&geometryError)) {
        if (error) {
            *error = exportTr("A PDF cannot be written for this card because its size "
                              "is not valid:\n%1")
                         .arg(geometryError);
        }
        return false;
    }

    const bool wantFront = options.frontSide || !options.backSide;
    const bool wantBack = options.backSide;

    QPdfWriter writer(path);
    // QPdfWriter reports failure through QPainter::begin(); the constructor only
    // opens the stream, so the real test happens there.

    const int dpi = qBound(72, options.dpi, 2400);
    writer.setResolution(dpi);
    writer.setTitle(doc.title());
    writer.setCreator(QStringLiteral("OpenCardCanvas"));
    // The page IS the card, at physical size, so "print at 100 %" produces a
    // dimensionally correct card.
    const QSizeF physical(geom.bleedBoundsMm().width(), geom.bleedBoundsMm().height());
    writer.setPageSize(QPageSize(physical, QPageSize::Millimeter, QString(),
                                 QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));

    // The document-level objects are already positioned in card millimetres, so
    // the painter needs the same millimetre space the renderer expects.
    CardRenderer::Options renderOptions;
    renderOptions.dpi = dpi;
    renderOptions.includeBleed = options.includeBleed;
    renderOptions.forPrinting = true;
    renderOptions.pxPerMmOverride = units::pxPerMmFromDpi(dpi);

    const QRectF area = CardRenderer::paintAreaMm(geom, renderOptions);

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error)
            *error = exportTr("A PDF could not be created at \"%1\".")
                         .arg(QFileInfo(path).fileName());
        return false;
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    bool firstPage = true;
    const auto paintOne = [&](CardSideId side) {
        if (!firstPage)
            writer.newPage();
        firstPage = false;
        painter.save();
        painter.scale(renderOptions.pxPerMm(), renderOptions.pxPerMm());
        painter.translate(-area.x(), -area.y());
        RenderContext ctx = CardRenderer::makeContext(geom, renderOptions, doc.assets());
        CardRenderer::paintSide(painter, doc.side(side), geom, ctx);
        painter.restore();
    };

    if (wantFront)
        paintOne(CardSideId::Front);
    if (wantBack)
        paintOne(CardSideId::Back);

    painter.end();

    if (!QFileInfo::exists(path) || QFileInfo(path).size() <= 0) {
        if (error) {
            *error = exportTr("\"%1\" could not be written. Check that the folder "
                              "exists and is writable.")
                         .arg(QFileInfo(path).fileName());
        }
        return false;
    }

    if (hardwarePending) {
        // "Hardware pending" here means: the file is valid but something it
        // needed was not available, so a human should look at it.
        *hardwarePending =
            (wantFront && sideIsMissingAssets(doc.front(), doc.assets()))
            || (wantBack && sideIsMissingAssets(doc.back(), doc.assets()));
    }
    if (error)
        error->clear();
    return true;
}

ExportRenderer::Result ExportRenderer::exportBatch(const CardDocument &doc,
                                                  const CsvTable &table,
                                                  const DataMapper::MappingSet &mapping,
                                                  const AssetStore &photos,
                                                  const QString &outputDir,
                                                  const Options &options)
{
    Result result;

    if (outputDir.trimmed().isEmpty()) {
        result.error = exportTr("No output folder was given, so nothing was exported.");
        result.technicalDetail = QStringLiteral("exportBatch: empty directory");
        return result;
    }
    QDir directory(outputDir);
    if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
        result.error = exportTr("The output folder \"%1\" does not exist and could not "
                                "be created.")
                           .arg(outputDir);
        result.technicalDetail = QStringLiteral("mkpath failed: ") + outputDir;
        return result;
    }
    if (table.rowCount() <= 0) {
        result.error = exportTr("The data set contains no records, so no cards were "
                                "exported.");
        result.technicalDetail = QStringLiteral("exportBatch: zero rows");
        return result;
    }

    // One file per record, named so that a folder of a thousand cards can be
    // checked by eye: <row>_<key value>_front.png
    const CardSideId side = CardSideId::Front;
    const double pxPerMm = units::pxPerMmFromDpi(qBound(72, options.dpi, 2400));

    for (int row = 0; row < table.rowCount(); ++row) {
        const QString stem = BatchRenderer::fileNameForRecord(table, row, mapping);
        const QString fileName =
            text::uniqueFileName(outputDir, stem + QStringLiteral("_")
                                                 + names::side(side)
                                                 + extensionFor(options.format));
        const QString path = directory.filePath(fileName);

        const QImage image =
            BatchRenderer::renderRecord(doc, side, row, table, mapping, photos, pxPerMm,
                                        nullptr);
        if (image.isNull()) {
            result.error = exportTr("Record %1 of the data set could not be drawn, so "
                                    "the export was stopped.")
                               .arg(row + 1);
            result.technicalDetail = QStringLiteral("renderRecord returned null for row ")
                                     + QString::number(row);
            return result;
        }

        // Batch output is always raster: one PDF per record would be one file per
        // card with no benefit, and the printer path does not use PDF for batches.
        Options fileOptions = options;
        if (fileOptions.format == Format::Pdf)
            fileOptions.format = Format::Png;
        const Result written = saveImage(image, path, fileOptions);
        if (!written.ok) {
            result.error = written.error;
            result.technicalDetail = written.technicalDetail;
            return result;
        }
        result.filesWritten += written.filesWritten;
    }

    result.ok = true;
    return result;
}

} // namespace occ
