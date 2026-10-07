// ---------------------------------------------------------------------------
// Unit tests: error handling.
//
// The rule this file enforces is that nothing fails silently and nothing fails
// with a raw technical dump: every rejected input produces a message a user
// could act on, and no bad input crashes the process.
// ---------------------------------------------------------------------------
#include "core/CardDocument.h"
#include "core/CardGeometry.h"
#include "core/ObjectFactory.h"
#include "core/QrObject.h"
#include "core/TextObject.h"
#include "project/AssetStore.h"
#include "project/ProjectSerializer.h"
#include "project/ProjectValidator.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace occ;

class TestErrors : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() { QVERIFY(true); }

    void factoryRejectsUnknownTypeWithAMessage();
    void factoryRejectsMissingProperties();
    void geometryRejectsOutOfRangeValues();
    void assetStoreRejectsAMissingFile();
    void assetStoreRejectsNonImageData();
    void validatorAcceptsACleanDocument();
    void validatorReportsAnEmptyQrCode();
    void validatorReportsTheObjectThatIsWrong();
    void validatorSummaryIsHumanReadable();
    void loadingAProjectWithAnUnknownObjectFailsClearly();
    void messagesNeverContainRawPathsAsTheOnlyExplanation();
};

void TestErrors::factoryRejectsUnknownTypeWithAMessage()
{
    QJsonObject json;
    json.insert(QStringLiteral("type"), QStringLiteral("hologram"));
    json.insert(QStringLiteral("rectMm"), QJsonObject{});
    QString error;
    const CardObjectPtr object = ObjectFactory::createAndLoad(json, &error);
    QVERIFY(object == nullptr);
    QVERIFY2(!error.isEmpty(), "an unknown object type produced no explanation");
}

void TestErrors::factoryRejectsMissingProperties()
{
    TextObject source;
    QJsonObject json = source.toJson();
    json.remove(QStringLiteral("properties"));
    QString error;
    QVERIFY(ObjectFactory::createAndLoad(json, &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void TestErrors::geometryRejectsOutOfRangeValues()
{
    QString error;
    QJsonObject json;
    json.insert(QStringLiteral("widthMm"), 5000.0);   // beyond any real card
    json.insert(QStringLiteral("heightMm"), 53.98);
    json.insert(QStringLiteral("renderDpi"), 300);
    json.insert(QStringLiteral("bleedMm"), 0.0);

    CardGeometry geometry;
    QVERIFY2(!geometry.fromJson(json, &error), "an impossible card size was accepted");
    QVERIFY(!error.isEmpty());

    // A numeric but nonsensical DPI must be rejected too.
    QJsonObject badDpi;
    badDpi.insert(QStringLiteral("widthMm"), 85.6);
    badDpi.insert(QStringLiteral("heightMm"), 53.98);
    badDpi.insert(QStringLiteral("renderDpi"), 100000);
    badDpi.insert(QStringLiteral("bleedMm"), 0.0);
    QVERIFY(!geometry.fromJson(badDpi, &error));
    QVERIFY(!error.isEmpty());
}

void TestErrors::assetStoreRejectsAMissingFile()
{
    AssetStore store;
    QString error;
    const QString id = store.addImageFile(QStringLiteral("C:/no/such/photo.png"), &error);
    QVERIFY(id.isEmpty());
    QVERIFY2(!error.isEmpty(), "a missing image produced no explanation");
    QCOMPARE(store.count(), 0);

    // A directory is not an image either.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    error.clear();
    QVERIFY(store.addImageFile(dir.path(), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestErrors::assetStoreRejectsNonImageData()
{
    AssetStore store;
    QString error;
    // Text pretending to be a photo must not enter the store.
    const QString id = store.addImageData(QByteArray("this is definitely not a png"),
                                          QStringLiteral("notes.txt"), &error);
    QVERIFY(id.isEmpty());
    QVERIFY(!error.isEmpty());
    QCOMPARE(store.count(), 0);
}

void TestErrors::validatorAcceptsACleanDocument()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    auto text = std::make_unique<TextObject>();
    text->setText(QStringLiteral("Employee"));
    text->setRectMm(QRectF(10, 10, 40, 8));
    doc.front().insertObject(std::move(text));

    const Report report = ProjectValidator::validate(doc);
    QVERIFY2(!report.hasErrors(),
             qPrintable(QStringLiteral("a clean document reported: %1").arg(report.summary())));
}


void TestErrors::validatorReportsAnEmptyQrCode()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    auto qr = std::make_unique<QrObject>();
    qr->setData(QString());                      // nothing to encode
    qr->setRectMm(QRectF(60, 10, 20, 20));
    doc.front().insertObject(std::move(qr));

    const Report report = ProjectValidator::validate(doc);
    QVERIFY2(!report.issues.isEmpty(), "an empty QR code was not reported");
    QVERIFY2(!report.summary().isEmpty(), "the report had no human readable summary");
}

void TestErrors::validatorReportsTheObjectThatIsWrong()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    auto qr = std::make_unique<QrObject>();
    qr->setData(QString());
    qr->setRectMm(QRectF(60, 10, 20, 20));
    const ObjectId id = qr->id();
    doc.front().insertObject(std::move(qr));

    const Report report = ProjectValidator::validate(doc);
    // Every issue must be attributable, so the UI can highlight the object
    // rather than merely announcing that something is wrong.
    bool attributed = false;
    for (const Issue &issue : report.issues) {
        if (issue.objectId == id)
            attributed = true;
        QVERIFY2(!issue.message.isEmpty(), "an issue had no message");
    }
    QVERIFY2(attributed, "no issue was attributed to the offending object");
}

void TestErrors::validatorSummaryIsHumanReadable()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());

    const Report clean = ProjectValidator::validate(doc);
    QVERIFY(!clean.summary().isEmpty());
    QCOMPARE(clean.errorCount(), 0);

    // Severity labels are shown next to each issue in the diagnostics dialog.
    QVERIFY(!ProjectValidator::severityText(Issue::Severity::Error).isEmpty());
    QVERIFY(!ProjectValidator::severityText(Issue::Severity::Warning).isEmpty());
    QVERIFY(!ProjectValidator::severityText(Issue::Severity::Info).isEmpty());
}

void TestErrors::loadingAProjectWithAnUnknownObjectFailsClearly()
{
    // A project containing an object type this build does not know about must
    // fail with an explanation rather than loading a document with a hole in it.
    const QByteArray data =
        R"({"formatVersion":1,)"
        R"("geometry":{"preset":"iso-id1","widthMm":85.6,"heightMm":53.98,"renderDpi":300,"bleedMm":0},)"
        R"("front":{"objects":[{"type":"hologram","id":"11111111-1111-1111-1111-111111111111",)"
        R"("rectMm":{"x":1,"y":1,"w":10,"h":10},"properties":{}}]},"back":{"objects":[]}})";

    CardDocument doc;
    const ProjectSerializer::LoadResult loaded =
        ProjectSerializer::loadFromData(&doc, data, QStringLiteral("odd.occard"));
    QVERIFY2(!loaded.ok, "a project with an unknown object type was accepted");
    QVERIFY(!loaded.error.isEmpty());
}

void TestErrors::messagesNeverContainRawPathsAsTheOnlyExplanation()
{
    // A user facing message must explain the problem in words. A bare path or a
    // bare error code is not an explanation.
    AssetStore store;
    QString error;
    store.addImageFile(QStringLiteral("C:/no/such/photo.png"), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY2(error.contains(QLatin1Char(' ')), "the message was not a sentence");

    CardDocument doc;
    const ProjectSerializer::LoadResult loaded =
        ProjectSerializer::load(&doc, QStringLiteral("C:/no/such/project.occard"));
    QVERIFY(!loaded.ok);
    QVERIFY2(loaded.error.contains(QLatin1Char(' ')),
             qPrintable(QStringLiteral("not an explanation: '%1'").arg(loaded.error)));
}

QTEST_MAIN(TestErrors)
#include "test_errors.moc"

