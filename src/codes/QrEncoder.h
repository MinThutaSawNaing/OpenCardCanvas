#pragma once

#include "core/CardTypes.h"

#include <QByteArray>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// QrEncoder - a complete, self contained QR Code (ISO/IEC 18004) encoder.
//
// Implemented from the specification: versions 1..40, error correction levels
// L/M/Q/H, numeric / alphanumeric / byte (UTF-8) modes, optimal mode selection,
// Reed-Solomon error correction and all eight data masks with penalty scoring.
//
// The encoder produces a module matrix only - rendering (colours, quiet zone,
// scaling) is the caller's responsibility, which keeps the algorithm testable
// against golden matrices produced by an independent implementation.
// ---------------------------------------------------------------------------
namespace occ {

class QrEncoder
{
public:
    struct Result
    {
        bool  ok = false;
        QString error;                      // populated when ok == false
        int   version = 0;                  // 1 .. 40
        int   size = 0;                     // modules per side = 17 + 4 * version
        QrErrorCorrection ecc = QrErrorCorrection::Medium;
        QVector<bool> modules;              // size * size, row major, true == dark
        QString mode;                       // "numeric" | "alphanumeric" | "byte"

        bool isDark(int x, int y) const
        {
            if (x < 0 || y < 0 || x >= size || y >= size)
                return false;
            return modules.at(y * size + x);
        }
    };

    // Encodes UTF-8 text at the requested error correction level using the
    // smallest version that fits. Mode is chosen automatically.
    static Result encode(const QString &text, QrErrorCorrection ecc);

    // Same, for an explicitly supplied UTF-8 byte string.
    static Result encodeUtf8(const QByteArray &utf8, QrErrorCorrection ecc);

    // True when `text` can be encoded at the requested level within version 40.
    static bool canEncode(const QString &text, QrErrorCorrection ecc);

    // Number of data bits available for a version/level combination.
    static int dataCapacityBits(int version, QrErrorCorrection ecc);

    // Total number of codewords for a version (data + error correction).
    static int totalCodewords(int version);

    // Error correction codewords per block / block layout, exposed for tests.
    static int eccCodewordsPerBlock(int version, QrErrorCorrection ecc);
    static int blockCount(int version, QrErrorCorrection ecc);
};

} // namespace occ
