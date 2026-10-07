#pragma once

#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// TextUtils - the small string helpers the UI keeps needing.
//
// These live in one file because each of them has exactly one correct
// behaviour and getting it wrong is a bug the user sees: a file name that is
// rejected by the printer driver, a layer list sorted "Layer 10" before
// "Layer 2", a size shown as "1536 bytes" instead of "1.5 KB", or a name
// truncated in the middle of a UTF-16 surrogate pair.
//
// Nothing here touches the file system except uniqueFileName() / sanitiseFileName(),
// which only compute names.
// ---------------------------------------------------------------------------
namespace occ {
namespace text {

// --- comparison -----------------------------------------------------------
// "Layer 2" < "Layer 10", unlike a plain QString comparison. Case insensitive
// on the alphabetical part, numeric on the digit runs.
int naturalCompare(const QString &left, const QString &right);
bool naturalLessThan(const QString &a, const QString &b);
// Sorts a copy of `values` naturally.
QStringList naturallySorted(const QStringList &values);

// --- display --------------------------------------------------------------
// Truncates in the middle, keeping both ends: "Employee_Photo_Final.png" ->
// "Employee_P...l.png". Never splits a surrogate pair or a grapheme cluster.
QString elideMiddle(const QString &text, int maxCharacters, const QString &ellipsis = QStringLiteral("..."));
// "1.5 KB", "12.0 MB", using binary multiples (1024) as file managers do.
QString formatBytes(qint64 bytes);
// "1 234" - thousands separators for record counts.
QString formatCount(qint64 count);

// --- file names -----------------------------------------------------------
// Replaces every character Windows forbids in a file name (< > : " / \ | ? *),
// control characters and trailing dots/spaces. An empty result becomes
// `fallback`. The result is never longer than 120 characters.
QString sanitiseFileName(const QString &name,
                         const QString &fallback = QStringLiteral("file"));
// A sanitised, unique file name inside `directory`. Existing files cause a
// " (2)", " (3)", ... suffix to be appended, exactly like Explorer does.
QString uniqueFileName(const QString &directory, const QString &desiredName);
// True when `name` is safe to use as a single file or directory component.
bool isSafeFileName(const QString &name);

// --- placeholders ---------------------------------------------------------
// Splits a key list into "used" and "unused", comparing case insensitively.
QStringList keysNotIn(const QStringList &keys, const QStringList &known);
// "name, employee_id and department" - for user facing messages.
QString joinedForHumans(const QStringList &values, int maxItems = 4);

} // namespace text
} // namespace occ
