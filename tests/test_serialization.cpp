// ---------------------------------------------------------------------------
// Unit tests: the .occard project format.
//
// The central assertion is a full round trip: build a document, save it, load it
// into a fresh document, and compare every object's JSON. Because the JSON is
// exactly what the undo layer and the serializer both use, equality there means
// nothing was lost. The remaining checks cover the failure modes that matter:
// a project from a newer build, a damaged file, and a filesystem error.
// ---------------------------------------------------------------------------
#include "core/CardDocument.h"
#include "core/ObjectFactory.h"
#include "core/QrObject.h"
#include "core/TextObject.h"
#include "project/AssetStore.h"
#include "project/ProjectSerializer.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace occ;

class TestSerialization : public QObject
{
    Q_OBJECT
private slots:
    void roundTripPreservesEveryObject();
    void roundTripPreservesGeometryAndMetadata();
    void roundTripPreservesBackground();
    void roundTripPreservesAssets();
    void refusesANewerFormatVersion();
    void rejectsGarbageData();
    void acceptsABareProjectJson();
    void savingIsAtomicAndLeavesNoTempFile();
    void loadingAMissingFileFailsCleanly();
    void printSettingsSurviveARoundTrip();
    void peekReadsMetadataWithoutADocument();
};

namespace {

// Builds a document with two sides, one object of several types, and a
// background, so the round trip exercises the real breadth of the format.
// Fills `doc` with two sides, one object of several types and a background, so
// the round trip exercises the real breadth of the format. CardDocument is not
// copyable, so it is filled in place.
void fillRichDocument(CardDocument &doc)
{
    CardGeometry geometry = CardGeometry::isoId1();
    geometry.setRenderDpi(600);
    geometry.setBleedMm(0.5);
    doc.setGeometry(geometry);
    doc.setAuthor(QStringLiteral("QA"));
    doc.setNotes(QStringLiteral("round trip fixture"));

    CardSide::Background background;
    background.kind = CardSide::Background::Kind::LinearGradient;
    background.color = QColor(255, 255, 255);
    background.color2 = QColor(0, 64, 128);
    background.angleDeg = 45.0;
    doc.front().setBackground(background);

    auto text = std::make_unique<TextObject>();
    text->setName(QStringLiteral("Employee Name"));
    text->setText(QStringLiteral("{{name}}"));
    text->setRectMm(QRectF(30.0, 10.0, 50.0, 8.0));
    text->setFontSizePt(9.5);
    text->setBold(true);
    doc.front().insertObject(std::move(text));

    auto qr = std::make_unique<QrObject>();
    qr->setName(QStringLiteral("QR Code"));
    qr->setData(QStringLiteral("{{employee_id}}"));
    qr->setErrorCorrection(QrErrorCorrection::High);
    qr->setRectMm(QRectF(60.0, 25.0, 20.0, 20.0));
    qr->setRotationDeg(12.5);
    qr->setOpacity(0.85);
    doc.front().insertObject(std::move(qr));

    auto backShape = ObjectFactory::create(ObjectType::Shape);
    backShape->setName(QStringLiteral("Back panel"));
    backShape->setRectMm(QRectF(2.0, 2.0, 81.0, 49.0));
    doc.back().insertObject(std::move(backShape));
}

QVector<QJsonObject> snapshot(const CardSide &side)
{
    QVector<QJsonObject> result;
    for (CardObject *object : side.objects())
        result.append(object->toJson());
    return result;
}

} // namespace

void TestSerialization::roundTripPreservesEveryObject()
{
    CardDocument original; fillRichDocument(original);
    const QVector<QJsonObject> frontBefore = snapshot(original.front());
    const QVector<QJsonObject> backBefore = snapshot(original.back());

    QByteArray data;
    const ProjectSerializer::SaveResult saved = ProjectSerializer::saveToData(original, &data);
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY(!data.isEmpty());

    CardDocument restored;
    const ProjectSerializer::LoadResult loaded =
        ProjectSerializer::loadFromData(&restored, data, QStringLiteral("test.occard"));
    QVERIFY2(loaded.ok, qPrintable(loaded.error));

    QCOMPARE(restored.front().count(), original.front().count());
    QCOMPARE(restored.back().count(), original.back().count());
    QCOMPARE(snapshot(restored.front()), frontBefore);
    QCOMPARE(snapshot(restored.back()), backBefore);
}


void TestSerialization::roundTripPreservesGeometryAndMetadata()
{
    CardDocument original; fillRichDocument(original);
    QByteArray data;
    QVERIFY(ProjectSerializer::saveToData(original, &data).ok);

    CardDocument restored;
    QVERIFY(ProjectSerializer::loadFromData(&restored, data).ok);

    QCOMPARE(restored.geometry().widthMm(), original.geometry().widthMm());
    QCOMPARE(restored.geometry().heightMm(), original.geometry().heightMm());
    QCOMPARE(restored.geometry().renderDpi(), original.geometry().renderDpi());
    QCOMPARE(restored.geometry().bleedMm(), original.geometry().bleedMm());
    QCOMPARE(restored.author(), original.author());
    QCOMPARE(restored.notes(), original.notes());

    // A freshly loaded document is not dirty: there is nothing to save yet.
    QVERIFY(!restored.isDirty());
}

void TestSerialization::roundTripPreservesBackground()
{
    CardDocument original; fillRichDocument(original);
    QByteArray data;
    QVERIFY(ProjectSerializer::saveToData(original, &data).ok);

    CardDocument restored;
    QVERIFY(ProjectSerializer::loadFromData(&restored, data).ok);

    const CardSide::Background &before = original.front().background();
    const CardSide::Background &after = restored.front().background();
    QCOMPARE(int(after.kind), int(before.kind));
    QCOMPARE(after.color.name(), before.color.name());
    QCOMPARE(after.color2.name(), before.color2.name());
    QCOMPARE(after.angleDeg, before.angleDeg);
}

void TestSerialization::roundTripPreservesAssets()
{
    // The photo bytes must travel inside the container, so a design keeps
    // working after the original file has been deleted or the folder moved.
    QImage picture(64, 64, QImage::Format_ARGB32_Premultiplied);
    picture.fill(QColor(20, 140, 220));

    CardDocument original;
    original.setGeometry(CardGeometry::isoId1());
    QString error;
    const QString assetId =
        original.assets()->addImage(picture, QStringLiteral("photo.png"), &error);
    QVERIFY2(!assetId.isEmpty(), qPrintable(error));

    auto photo = ObjectFactory::create(ObjectType::Photo);
    QVERIFY(photo != nullptr);
    photo->setRectMm(QRectF(5.0, 5.0, 24.0, 32.0));
    QJsonObject json = photo->toJson();
    QJsonObject properties = json.value(QStringLiteral("properties")).toObject();
    properties.insert(QStringLiteral("assetId"), assetId);
    json.insert(QStringLiteral("properties"), properties);
    QVERIFY2(photo->fromJson(json, &error), qPrintable(error));
    original.front().insertObject(std::move(photo));

    QByteArray data;
    QVERIFY(ProjectSerializer::saveToData(original, &data).ok);

    CardDocument restored;
    QVERIFY(ProjectSerializer::loadFromData(&restored, data).ok);
    QCOMPARE(restored.assets()->count(), 1);
    QVERIFY(restored.assets()->has(assetId));
    const QImage decoded = restored.assets()->image(assetId);
    QVERIFY(!decoded.isNull());
    QCOMPARE(decoded.size(), picture.size());
}


void TestSerialization::refusesANewerFormatVersion()
{
    // A project written by a future build must be refused rather than
    // misinterpreted, and the file must be left untouched.
    const QByteArray data =
        R"({"formatVersion":999,"application":{"name":"OpenCardCanvas","version":"9.9.9"},)"
        R"("geometry":{"preset":"iso-id1","widthMm":85.6,"heightMm":53.98,"renderDpi":300,"bleedMm":0},)"
        R"("front":{"objects":[]},"back":{"objects":[]}})";

    CardDocument doc;
    const ProjectSerializer::LoadResult loaded =
        ProjectSerializer::loadFromData(&doc, data, QStringLiteral("future.occard"));
    QVERIFY2(!loaded.ok, "a newer format version must not be loaded");
    QVERIFY(loaded.error.contains(QStringLiteral("newer"), Qt::CaseInsensitive));
    QCOMPARE(loaded.sourceFormatVersion, 999);
}

void TestSerialization::rejectsGarbageData()
{
    const QByteArray candidates[] = {
        QByteArray(),
        QByteArray("not a project at all"),
        QByteArray("{ this is not json }"),
        QByteArray("[1,2,3]"),
    };
    for (const QByteArray &candidate : candidates) {
        // A silent failure is the one outcome that is never acceptable, because
        // it would lose work. Either the load fails with an explanation, or it
        // must not claim success.
        CardDocument fresh;
        const ProjectSerializer::LoadResult loaded =
            ProjectSerializer::loadFromData(&fresh, candidate);
        QVERIFY2(!loaded.ok, qPrintable(QStringLiteral("garbage was accepted: %1")
                                            .arg(QString::fromLatin1(candidate))));
        QVERIFY2(!loaded.error.isEmpty(), candidate.constData());
    }
}

void TestSerialization::acceptsABareProjectJson()
{
    // Hand written and script generated projects are supported: plain JSON in a
    // file named .occard loads, and the reader reports how it was interpreted.
    const QByteArray plain =
        R"({"formatVersion":1,)"
        R"("geometry":{"preset":"iso-id1","widthMm":85.6,"heightMm":53.98,)"
        R"("renderDpi":300,"bleedMm":0},)"
        R"("front":{"objects":[]},"back":{"objects":[]}})";

    CardDocument doc;
    const ProjectSerializer::LoadResult loaded = ProjectSerializer::loadFromData(&doc, plain);
    QVERIFY2(loaded.ok, qPrintable(loaded.error));
    QCOMPARE(doc.geometry().widthMm(), 85.6);
    QCOMPARE(doc.front().count(), 0);
}

void TestSerialization::savingIsAtomicAndLeavesNoTempFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("project.occard"));

    CardDocument doc; fillRichDocument(doc);
    const ProjectSerializer::SaveResult saved = ProjectSerializer::save(doc, path);
    QVERIFY2(saved.ok, qPrintable(saved.error));
    QVERIFY(QFile::exists(path));
    QVERIFY2(!QFile::exists(path + QStringLiteral(".tmp")),
             "an atomic save must not leave its temporary file behind");

    // Overwriting an existing project must succeed as well.
    QVERIFY2(ProjectSerializer::save(doc, path).ok, "overwriting a project failed");

    CardDocument reloaded;
    QVERIFY(ProjectSerializer::load(&reloaded, path).ok);
    QCOMPARE(reloaded.front().count(), doc.front().count());
}

void TestSerialization::loadingAMissingFileFailsCleanly()
{
    CardDocument doc;
    const ProjectSerializer::LoadResult loaded = ProjectSerializer::load(
        &doc, QStringLiteral("C:/definitely/not/here/nothing.occard"));
    QVERIFY(!loaded.ok);
    QVERIFY(!loaded.error.isEmpty());
}

void TestSerialization::printSettingsSurviveARoundTrip()
{
    // A frozen CardDocument deliberately knows nothing about printers, so the
    // "last used printer" travels in the serializer's own settings value.
    ProjectSerializer::PrintSettings settings;
    settings.printer = QStringLiteral("XPS Card Printer");
    settings.copies = 3;
    settings.duplex = true;
    settings.backEnabled = true;
    settings.frontTopcoat = QStringLiteral("Except");
    settings.hopper = QStringLiteral("1");
    ProjectSerializer::setPrintSettings(settings);

    CardDocument doc; fillRichDocument(doc);
    QByteArray data;
    QVERIFY(ProjectSerializer::saveToData(doc, &data).ok);

    // Reset, then load: the stored values must come back.
    ProjectSerializer::setPrintSettings(ProjectSerializer::PrintSettings{});
    CardDocument restored;
    QVERIFY(ProjectSerializer::loadFromData(&restored, data).ok);

    const ProjectSerializer::PrintSettings readBack = ProjectSerializer::printSettings();
    QCOMPARE(readBack.printer, settings.printer);
    QCOMPARE(readBack.copies, settings.copies);
    QCOMPARE(readBack.duplex, settings.duplex);
    QCOMPARE(readBack.backEnabled, settings.backEnabled);
    QCOMPARE(readBack.frontTopcoat, settings.frontTopcoat);
}

void TestSerialization::peekReadsMetadataWithoutADocument()
{
    CardDocument doc; fillRichDocument(doc);
    QByteArray data;
    QVERIFY(ProjectSerializer::saveToData(doc, &data).ok);

    // The recents list and the template browser call peek(), so they never build
    // a whole document just to display a name.
    const ProjectSerializer::PeekResult peeked = ProjectSerializer::peekData(data);
    QVERIFY2(peeked.ok, qPrintable(peeked.error));
    QCOMPARE(peeked.formatVersion, ProjectSerializer::kFormatVersion);
    QVERIFY(peeked.placeholders.contains(QStringLiteral("name")));

    const ProjectSerializer::PeekResult missing =
        ProjectSerializer::peek(QStringLiteral("C:/no/such/file.occard"));
    QVERIFY(!missing.ok);
    QVERIFY(!missing.error.isEmpty());
}

QTEST_MAIN(TestSerialization)
#include "test_serialization.moc"
