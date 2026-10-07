#include "personalization/TemplateEngine.h"

#include <QStringView>

namespace occ {

namespace {

// {{ key }} or {{ key | fallback }}
const char *kPattern = R"(\{\{\s*([^{}|]+?)\s*(?:\|\s*([^{}]*?)\s*)?\}\})";

} // namespace

const QRegularExpression &TemplateEngine::pattern()
{
    static const QRegularExpression re(QString::fromLatin1(kPattern));
    return re;
}

bool TemplateEngine::lookup(const QHash<QString, QString> &values, const QString &key,
                            QString *out)
{
    if (values.isEmpty())
        return false;

    auto it = values.constFind(key);
    if (it == values.constEnd()) {
        // Fall back to a case insensitive match so "Employee_ID" in a CSV still
        // feeds {{employee_id}}.
        for (auto i = values.constBegin(); i != values.constEnd(); ++i) {
            if (i.key().compare(key, Qt::CaseInsensitive) == 0) {
                it = i;
                break;
            }
        }
    }
    if (it == values.constEnd())
        return false;
    if (it.value().isEmpty())
        return false;
    if (out)
        *out = it.value();
    return true;
}

TemplateEngine::Result TemplateEngine::expandDetailed(const QString &source,
                                                      const QHash<QString, QString> &values)
{
    Result result;
    result.text = source;
    if (source.isEmpty())
        return result;

    const QRegularExpression re = pattern();
    QRegularExpressionMatchIterator it = re.globalMatch(source);

    struct Repl { int start; int length; QString text; };
    QVector<Repl> replacements;
    replacements.reserve(8);

    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString key = m.captured(1).trimmed();
        const QString fallback = m.captured(2);

        if (!result.usedKeys.contains(key))
            result.usedKeys.append(key);

        QString value;
        if (lookup(values, key, &value)) {
            replacements.append(Repl{ int(m.capturedStart(0)), int(m.capturedLength(0)), value });
        } else if (!fallback.isNull() && m.lastCapturedIndex() >= 2) {
            replacements.append(Repl{ int(m.capturedStart(0)), int(m.capturedLength(0)), fallback });
        } else {
            if (!result.missingKeys.contains(key))
                result.missingKeys.append(key);
            // Leave the placeholder untouched so it is visible rather than lost.
        }
    }

    // Apply from the end so earlier offsets stay valid.
    for (int i = replacements.size() - 1; i >= 0; --i) {
        const Repl &r = replacements.at(i);
        result.text.replace(r.start, r.length, r.text);
    }
    return result;
}

QString TemplateEngine::expand(const QString &source, const QHash<QString, QString> &values)
{
    return expandDetailed(source, values).text;
}

QStringList TemplateEngine::placeholders(const QString &source)
{
    QStringList keys;
    if (source.isEmpty())
        return keys;
    QRegularExpressionMatchIterator it = pattern().globalMatch(source);
    while (it.hasNext()) {
        const QString key = it.next().captured(1).trimmed();
        if (!keys.contains(key))
            keys.append(key);
    }
    return keys;
}

bool TemplateEngine::containsPlaceholders(const QString &source)
{
    return source.contains(pattern());
}

QStringList TemplateEngine::missing(const QString &source, const QHash<QString, QString> &values)
{
    QStringList result;
    const QStringList keys = placeholders(source);
    for (const QString &key : keys) {
        if (!lookup(values, key, nullptr) && !result.contains(key))
            result.append(key);
    }
    return result;
}

QString TemplateEngine::literal(const QString &text)
{
    // Nothing exotic: a literal is produced by leaving the text as is. The
    // helper exists so call sites document their intent.
    return text;
}

} // namespace occ
