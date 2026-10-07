#pragma once

#include "core/CardTypes.h"

#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// ProjectValidator - the structural audit behind Help -> Diagnostics.
//
// The validator answers one question: "would this design print correctly?" It
// therefore looks at things Qt will happily let you do but a card printer will
// not forgive - a barcode whose modules are a third of a millimetre wide, a
// photograph whose asset never made it into the project, a name that is
// silently clipped by its own text box, a font that is not installed.
//
// Every message is written for the person designing the card, never for a
// programmer: it names the object and says what to do about it.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;

struct Issue
{
    enum class Severity { Error, Warning, Info };

    Severity  severity = Severity::Warning;
    QString   message;          // user facing, already translated
    ObjectId  objectId;         // null when the issue is not object specific
    CardSideId side = CardSideId::Front;

    bool isError() const { return severity == Severity::Error; }
};

struct Report
{
    QVector<Issue> issues;

    int  errorCount() const;
    int  warningCount() const;
    bool hasErrors() const;
    QString summary() const;    // e.g. "2 errors, 1 warning"
};

class ProjectValidator
{
public:
    // Runs every check against the document as it stands.
    //
    // `documentPlaceholders` are the {{placeholders}} used by the design;
    // `mappedPlaceholders` are those a loaded data set actually provides. When
    // `mappedPlaceholders` is not empty, placeholders with no mapping are
    // reported. Pass an empty list to skip that check.
    static Report validate(const CardDocument &doc,
                           const QStringList &mappedPlaceholders = QStringList());

    // Severity helpers exposed so the UI can colour a single issue.
    static QString severityText(Issue::Severity severity);
};

} // namespace occ
