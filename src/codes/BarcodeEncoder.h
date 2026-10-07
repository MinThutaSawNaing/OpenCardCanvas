#pragma once

#include "core/CardTypes.h"

#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// BarcodeEncoder - linear (1D) barcode symbologies used on ID cards.
//
// Supports Code 128 (code sets A/B/C with automatic switching), Code 39,
// EAN-13, EAN-8 and ITF-14.  The encoder returns module level bars/spaces;
// the renderer turns those into pixels, so the same encoding drives the
// editor, the preview, PNG export and the printer output.
//
// Validation is explicit: encode() refuses invalid data and explains why,
// because an unreadable barcode on a printed card is worse than a build error.
// ---------------------------------------------------------------------------
namespace occ {

class BarcodeEncoder
{
public:
    struct Result
    {
        bool ok = false;
        QString error;                 // populated when ok == false
        QVector<bool> modules;         // true == dark module, left to right
        QString humanText;             // text to print under the bars (may be empty)
        int quietZoneModules = 10;     // recommended quiet zone on each side
        BarcodeSymbology symbology = BarcodeSymbology::Code128;

        bool isDark(int index) const
        {
            if (index < 0 || index >= modules.size())
                return false;
            return modules.at(index);
        }
        int moduleCount() const { return static_cast<int>(modules.size()); }
    };

    // Encodes `data`; performs normalisation and validation first.
    // `showCheckDigit` controls whether a computed check digit is appended
    // (Code 39) or accepted from the input (EAN / ITF).
    static Result encode(const QString &data, BarcodeSymbology symbology,
                         bool showCheckDigit = false);

    // Validates without producing a barcode. `error` receives a user facing
    // explanation suitable for display next to the offending property.
    static bool validate(const QString &data, BarcodeSymbology symbology,
                         QString *error = nullptr);

    // Normalises user input into the canonical form for the symbology
    // (upper-casing Code 39, stripping EAN separators, padding ITF-14, ...).
    // Returns an empty string and sets `error` when the data cannot be used.
    static QString normalize(const QString &data, BarcodeSymbology symbology,
                             QString *error = nullptr);

    // Human readable text is part of the symbology definition:
    // Code 39 / Code 128 display the raw data, EAN types display digit groups.
    static bool supportsHumanText(BarcodeSymbology symbology);
    static QString humanReadableText(const QString &data, BarcodeSymbology symbology);

    // Localised display names for the UI.
    static QString displayName(BarcodeSymbology symbology);

    // Computes the EAN/UPC modulo-10 check digit for a digit string that does
    // not yet include it.
    static int eanCheckDigit(const QString &digitsWithoutCheckDigit);

    // Code 39 optional modulo-43 check character.
    static QChar code39CheckCharacter(const QString &data);
};

} // namespace occ
