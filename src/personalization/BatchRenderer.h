#pragma once

#include "core/CardTypes.h"
#include "personalization/CsvImporter.h"
#include "personalization/DataMapper.h"
#include "rendering/RenderContext.h"

#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// BatchRenderer - one CSV record in, one card image out.
//
// This class is deliberately thin. It resolves the record's values, resolves
// the record's photograph into a temporary asset, and then hands the document
// to CardRenderer. It contains no drawing code at all, which is what makes a
// batch output byte-for-byte identical to what the editor showed: there is only
// one renderer.
//
// A missing or unreadable photograph produces the normal "missing image"
// rendering (a clearly marked placeholder, courtesy of the core object) plus a
// warning. A face is never substituted and the batch never aborts.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;
class AssetStore;

class BatchRenderer
{
public:
    struct RecordResult
    {
        bool    ok = false;
        QString error;                       // user facing, empty when ok
        QVector<RenderContext::Warning> warnings;
        ObjectId photoObjectId;              // object the photo was put into
        QString  photoAssetId;               // asset id of this record's photograph
        QString  photoFileName;              // for user facing messages
    };

    // Renders the card for one CSV record, with every {{placeholder}} expanded
    // and the record's photograph placed into the mapped photo object.
    // `photoStore` receives (and owns) the decoded photograph for the duration
    // of the call, which keeps the document itself unchanged.
    static QImage renderRecord(const CardDocument &doc, CardSideId side, int recordIndex,
                               const CsvTable &table, const MappingSet &mapping,
                               const AssetStore &photoStore, double pxPerMm,
                               QVector<RenderContext::Warning> *warnings = nullptr);

    // Renders the card as designed, with placeholders left untouched. Used for
    // the template thumbnail and for a dry run before a batch starts.
    static QImage renderTemplate(const CardDocument &doc, CardSideId side, double pxPerMm,
                                 QVector<RenderContext::Warning> *warnings = nullptr);

    // --- helpers ------------------------------------------------------------
    // The document is never modified by rendering; these two functions produce
    // the side that IS rendered (a copy with the record's values applied), so a
    // caller can also use them for a per-record print preview.
    // Returns nullptr (and fills `result`) when the record cannot be prepared.
    static bool prepareRecord(const CardDocument &doc, CardSideId side, int recordIndex,
                              const CsvTable &table, const MappingSet &mapping,
                              const AssetStore &photoStore, QHash<QString, QString> *values,
                              AssetStore *recordAssets, RecordResult *result);

    // The photo object that MappingSet::photoAssetPlaceholder feeds, if any.
    static ObjectId photoObjectOn(const CardDocument &doc, CardSideId side,
                                  const MappingSet &mapping);

    // Filename stem for one record: "<row>_<sanitised key value>".
    static QString fileNameForRecord(const CsvTable &table, int recordIndex,
                                     const MappingSet &mapping);
};

} // namespace occ
