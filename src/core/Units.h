#pragma once

#include <QString>

// ---------------------------------------------------------------------------
// Units - the canonical internal length unit of OpenCardCanvas is the
// MILLIMETRE.
//
// Rationale: an ID card is a physical object. Storing geometry in millimetres
// means the design stays physically accurate no matter what the screen DPI,
// monitor scaling factor or output resolution is. Screen pixels only ever
// appear at the very last step, when converting millimetres to device
// coordinates for a specific render pass.
//
// All conversions live here so there is exactly one definition of "how many
// pixels is a millimetre".
// ---------------------------------------------------------------------------
namespace occ {
namespace units {

inline constexpr double kMmPerInch    = 25.4;
inline constexpr double kMmPerPoint   = 25.4 / 72.0;   // typographic point
inline constexpr double kPointsPerInch = 72.0;

// The reference render resolution used when no explicit DPI is available.
inline constexpr int kReferenceDpi = 300;

inline double inchToMm(double inch)   { return inch * kMmPerInch; }
inline double mmToInch(double mm)     { return mm / kMmPerInch; }
inline double pointsToMm(double pt)   { return pt * kMmPerPoint; }
inline double mmToPoints(double mm)   { return mm / kMmPerPoint; }

// Pixels <-> millimetres at a given resolution (dots per inch).
inline double mmToPx(double mm, double dpi) { return mm * dpi / kMmPerInch; }
inline double pxToMm(double px, double dpi) { return px * kMmPerInch / dpi; }

// Millimetres <-> device pixels for the renderer. pxPerMm is the single scale
// factor every render pass is built from.
inline double pxPerMmFromDpi(double dpi) { return dpi / kMmPerInch; }
inline double dpiFromPxPerMm(double pxPerMm) { return pxPerMm * kMmPerInch; }

// Rounds a value to the nearest thousandth of a millimetre (1 micrometre).
// Used when serialising so that a save/load/save cycle is byte-stable.
inline double canonicalMm(double mm)
{
    return qRound(mm * 1000.0) / 1000.0;
}

enum class DisplayUnit { Millimeters, Inches };

// Formats a length for display, e.g. "85.60 mm" or "3.370 in".
QString formatLength(double mm, DisplayUnit unit, int decimals = 2,
                     bool withSuffix = true);
// Formats just the number, no unit suffix.
QString formatLengthNumber(double mm, DisplayUnit unit, int decimals = 2);

// Parses a user typed length. Accepts "85.6", "85.6mm", "85,6", "3.37in",
// "3.37\"", "250mil". Returns false (and leaves `ok` false) on garbage.
double parseLength(const QString &text, DisplayUnit fallbackUnit, bool *ok = nullptr);

// Short suffix for a display unit ("mm" / "in").
QString unitSuffix(DisplayUnit unit);

// Parses a resolution string such as "300x300" or "600x600" and returns the
// first component. Returns `fallback` when the string is not parseable.
int parseResolutionDpi(const QString &resolution, int fallback = kReferenceDpi);

} // namespace units

// Re-exported into `occ` for convenience.
using units::DisplayUnit;

} // namespace occ
