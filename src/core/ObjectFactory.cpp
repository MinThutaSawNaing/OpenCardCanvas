#include "core/ObjectFactory.h"

#include "core/BarcodeObject.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/PhotoObject.h"
#include "core/QrObject.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"

#include <QCoreApplication>
#include <QJsonValue>

namespace occ {

namespace {

QString factoryTr(const char *text)
{
    return QCoreApplication::translate("ObjectFactory", text);
}

} // namespace

CardObjectPtr ObjectFactory::create(ObjectType type)
{
    switch (type) {
    case ObjectType::Text:    return std::make_unique<TextObject>();
    case ObjectType::Image:   return std::make_unique<ImageObject>();
    case ObjectType::Photo:   return std::make_unique<PhotoObject>();
    case ObjectType::Shape:   return std::make_unique<ShapeObject>();
    case ObjectType::QrCode:  return std::make_unique<QrObject>();
    case ObjectType::Barcode: return std::make_unique<BarcodeObject>();
    case ObjectType::Group:   return std::make_unique<GroupObject>();
    }
    // Unknown enum value: refuse rather than guess, because guessing would put
    // the wrong kind of object on the card.
    return nullptr;
}

CardObjectPtr ObjectFactory::createAndLoad(const QJsonObject &json, QString *error)
{
    const auto fail = [error](const QString &message) -> CardObjectPtr {
        if (error)
            *error = message;
        return nullptr;
    };

    const QJsonValue typeValue = json.value(QStringLiteral("type"));
    if (!typeValue.isString())
        return fail(factoryTr("An object in this project has no type and could not be read."));

    const QString typeName = typeValue.toString();
    ObjectType type = ObjectType::Text;
    if (!names::objectTypeFromString(typeName, &type)) {
        return fail(factoryTr("This project contains an object of an unknown type "
                              "(\"%1\") and it could not be read.")
                        .arg(typeName));
    }

    CardObjectPtr object = create(type);
    if (!object) {
        return fail(factoryTr("The object type \"%1\" is not supported by this version "
                              "of OpenCardCanvas.")
                        .arg(typeName));
    }

    QString loadError;
    if (!object->fromJson(json, &loadError)) {
        if (error) {
            *error = loadError.isEmpty()
                         ? factoryTr("An object could not be read from this project.")
                         : loadError;
        }
        return nullptr;
    }

    if (error)
        error->clear();
    return object;
}

QStringList ObjectFactory::supportedTypeNames()
{
    QStringList names;
    names.reserve(int(std::size(names::kObjectTypes)));
    for (const auto &entry : names::kObjectTypes)
        names.append(QString::fromLatin1(entry.first));
    return names;
}

bool ObjectFactory::typeFromName(const QString &name, ObjectType *out)
{
    return names::objectTypeFromString(name, out);
}

QString ObjectFactory::nameForType(ObjectType type)
{
    return names::objectType(type);
}

} // namespace occ
