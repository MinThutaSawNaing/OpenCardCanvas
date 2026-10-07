#include "project/ProjectValidator.h"

#include "core/BarcodeObject.h"
#include "core/CardDocument.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/QrObject.h"
#include "core/TextObject.h"
#include "project/AssetStore.h"
#include "rendering/TextLayout.h"
#include "utils/TextUtils.h"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QSet>

namespace occ {

namespace {

QString validatorTr(const char *text)
{
    return QCoreApplication::translate("ProjectValidator", text);
}

QString quoted(const QString &name)
{
    return QStringLiteral("\u201c%1\u201d").arg(name);
}

QString objectName(const CardObject &object)
{
    const QString name = object.name().trimmed();
    return name.isEmpty() ? object.typeDisplayName() : name;
}

// Anything at or below this is invisible on the printed card and cannot be
// grabbed again with the mouse, so it is reported rather than left alone.
double minimumSizeMm()
{
    return 0.2;
}

void addIssue(Report &report, Issue::Severity severity, const QString &message,
              CardSideId side, const ObjectId &objectId = ObjectId())
{
    Issue issue;
    issue.severity = severity;
    issue.message = message;
    issue.objectId = objectId;
    issue.side = side;
    report.issues.append(issue);
}

} // namespace

int Report::errorCount() const
{
    int count = 0;
    for (const Issue &issue : issues) {
        if (issue.severity == Issue::Severity::Error)
            ++count;
    }
    return count;
}

int Report::warningCount() const
{
    int count = 0;
    for (const Issue &issue : issues) {
        if (issue.severity == Issue::Severity::Warning)
            ++count;
    }
    return count;
}

bool Report::hasErrors() const
{
    return errorCount() > 0;
}

QString Report::summary() const
{
    const int errors = errorCount();
    const int warnings = warningCount();
    if (errors == 0 && warnings == 0)
        return validatorTr("No problems were found.");
    if (errors == 0) {
        return QCoreApplication::translate("ProjectValidator",
                                           "%n warning(s) were found.", nullptr,
                                           warnings);
    }
    if (warnings == 0) {
        return QCoreApplication::translate("ProjectValidator", "%n error(s) were found.",
                                           nullptr, errors);
    }
    return validatorTr("%1 error(s) and %2 warning(s) were found.")
        .arg(errors)
        .arg(warnings);
}

QString ProjectValidator::severityText(Issue::Severity severity)
{
    switch (severity) {
    case Issue::Severity::Error:   return validatorTr("Error");
    case Issue::Severity::Warning: return validatorTr("Warning");
    case Issue::Severity::Info:    return validatorTr("Information");
    }
    return validatorTr("Information");
}

namespace {

// Checks one object. Everything that concerns only the object itself - its size,
// its payload, its module width, whether its text fits, whether its font exists -
// is reported from here.
void validateObject(const CardObject &object, const CardSide &side,
                    const CardGeometry &geom, const AssetStore *assets,
                    const QSet<ObjectId> &seen, Report &report)
{
    const QString label = quoted(objectName(object));
    const CardSideId sideId = side.id();

    if (seen.contains(object.id())) {
        addIssue(report, Issue::Severity::Error,
                 validatorTr("Two objects on the %1 side share the same identity, so "
                             "editing one would affect the other. Delete and re-add %2.")
                     .arg(names::side(sideId), label),
                 sideId, object.id());
    }

    const double width = object.widthMm();
    const double height = object.heightMm();
    if (width <= 0.0 || height <= 0.0) {
        addIssue(report, Issue::Severity::Error,
                 validatorTr("%1 has no size. Give it a width and a height of at least "
                             "%2 mm.")
                     .arg(label)
                     .arg(minimumSizeMm(), 0, 'f', 1),
                 sideId, object.id());
    } else if (width < minimumSizeMm() || height < minimumSizeMm()) {
        addIssue(report, Issue::Severity::Warning,
                 validatorTr("%1 is only %2 x %3 mm, which is too small to see on the "
                             "printed card.")
                     .arg(label)
                     .arg(width, 0, 'f', 2)
                     .arg(height, 0, 'f', 2),
                 sideId, object.id());
    }

    // Completely outside the card: it can never appear in the output, so this is
    // almost always a mis-drag rather than an intention.
    if (!object.boundingRectMm().intersects(geom.boundsMm())) {
        addIssue(report, Issue::Severity::Warning,
                 validatorTr("%1 lies completely outside the card and will not be "
                             "printed. Move it back onto the card or delete it.")
                     .arg(label),
                 sideId, object.id());
    } else if (!geom.boundsMm().contains(object.boundingRectMm())) {
        addIssue(report, Issue::Severity::Info,
                 validatorTr("%1 extends past the edge of the card, so the part outside "
                             "the card will be cut off.")
                     .arg(label),
                 sideId, object.id());
    }

    if (const auto *text = dynamic_cast<const TextObject *>(&object)) {
        const TextLayout::Params params{ text->widthMm(),     text->horizontalAlign(),
                                         text->wordWrap(),    text->letterSpacingMm(),
                                         text->lineSpacingPercent() };
        const TextLayoutResult layout =
            TextLayout::layout(text->text(), text->fontSizePt(), text->fontFamily(),
                               text->bold(), text->italic(), params);
        if (layout.overflowHeight) {
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("The text in %1 does not fit its box: it needs %2 mm "
                                 "of height but the box is %3 mm. Enlarge the box, "
                                 "reduce the font size or switch on auto-shrink.")
                         .arg(label)
                         .arg(layout.totalHeightMm, 0, 'f', 1)
                         .arg(text->heightMm(), 0, 'f', 1),
                     sideId, object.id());
        } else if (layout.overflowWidth) {
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("A word in %1 is wider than its box, so it had to be "
                                 "broken across lines. Enlarge the box or reduce the "
                                 "font size.")
                         .arg(label),
                     sideId, object.id());
        }
        if (!text->fontFamily().isEmpty()
            && !QFontDatabase::families().contains(text->fontFamily(),
                                                   Qt::CaseInsensitive)) {
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("The font \"%1\" used by %2 is not installed on this "
                                 "computer, so a substitute will be printed. Install "
                                 "the font or choose another one.")
                         .arg(text->fontFamily(), label),
                     sideId, object.id());
        }
    }

    if (const auto *qr = dynamic_cast<const QrObject *>(&object)) {
        if (qr->data().trimmed().isEmpty()) {
            addIssue(report, Issue::Severity::Error,
                     validatorTr("%1 has no data, so it would print as an unreadable "
                                 "square. Enter the text or placeholder it should "
                                 "contain.")
                         .arg(label),
                     sideId, object.id());
        } else if (qr->data().contains(QLatin1String("{{"))) {
            addIssue(report, Issue::Severity::Info,
                     validatorTr("%1 contains a placeholder, so it is filled in for "
                                 "each record when personalizing. Without a data set it "
                                 "prints the placeholder text itself.")
                         .arg(label),
                     sideId, object.id());
        } else {
            const QString qrError = qr->validationError();
            if (!qrError.isEmpty()) {
                addIssue(report, Issue::Severity::Error,
                         validatorTr("%1 cannot be encoded: %2").arg(label, qrError),
                         sideId, object.id());
            }
        }
    }

    if (const auto *barcode = dynamic_cast<const BarcodeObject *>(&object)) {
        const bool hasPlaceholder = barcode->data().contains(QLatin1String("{{"));
        if (barcode->data().trimmed().isEmpty()) {
            addIssue(report, Issue::Severity::Error,
                     validatorTr("%1 has no data, so it would print as blank bars. "
                                 "Enter the value or placeholder it should contain.")
                         .arg(label),
                     sideId, object.id());
        } else if (!hasPlaceholder) {
            const QString barcodeError = barcode->validationError();
            if (!barcodeError.isEmpty()) {
                addIssue(report, Issue::Severity::Error,
                         validatorTr("%1 cannot be encoded: %2")
                             .arg(label, barcodeError),
                         sideId, object.id());
            }
        }

        double moduleMm = 0.0;
        if (barcode->moduleWidthIsRisky(&moduleMm)) {
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("The barcode %1 is too narrow to print reliably: its "
                                 "narrowest bar would be %2 mm, but %3 mm is the "
                                 "minimum recommended for a card printer. Make the "
                                 "barcode wider or shorten the data.")
                         .arg(label)
                         .arg(moduleMm, 0, 'f', 3)
                         .arg(BarcodeObject::minimumReliableModuleMm(), 0, 'f', 2),
                     sideId, object.id());
        }
    }

    if (const auto *image = dynamic_cast<const ImageObject *>(&object)) {
        if (image->assetId().isEmpty()) {
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("%1 has no image yet, so it prints as a marked "
                                 "placeholder. Choose an image for it.")
                         .arg(label),
                     sideId, object.id());
        } else if (!assets || !assets->has(image->assetId())) {
            addIssue(report, Issue::Severity::Error,
                     validatorTr("The image used by %1 (%2) is missing from this "
                                 "project. Replace the image or delete the object.")
                         .arg(label, assets ? assets->description(image->assetId())
                                            : validatorTr("unknown file")),
                     sideId, object.id());
        }
    }

    Q_UNUSED(seen);
}

} // namespace

Report ProjectValidator::validate(const CardDocument &doc,
                                 const QStringList &mappedPlaceholders)
{
    Report report;

    const CardGeometry &geom = doc.geometry();
    QString geometryError;
    if (!geom.isValid(&geometryError)) {
        addIssue(report, Issue::Severity::Error,
                 validatorTr("The card size is not valid: %1").arg(geometryError),
                 CardSideId::Front);
    }

    const AssetStore *assets = doc.assets();

    // Duplicate ids are detected per side, because an id is only required to be
    // unique within the side it lives on.
    for (CardSideId sideId : { CardSideId::Front, CardSideId::Back }) {
        const CardSide &side = doc.side(sideId);
        QSet<ObjectId> seen;
        for (CardObject *object : side.objects()) {
            if (!object)
                continue;
            validateObject(*object, side, geom, assets, seen, report);
            seen.insert(object->id());
        }

        // The background image is a card-wide dependency and is easy to forget.
        if (side.background().kind == CardSide::Background::Kind::Image
            && side.background().assetId.isEmpty()) {
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("The %1 side is set to use an image as its background "
                                 "but no image has been chosen yet, so the background "
                                 "prints blank.")
                         .arg(names::side(sideId)),
                     sideId);
        } else if (side.background().kind == CardSide::Background::Kind::Image
                   && assets && !assets->has(side.background().assetId)) {
            addIssue(report, Issue::Severity::Error,
                     validatorTr("The background image of the %1 side (%2) is missing "
                                 "from this project. Choose the image again.")
                         .arg(names::side(sideId),
                              assets->description(side.background().assetId)),
                     sideId);
        }

        // A side that is not empty but prints nothing is a mistake worth naming.
        if (!side.isEmpty() && !side.contentBoundsMm().intersects(geom.boundsMm())) {
            addIssue(report, Issue::Severity::Error,
                     validatorTr("Nothing on the %1 side is inside the card, so the "
                                 "printed side would be blank. Move the design back "
                                 "onto the card.")
                         .arg(names::side(sideId)),
                     sideId);
        }
    }

    // --- data set placeholders ----------------------------------------------
    // Only checked when the caller says a data set is loaded: without one, a
    // placeholder is a perfectly normal thing to have in a template.
    if (!mappedPlaceholders.isEmpty()) {
        const QStringList used = doc.placeholders();
        for (const QString &placeholder : used) {
            if (mappedPlaceholders.contains(placeholder))
                continue;
            addIssue(report, Issue::Severity::Warning,
                     validatorTr("The placeholder {{%1}} is used by this design but no "
                                 "column of the loaded data set has been mapped to it, "
                                 "so it will print literally.")
                         .arg(placeholder),
                     CardSideId::Front);
        }
    }

    // A card that uses no placeholder at all cannot be personalized; that is
    // useful information rather than a problem, but it is worth saying once.
    if (mappedPlaceholders.isEmpty() && !doc.placeholders().isEmpty()) {
        addIssue(report, Issue::Severity::Info,
                 validatorTr("This design contains %1 placeholder(s) that will be "
                             "filled in from a data set: %2.")
                     .arg(doc.placeholders().size())
                     .arg(text::joinedForHumans(doc.placeholders())),
                 CardSideId::Front);
    }

    return report;
}

} // namespace occ
