#pragma once

#include <QString>
#include <QUuid>

#include <utility>

// ---------------------------------------------------------------------------
// CardTypes - identifiers and enumerations shared by the whole document model.
//
// Everything that reaches a .occard file is written through the names::xxxx()
// helpers and read back with the matching *FromString() helper, so the file
// format is stable, self describing and never depends on enum ordering.
// ---------------------------------------------------------------------------
namespace occ {

// Stable identity for a design object. Survives save/load and undo/redo.
using ObjectId = QUuid;

enum class ObjectType { Text, Image, Photo, Shape, QrCode, Barcode, Group };
enum class CardSideId { Front = 0, Back = 1 };
enum class HorizontalAlign { Left, Center, Right, Justify };
enum class VerticalAlign { Top, Middle, Bottom };

// How a source image is mapped into its frame. Non destructive: the original
// image is never modified, only the sampling rectangle changes.
enum class ImageFitMode { Stretch, Contain, Cover, Center, Tile };

enum class CropShape { Rectangle, RoundedRectangle, Ellipse };

enum class ShapeKind {
    Rectangle, RoundedRectangle, Ellipse, Line,
    Triangle, Polygon, Star, Arrow
};

enum class BarcodeSymbology { Code128, Code39, Ean13, Ean8, Itf14 };
enum class QrErrorCorrection { Low = 0, Medium = 1, Quartile = 2, High = 3 };

enum class QrContentKind {
    PlainText, Url, EmployeeId, StudentId, PatientId, Contact, Custom
};

namespace names {

// Generic, table driven enum <-> string mapping.
template <typename E, std::size_t N>
QString toName(E value, const std::pair<const char *, E> (&table)[N],
               const QString &fallback)
{
    for (const auto &entry : table) {
        if (entry.second == value)
            return QString::fromLatin1(entry.first);
    }
    return fallback;
}

template <typename E, std::size_t N>
bool fromName(const QString &s, E *out, const std::pair<const char *, E> (&table)[N])
{
    for (const auto &entry : table) {
        if (s == QLatin1String(entry.first)) {
            if (out)
                *out = entry.second;
            return true;
        }
    }
    return false;
}

} // namespace names

// ---------------------------------------------------------------------------
// Enum <-> persistence string tables
// ---------------------------------------------------------------------------
// NOTE (fixed 2026-xx-xx): this block used to read "namespace occ::names {".
// Inside "namespace occ" that form declares the nested namespace occ::occ::names
// on MSVC with /permissive-, which left every helper in this block (objectType(),
// side(), ...) invisible to the rest of the program and made CardTypes.h - and
// therefore the whole project - fail to compile. Written as "namespace names"
// the block reopens occ::names, which is what the declared API already promised:
// occ::names::objectType(), occ::names::side(), and so on. No public name changed.
namespace names {

inline constexpr std::pair<const char *, ObjectType> kObjectTypes[] = {
    { "text", ObjectType::Text },   { "image", ObjectType::Image },
    { "photo", ObjectType::Photo }, { "shape", ObjectType::Shape },
    { "qr", ObjectType::QrCode },   { "barcode", ObjectType::Barcode },
    { "group", ObjectType::Group },
};
inline QString objectType(ObjectType t)
{ return toName(t, kObjectTypes, QStringLiteral("unknown")); }
inline bool objectTypeFromString(const QString &s, ObjectType *out)
{ return fromName(s, out, kObjectTypes); }

inline constexpr std::pair<const char *, CardSideId> kSides[] = {
    { "front", CardSideId::Front }, { "back", CardSideId::Back },
};
inline QString side(CardSideId s) { return toName(s, kSides, QStringLiteral("front")); }
inline bool sideFromString(const QString &s, CardSideId *out)
{ return fromName(s.toLower(), out, kSides); }

inline constexpr std::pair<const char *, HorizontalAlign> kHAlign[] = {
    { "left", HorizontalAlign::Left },   { "center", HorizontalAlign::Center },
    { "right", HorizontalAlign::Right }, { "justify", HorizontalAlign::Justify },
};
inline QString horizontalAlign(HorizontalAlign a)
{ return toName(a, kHAlign, QStringLiteral("left")); }
inline bool horizontalAlignFromString(const QString &s, HorizontalAlign *out)
{ return fromName(s, out, kHAlign); }

inline constexpr std::pair<const char *, VerticalAlign> kVAlign[] = {
    { "top", VerticalAlign::Top }, { "middle", VerticalAlign::Middle },
    { "bottom", VerticalAlign::Bottom },
};
inline QString verticalAlign(VerticalAlign a)
{ return toName(a, kVAlign, QStringLiteral("top")); }
inline bool verticalAlignFromString(const QString &s, VerticalAlign *out)
{ return fromName(s, out, kVAlign); }

inline constexpr std::pair<const char *, ImageFitMode> kImageFit[] = {
    { "stretch", ImageFitMode::Stretch }, { "contain", ImageFitMode::Contain },
    { "cover", ImageFitMode::Cover },     { "center", ImageFitMode::Center },
    { "tile", ImageFitMode::Tile },
};
inline QString imageFit(ImageFitMode m)
{ return toName(m, kImageFit, QStringLiteral("cover")); }
inline bool imageFitFromString(const QString &s, ImageFitMode *out)
{ return fromName(s, out, kImageFit); }

inline constexpr std::pair<const char *, CropShape> kCropShapes[] = {
    { "rectangle", CropShape::Rectangle },
    { "rounded", CropShape::RoundedRectangle },
    { "ellipse", CropShape::Ellipse },
};
inline QString cropShape(CropShape s)
{ return toName(s, kCropShapes, QStringLiteral("rectangle")); }
inline bool cropShapeFromString(const QString &s, CropShape *out)
{ return fromName(s, out, kCropShapes); }

inline constexpr std::pair<const char *, ShapeKind> kShapeKinds[] = {
    { "rectangle", ShapeKind::Rectangle }, { "rounded", ShapeKind::RoundedRectangle },
    { "ellipse", ShapeKind::Ellipse },     { "line", ShapeKind::Line },
    { "triangle", ShapeKind::Triangle },   { "polygon", ShapeKind::Polygon },
    { "star", ShapeKind::Star },           { "arrow", ShapeKind::Arrow },
};
inline QString shapeKind(ShapeKind k)
{ return toName(k, kShapeKinds, QStringLiteral("rectangle")); }
inline bool shapeKindFromString(const QString &s, ShapeKind *out)
{ return fromName(s, out, kShapeKinds); }

inline constexpr std::pair<const char *, BarcodeSymbology> kBarcodeSymbologies[] = {
    { "code128", BarcodeSymbology::Code128 }, { "code39", BarcodeSymbology::Code39 },
    { "ean13", BarcodeSymbology::Ean13 },     { "ean8", BarcodeSymbology::Ean8 },
    { "itf14", BarcodeSymbology::Itf14 },
};
inline QString barcodeSymbology(BarcodeSymbology s)
{ return toName(s, kBarcodeSymbologies, QStringLiteral("code128")); }
inline bool barcodeSymbologyFromString(const QString &s, BarcodeSymbology *out)
{ return fromName(s, out, kBarcodeSymbologies); }

inline constexpr std::pair<const char *, QrErrorCorrection> kQrEcc[] = {
    { "L", QrErrorCorrection::Low },      { "M", QrErrorCorrection::Medium },
    { "Q", QrErrorCorrection::Quartile }, { "H", QrErrorCorrection::High },
};
inline QString qrEcc(QrErrorCorrection e)
{ return toName(e, kQrEcc, QStringLiteral("M")); }
inline bool qrEccFromString(const QString &s, QrErrorCorrection *out)
{ return fromName(s.toUpper(), out, kQrEcc); }

inline constexpr std::pair<const char *, QrContentKind> kQrKinds[] = {
    { "text", QrContentKind::PlainText },       { "url", QrContentKind::Url },
    { "employee", QrContentKind::EmployeeId },  { "student", QrContentKind::StudentId },
    { "patient", QrContentKind::PatientId },    { "contact", QrContentKind::Contact },
    { "custom", QrContentKind::Custom },
};
inline QString qrContentKind(QrContentKind k)
{ return toName(k, kQrKinds, QStringLiteral("text")); }
inline bool qrContentKindFromString(const QString &s, QrContentKind *out)
{ return fromName(s, out, kQrKinds); }

} // namespace names

} // namespace occ
