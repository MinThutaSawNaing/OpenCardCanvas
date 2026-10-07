// ---------------------------------------------------------------------------
// Unit tests: {{placeholder}} expansion (occ::TemplateEngine).
// ---------------------------------------------------------------------------
#include "personalization/TemplateEngine.h"

#include <QtTest/QtTest>

using namespace occ;

class TestTemplate : public QObject
{
    Q_OBJECT
private slots:
    void replacesSimpleValues();
    void usesFallbackWhenEmpty();
    void reportsMissingKeys();
    void leavesUnknownPlaceholderVisible();
    void literalTextIsUntouched();
    void listsPlaceholdersInOrder();
    void handlesAdjacentAndRepeated();
    void isCaseInsensitiveOnLookup();
    void detectsPlaceholders();
    void handlesWhitespaceInsideBraces();
};

void TestTemplate::replacesSimpleValues()
{
    const QHash<QString, QString> values{
        { QStringLiteral("name"), QStringLiteral("John Doe") },
        { QStringLiteral("employee_id"), QStringLiteral("EMP001") },
    };
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{name}}"), values),
             QStringLiteral("John Doe"));
    QCOMPARE(TemplateEngine::expand(QStringLiteral("ID: {{employee_id}}"), values),
             QStringLiteral("ID: EMP001"));
    // Repeated use of the same key must work everywhere.
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{name}} / {{name}}"), values),
             QStringLiteral("John Doe / John Doe"));
}

void TestTemplate::usesFallbackWhenEmpty()
{
    const QHash<QString, QString> values{ { QStringLiteral("department"), QString() } };
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{department|Unassigned}}"), values),
             QStringLiteral("Unassigned"));
    // A present value must win over the fallback.
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{department|Unassigned}}"),
                                    { { QStringLiteral("department"), QStringLiteral("IT") } }),
             QStringLiteral("IT"));
}

void TestTemplate::reportsMissingKeys()
{
    const QHash<QString, QString> values{ { QStringLiteral("name"), QStringLiteral("A") } };
    const TemplateEngine::Result r = TemplateEngine::expandDetailed(
        QStringLiteral("{{name}} {{expiry_date}} {{photo}}"), values);

    QCOMPARE(r.usedKeys.size(), 3);
    QCOMPARE(r.missingKeys.size(), 2);
    QVERIFY(r.missingKeys.contains(QStringLiteral("expiry_date")));
    QVERIFY(r.missingKeys.contains(QStringLiteral("photo")));
    QVERIFY(r.text.contains(QStringLiteral("A")));
}

void TestTemplate::leavesUnknownPlaceholderVisible()
{
    // A missing value must stay visible so the problem is obvious rather than
    // silently producing a blank field on a printed card.
    const QString out = TemplateEngine::expand(QStringLiteral("X{{nope}}Y"), {});
    QCOMPARE(out, QStringLiteral("X{{nope}}Y"));
}

void TestTemplate::literalTextIsUntouched()
{
    const QHash<QString, QString> values{ { QStringLiteral("a"), QStringLiteral("1") } };
    QCOMPARE(TemplateEngine::expand(QStringLiteral("no braces here"), values),
             QStringLiteral("no braces here"));
    QCOMPARE(TemplateEngine::expand(QString(), values), QString());
}

void TestTemplate::listsPlaceholdersInOrder()
{
    const QStringList keys = TemplateEngine::placeholders(
        QStringLiteral("{{b}} {{a}} {{b}} {{c}}"));
    QCOMPARE(keys, QStringList({ QStringLiteral("b"), QStringLiteral("a"),
                                 QStringLiteral("c") }));
    QVERIFY(TemplateEngine::missing(QStringLiteral("{{a}}"), {}).contains(QStringLiteral("a")));
}

void TestTemplate::handlesAdjacentAndRepeated()
{
    const QHash<QString, QString> values{
        { QStringLiteral("a"), QStringLiteral("1") },
        { QStringLiteral("b"), QStringLiteral("2") },
    };
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{a}}{{b}}{{a}}"), values),
             QStringLiteral("121"));
}

void TestTemplate::isCaseInsensitiveOnLookup()
{
    // An HR export with "Employee_ID" must still feed {{employee_id}}.
    const QHash<QString, QString> values{ { QStringLiteral("Employee_ID"), QStringLiteral("E1") } };
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{employee_id}}"), values),
             QStringLiteral("E1"));
}

void TestTemplate::detectsPlaceholders()
{
    QVERIFY(TemplateEngine::containsPlaceholders(QStringLiteral("{{x}}")));
    QVERIFY(!TemplateEngine::containsPlaceholders(QStringLiteral("plain")));
    QVERIFY(!TemplateEngine::containsPlaceholders(QStringLiteral("{ x }")));
}

void TestTemplate::handlesWhitespaceInsideBraces()
{
    const QHash<QString, QString> values{ { QStringLiteral("name"), QStringLiteral("Jo") } };
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{  name  }}"), values),
             QStringLiteral("Jo"));
}

QTEST_MAIN(TestTemplate)
#include "test_template.moc"
