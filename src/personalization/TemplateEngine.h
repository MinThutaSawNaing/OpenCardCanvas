#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

// ---------------------------------------------------------------------------
// TemplateEngine - the {{placeholder}} substitution used by templates and by
// CSV driven personalization.
//
// Syntax:
//     {{name}}            substituted with the value of "name"
//     {{name|Unknown}}    substituted, or with the fallback when unset/empty
//
// Unknown keys are reported through Result::missingKeys instead of being
// silently dropped, so the personalization UI can tell the user exactly which
// column is missing rather than printing a card with a blank field.
// ---------------------------------------------------------------------------
namespace occ {

class TemplateEngine
{
public:
    struct Result
    {
        QString     text;
        QStringList usedKeys;      // ordered, unique
        QStringList missingKeys;   // placeholders with no value
    };

    static Result expandDetailed(const QString &source, const QHash<QString, QString> &values);
    static QString expand(const QString &source, const QHash<QString, QString> &values);

    // All placeholder keys present in `source`, in order of first appearance.
    static QStringList placeholders(const QString &source);
    static bool containsPlaceholders(const QString &source);

    // Keys present in `source` that have no (non-empty) value in `values`.
    static QStringList missing(const QString &source, const QHash<QString, QString> &values);

    // Case sensitive lookup first, then case insensitive.
    static bool lookup(const QHash<QString, QString> &values, const QString &key,
                       QString *out);

    // Encloses text so it is treated as a literal by expand().
    static QString literal(const QString &text);

    static const QRegularExpression &pattern();

    static constexpr char kOpenToken[]  = "{{";
    static constexpr char kCloseToken[] = "}}";
};

} // namespace occ
