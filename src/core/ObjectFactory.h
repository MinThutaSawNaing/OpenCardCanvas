#pragma once

#include "core/CardObject.h"
#include "core/CardTypes.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// ObjectFactory - the single place where a concrete CardObject is built from a
// type tag.
//
// Two entry points exist:
//
//   create()        builds an empty object with sensible defaults (used by the
//                   drawing tools and by "insert object" commands)
//   createAndLoad() builds the concrete object named by a JSON "type" field and
//                   loads it (used by CardSide::fromJson and therefore by every
//                   project file, template and paste operation)
//
// Keeping the switch in one file means a new object type only has to be taught
// to this class and to CardObject::typeDisplayName(); nothing else in the
// application needs to know the concrete classes.
// ---------------------------------------------------------------------------
namespace occ {

class ObjectFactory
{
public:
    // Creates an empty object of the requested type with sensible defaults.
    static CardObjectPtr create(ObjectType type);

    // Creates the right concrete object for the JSON "type" field and loads it.
    // Returns nullptr and fills `error` for an unknown or invalid object.
    static CardObjectPtr createAndLoad(const QJsonObject &json, QString *error);

    // The names accepted by createAndLoad(), i.e. the contents of the "type"
    // field. Used by diagnostics and by the file format documentation.
    static QStringList supportedTypeNames();

    // Convenience: the ObjectType behind a persisted type name.
    static bool typeFromName(const QString &name, ObjectType *out);
    static QString nameForType(ObjectType type);
};

} // namespace occ
