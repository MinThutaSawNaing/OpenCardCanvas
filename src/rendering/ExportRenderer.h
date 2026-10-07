#pragma once

#include "core/CardTypes.h"
#include "rendering/CardRenderer.h"

#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// ExportRenderer - PNG, JPEG and PDF output.
//
// Everything here builds a CardRenderer::Options and a RenderContext and calls
// CardRenderer, so an exported file is pixel for pixel what the print preview
// showed. There is no export-specific drawing code anywhere in the project.
//
// PDF is written with QPdfWriter at the card's PHYSICAL size (85.60 x 53.98 mm
// for an ID-1 card) so that a "print at 100 %" from a PDF reader produces a
// dimensionally correct card. A PDF is vector for text and shapes, which is
// exactly what a card printer's driver path wants.
// ---------------------------------------------------------------------------
namespace occ {

struct CsvTable;
struct MappingSet;

class AssetStore;

class ExportRenderer
{
public:
    enum class Format { Png, Jpeg, Pdf };

    struct Options
    {
        Format format = Format::Png;
        int    dpi = 300;
        bool   frontSide = true;
        bool   backSide = false;
        bool   includeBleed = false;
        int    jpegQuality = 92;
    };

    struct Result
    {
        bool        ok = false;
        QString     error;             // user facing
        QStringList filesWritten;
        QString     technicalDetail;   // for the log

        int fileCount() const { return int(filesWritten.size()); }
    };

    // --- single side --------------------------------------------------------
    // `path` must already carry the desired extension; it is corrected when the
    // format and the extension disagree.
    static Result exportSide(const CardDocument &doc, CardSideId side,
                             const QString &path, const Options &options);

    // Writes <base>_front.<ext> and (when requested) <base>_back.<ext>.
    static Result exportBoth(const CardDocument &doc, const QString &basePath,
                             const Options &options);

    // --- PDF ----------------------------------------------------------------
    // Draws the requested sides at their physical size. `hardwarePending` is
    // set when a side could not be drawn because something it needs (an image
    // asset) is unavailable - the PDF is still valid, the caller may choose to
    // warn.
    static bool exportPdf(const CardDocument &doc, const QString &path,
                          const Options &options, QString *error = nullptr,
                          bool *hardwarePending = nullptr);

    // --- batch --------------------------------------------------------------
    // Renders one image per CSV record, named
    //   <row number>_<sanitised key field>_front.png
    // The key field is the value of the first mapped field, which is what a
    // human expects to see in a folder of a thousand cards.
    static Result exportBatch(const CardDocument &doc, const CsvTable &table,
                              const MappingSet &mapping,
                              const AssetStore &photos, const QString &outputDir,
                              const Options &options);

    // Canonical extension for a format, including the dot.
    static QString extensionFor(Format format);
    // Chooses the format from a file name, or `fallback` when unknown.
    static Format formatForPath(const QString &path, Format fallback);
    // Human readable name of a format, for progress and error messages.
    static QString formatName(Format format);
};

} // namespace occ
