#include "personalization/BatchRenderer.h"

#include "core/CardDocument.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/PhotoObject.h"
#include "core/Units.h"
#include "project/AssetStore.h"
#include "rendering/CardRenderer.h"
#include "utils/TextUtils.h"

#include <QCoreApplication>
#include <QFileInfo>

namespace occ {

namespace {

QString batchTr(const char *text)
{
    return QCoreApplication::translate("BatchRenderer", text);
}

// Serves the record's own assets first and falls back to the project's, so a
// batch card is rendered with the document's artwork plus this record's
// photograph and nothing else changes.
class RecordImageProvider : public ImageProvider
{
public:
    RecordImageProvider(const AssetStore *recordAssets, const AssetStore *documentAssets)
        : m_first(recordAssets), m_second(documentAssets)
    {
    }

    bool has(const QString &assetId) const override
    {
        if (assetId.isEmpty())
            return false;
        if (m_first && m_first->has(assetId))
            return true;
        return m_second && m_second->has(assetId);
    }

    QImage image(const QString &assetId) const override
    {
        if (m_first) {
            const QImage image = m_first->image(assetId);
            if (!image.isNull())
                return image;
        }
        return m_second ? m_second->image(assetId) : QImage();
    }

    QString description(const QString &assetId) const override
    {
        if (m_first && m_first->has(assetId))
            return m_first->description(assetId);
        if (m_second && m_second->has(assetId))
            return m_second->description(assetId);
        return QCoreApplication::translate("BatchRenderer", "an unnamed image");
    }

private:
    const AssetStore *m_first = nullptr;
    const AssetStore *m_second = nullptr;
};

// True when `object` (or something inside it) can hold the record's photograph.
bool isPhotoTarget(const CardObject *object)
{
    return dynamic_cast<const ImageObject *>(object) != nullptr;
}

CardObject *findFirstPhoto(const QVector<CardObject *> &objects)
{
    for (CardObject *object : objects) {
        if (isPhotoTarget(object))
            return object;
    }
    return nullptr;
}

CardObject *findByLayerName(const QVector<CardObject *> &objects, const QString &name)
{
    for (CardObject *object : objects) {
        if (object && isPhotoTarget(object)
            && object->name().compare(name, Qt::CaseInsensitive) == 0) {
            return object;
        }
    }
    return nullptr;
}

} // namespace

ObjectId BatchRenderer::photoObjectOn(const CardDocument &doc, CardSideId side,
                                     const MappingSet &mapping)
{
    if (mapping.photoAssetPlaceholder.isEmpty())
        return ObjectId();

    const CardSide &cardSide = doc.side(side);
    // The search order is deliberate: what the user named wins over what happens
    // to be first, and a real photo frame wins over a plain image.
    if (CardObject *named = findByLayerName(cardSide.objects(),
                                            mapping.photoAssetPlaceholder)) {
        return named->id();
    }

    QVector<CardObject *> topLevel = cardSide.objects();
    for (CardObject *object : topLevel) {
        if (auto *group = dynamic_cast<GroupObject *>(object)) {
            if (CardObject *named = findByLayerName(group->children(),
                                                    mapping.photoAssetPlaceholder)) {
                return named->id();
            }
        }
    }

    for (CardObject *object : topLevel) {
        if (dynamic_cast<PhotoObject *>(object))
            return object->id();
    }
    for (CardObject *object : topLevel) {
        if (auto *group = dynamic_cast<GroupObject *>(object)) {
            if (CardObject *photo = findFirstPhoto(group->children()))
                return photo->id();
        }
    }
    if (CardObject *image = findFirstPhoto(topLevel))
        return image->id();
    return ObjectId();
}

QString BatchRenderer::fileNameForRecord(const CsvTable &table, int recordIndex,
                                         const MappingSet &mapping)
{
    // 1-based, because that is the number a human sees in a spreadsheet.
    const QString number = QString::number(recordIndex + 1);

    QString key;
    if (recordIndex >= 0 && recordIndex < table.rowCount()) {
        for (const FieldMapping &field : mapping.fields) {
            if (!field.isValid())
                continue;
            key = table.value(recordIndex, field.csvColumn).trimmed();
            if (!key.isEmpty())
                break;
        }
    }
    if (key.isEmpty())
        key = batchTr("record");
    if (key.size() > 60)
        key = key.left(60);

    return QStringLiteral("%1_%2").arg(number, text::sanitiseFileName(key));
}

bool BatchRenderer::prepareRecord(const CardDocument &doc, CardSideId side,
                                 int recordIndex, const CsvTable &table,
                                 const MappingSet &mapping, const AssetStore &photoStore,
                                 QHash<QString, QString> *values,
                                 AssetStore *recordAssets, RecordResult *result)
{
    if (!values || !recordAssets || !result)
        return false;

    *result = RecordResult();
    recordAssets->clear();

    if (recordIndex < 0 || recordIndex >= table.rowCount()) {
        result->error = batchTr("The data set has %1 records, so record %2 does not "
                               "exist.")
                            .arg(table.rowCount())
                            .arg(recordIndex + 1);
        return false;
    }

    QString valuesError;
    *values = DataMapper::valuesForRecord(table, recordIndex, mapping, &valuesError);
    if (!valuesError.isEmpty()) {
        result->error = valuesError;
        return false;
    }

    result->photoObjectId = photoObjectOn(doc, side, mapping);
    if (result->photoObjectId.isNull()) {
        // Nothing on this card can hold a photograph, which is normal for many
        // designs; there is nothing to warn about.
        result->ok = true;
        return true;
    }

    bool photoValueMissing = false;
    const QString path =
        DataMapper::photoPathForRecord(table, recordIndex, mapping, &photoValueMissing);

    QString assetId;
    QString fileName;
    if (!path.isEmpty()) {
        QString importError;
        assetId = recordAssets->addImageFile(path, &importError);
        fileName = QFileInfo(path).fileName();
        if (assetId.isEmpty()) {
            // The file exists but is not usable: that is worth telling the user,
            // because the printed card will show the missing-image placeholder.
            result->warnings.append(
                { batchTr("The photograph \"%1\" for record %2 could not be used: %3")
                      .arg(fileName)
                      .arg(recordIndex + 1)
                      .arg(importError),
                  result->photoObjectId });
            fileName.clear();
        }
    } else {
        // No usable path: the caller may already have imported the photographs
        // into the store it handed us, in which case the data set's value is the
        // file name of one of them.
        const QString column = mapping.columnFor(mapping.photoAssetPlaceholder);
        const QString wanted =
            column.isEmpty() ? QString() : table.value(recordIndex, column).trimmed();
        if (!wanted.isEmpty()) {
            const QString wantedName = QFileInfo(wanted).fileName().toCaseFolded();
            for (const QString &id : photoStore.assetIds()) {
                if (photoStore.fileName(id).toCaseFolded() == wantedName) {
                    assetId = id;
                    fileName = photoStore.fileName(id);
                    break;
                }
            }
        }
        if (assetId.isEmpty()) {
            if (photoValueMissing) {
                result->warnings.append(
                    { batchTr("The photograph \"%1\" for record %2 was not found, so "
                              "the card was printed with the missing-image marking.")
                          .arg(QFileInfo(table.value(
                                            recordIndex,
                                            mapping.columnFor(
                                                mapping.photoAssetPlaceholder)))
                                    .fileName())
                          .arg(recordIndex + 1),
                      result->photoObjectId });
            } else if (!wanted.isEmpty()) {
                result->warnings.append(
                    { batchTr("The photograph \"%1\" for record %2 is not part of this "
                              "project, so the card was printed with the "
                              "missing-image marking.")
                          .arg(wanted)
                          .arg(recordIndex + 1),
                      result->photoObjectId });
            } else {
                result->warnings.append(
                    { batchTr("Record %1 has no photograph, so the card was printed "
                              "with the missing-image marking.")
                          .arg(recordIndex + 1),
                      result->photoObjectId });
            }
        }
    }

    result->photoAssetId = assetId;
    result->photoFileName = fileName;
    result->ok = true;
    return true;
}

QImage BatchRenderer::renderRecord(const CardDocument &doc, CardSideId side,
                                  int recordIndex, const CsvTable &table,
                                  const MappingSet &mapping, const AssetStore &photoStore,
                                  double pxPerMm,
                                  QVector<RenderContext::Warning> *warnings)
{
    QHash<QString, QString> values;
    AssetStore recordAssets;
    RecordResult prepared;
    const bool preparedOk = prepareRecord(doc, side, recordIndex, table, mapping,
                                          photoStore, &values, &recordAssets, &prepared);
    if (warnings)
        *warnings += prepared.warnings;
    if (!preparedOk || !prepared.ok) {
        if (warnings && !prepared.error.isEmpty())
            warnings->append({ prepared.error, ObjectId() });
        return QImage();
    }

    // The card is rendered from a copy of the side, so the personalization pass
    // can never leave a record's values or photograph in the open document.
    const CardSide &source = doc.side(side);
    CardSide recordSide(side);
    recordSide.setBackground(source.background());
    for (CardObject *object : source.objects()) {
        if (!object)
            continue;
        CardObjectPtr copy = object->clone();
        if (copy && prepared.photoObjectId != ObjectId()
            && copy->id() == prepared.photoObjectId && !prepared.photoAssetId.isEmpty()) {
            if (auto *image = dynamic_cast<ImageObject *>(copy.get())) {
                image->setAssetId(prepared.photoAssetId);
                // The record's photograph arrives cropped to the frame, and the
                // non-destructive source window of the template must not be
                // applied to a different picture.
                image->resetCrop();
            }
        }
        recordSide.insertObject(std::move(copy));
    }

    CardRenderer::Options options;
    options.pxPerMmOverride = pxPerMm > 0.0 ? pxPerMm : units::pxPerMmFromDpi(300);
    if (!doc.geometry().isValid(nullptr)) {
        if (warnings) {
            warnings->append(
                { batchTr("This card cannot be drawn because its size is not valid."),
                  ObjectId() });
        }
        return QImage();
    }

    // First the record's own assets, then the project's: exactly one photograph
    // changes from card to card.
    RecordImageProvider provider(&recordAssets, doc.assets());

    RenderContext ctx = CardRenderer::makeContext(doc.geometry(), options, &provider);
    ctx.setForEditing(false);
    ctx.setPlaceholders(&values);

    const QImage image =
        CardRenderer::renderSideWithContext(recordSide, doc.geometry(), options, ctx);
    if (warnings)
        *warnings += ctx.warnings();
    return image;
}

QImage BatchRenderer::renderTemplate(const CardDocument &doc, CardSideId side,
                                     double pxPerMm,
                                     QVector<RenderContext::Warning> *warnings)
{
    CardRenderer::Options options;
    options.pxPerMmOverride = pxPerMm > 0.0 ? pxPerMm : units::pxPerMmFromDpi(300);
    // No placeholders are set, so {{name}} stays visible as {{name}}: that is the
    // whole point of a template preview.
    return CardRenderer::renderSide(doc, side, options, warnings);
}

} // namespace occ
