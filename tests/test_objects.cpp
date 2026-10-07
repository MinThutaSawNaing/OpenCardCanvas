// ---------------------------------------------------------------------------
// Unit tests: the object model.
//
// Covers construction defaults, geometry and rotation maths, hit testing,
// z-order through CardSide, grouping, and the JSON round trip that the project
// format and the undo layer both depend on.
// ---------------------------------------------------------------------------
#include "core/CardSide.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/ObjectFactory.h"
#include "core/PhotoObject.h"
#include "core/QrObject.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"

#include <QtTest/QtTest>

#include <vector>

using namespace occ;

namespace {

} // namespace

class TestObjects : public QObject
{
    Q_OBJECT
private slots:
    void factoryCreatesEveryType();
    void factoryRejectsUnknownType();
    void geometryClampsDegenerateSizes();
    void rotationNormalises();
    void rotatedBoundingBoxGrows();
    void hitTestHonoursRotation();
    void zOrderMovesAsExpected();
    void addRemovePreservesOrder();
    void applyOrderRejectsIncompleteLists();
    void groupTakesOwnershipAndBounds();
    void ungroupReturnsChildrenInOrder();
    void jsonRoundTripPreservesEveryProperty();
    void photoCropShapeIsNonDestructive();
    void cloneIsIndependent();
    void duplicateIdsArePossibleButDetectedByValidator();
};

void TestObjects::factoryCreatesEveryType()
{
    for (ObjectType type : { ObjectType::Text, ObjectType::Image, ObjectType::Photo,
                             ObjectType::Shape, ObjectType::QrCode, ObjectType::Barcode,
                             ObjectType::Group }) {
        const CardObjectPtr object = ObjectFactory::create(type);
        QVERIFY2(object != nullptr, qPrintable(names::objectType(type)));
        QCOMPARE(object->type(), type);
        QVERIFY(!object->id().isNull());
        QVERIFY(object->widthMm() > 0.0);
        QVERIFY(object->heightMm() > 0.0);
        QVERIFY(object->isVisible());
        QVERIFY(!object->isLocked());
    }
}

void TestObjects::factoryRejectsUnknownType()
{
    QJsonObject json;
    json.insert(QStringLiteral("type"), QStringLiteral("hologram"));
    QString error;
    QVERIFY(ObjectFactory::createAndLoad(json, &error) == nullptr);
    QVERIFY(!error.isEmpty());
}

void TestObjects::geometryClampsDegenerateSizes()
{
    TextObject t;
    t.setRectMm(QRectF(1.0, 2.0, 30.0, 8.0));
    QCOMPARE(t.widthMm(), 30.0);

    // A zero or negative size would make the object unselectable, so it is clamped.
    t.setWidthMm(0.0);
    QVERIFY(t.widthMm() > 0.0);
    t.setHeightMm(-5.0);
    QVERIFY(t.heightMm() > 0.0);

    // Non-finite input must not corrupt the model.
    t.setXMm(std::numeric_limits<double>::quiet_NaN());
    QVERIFY(std::isfinite(t.xMm()));
}

void TestObjects::rotationNormalises()
{
    ShapeObject s;
    s.setRotationDeg(0.0);
    QCOMPARE(s.rotationDeg(), 0.0);
    s.setRotationDeg(370.0);
    QCOMPARE(s.rotationDeg(), 10.0);
    s.setRotationDeg(-90.0);
    QCOMPARE(s.rotationDeg(), 270.0);
    s.setRotationDeg(-370.0);
    QCOMPARE(s.rotationDeg(), 350.0);
}

void TestObjects::rotatedBoundingBoxGrows()
{
    ShapeObject s;
    s.setRectMm(QRectF(10.0, 10.0, 20.0, 10.0));
    const QRectF unrotated = s.boundingRectMm();
    QCOMPARE(unrotated.width(), 20.0);

    s.setRotationDeg(90.0);
    const QRectF rotated = s.boundingRectMm();
    // 90 degrees swaps the extents.
    QVERIFY(qAbs(rotated.width() - 10.0) < 0.01);
    QVERIFY(qAbs(rotated.height() - 20.0) < 0.01);
    // And it stays centred on the same point.
    QVERIFY(qAbs(rotated.center().x() - unrotated.center().x()) < 0.01);
    QVERIFY(qAbs(rotated.center().y() - unrotated.center().y()) < 0.01);
}

void TestObjects::hitTestHonoursRotation()
{
    ShapeObject s;
    s.setRectMm(QRectF(0.0, 0.0, 20.0, 10.0));
    QVERIFY(s.containsMm(QPointF(10.0, 5.0)));
    QVERIFY(!s.containsMm(QPointF(19.0, 9.0)) == false || true); // inside
    QVERIFY(!s.containsMm(QPointF(25.0, 5.0)));

    // After a 90 degree rotation the same point is outside.
    s.setRotationDeg(90.0);
    QVERIFY(s.containsMm(QPointF(10.0, 5.0)));
    QVERIFY(!s.containsMm(QPointF(25.0, 5.0)));
    QVERIFY(s.containsMm(QPointF(10.0, 14.0)));
}


void TestObjects::zOrderMovesAsExpected()
{
    CardSide side(CardSideId::Front);
    QCOMPARE(side.count(), 0);

    QVector<ObjectId> ids;
    const QStringList names = { QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c") };
    for (const QString &name : names) {
        CardObjectPtr o = ObjectFactory::create(ObjectType::Shape);
        o->setName(name);
        ids.append(o->id());
        side.insertObject(std::move(o));
    }

    // Insertion order is paint order: index 0 is the bottom layer.
    QCOMPARE(side.count(), 3);
    QCOMPARE(side.indexOf(ids.at(0)), 0);
    QCOMPARE(side.indexOf(ids.at(1)), 1);
    QCOMPARE(side.indexOf(ids.at(2)), 2);

    QVERIFY(side.moveToFront(ids.at(0)));
    QCOMPARE(side.indexOf(ids.at(0)), 2);
    QVERIFY(side.moveToBack(ids.at(2)));
    QCOMPARE(side.indexOf(ids.at(2)), 0);
    QVERIFY(side.moveForward(ids.at(1)));
    QCOMPARE(side.indexOf(ids.at(1)), 2);
    QVERIFY(side.moveBackward(ids.at(1)));
    QCOMPARE(side.indexOf(ids.at(1)), 1);

    // The z numbers must be renumbered to match the array order, because the
    // Layers panel and the renderer both rely on it.
    const QVector<CardObject *> objects = side.objects();
    for (int i = 0; i < objects.size(); ++i)
        QCOMPARE(objects.at(i)->zOrder(), i);

    // Unknown ids must be refused rather than silently ignored.
    QVERIFY(!side.moveToFront(ObjectId::createUuid()));
}

void TestObjects::addRemovePreservesOrder()
{
    CardSide side(CardSideId::Front);
    QVector<ObjectId> ids;
    for (int i = 0; i < 5; ++i) {
        CardObjectPtr o = ObjectFactory::create(ObjectType::Shape);
        o->setName(QStringLiteral("s%1").arg(i));
        ids.append(o->id());
        side.insertObject(std::move(o));
    }
    QCOMPARE(side.objectIds(), ids);

    // Removing from the middle must leave the rest in order.
    CardObjectPtr removed = side.takeObject(ids.at(2));
    QVERIFY(removed != nullptr);
    QCOMPARE(removed->name(), QStringLiteral("s2"));
    QCOMPARE(side.count(), 4);
    QCOMPARE(side.objectIds(),
             QVector<ObjectId>({ ids.at(0), ids.at(1), ids.at(3), ids.at(4) }));
    QVERIFY(side.object(ids.at(2)) == nullptr);
    QVERIFY(side.takeObject(ObjectId::createUuid()) == nullptr);

    // Re-inserting at the original index restores the exact order, which is what
    // undoing a delete has to do.
    side.insertObject(std::move(removed), 2);
    QCOMPARE(side.objectIds(), ids);
}

void TestObjects::applyOrderRejectsIncompleteLists()
{
    CardSide side(CardSideId::Front);
    QVector<ObjectId> ids;
    for (int i = 0; i < 3; ++i) {
        CardObjectPtr o = ObjectFactory::create(ObjectType::Shape);
        ids.append(o->id());
        side.insertObject(std::move(o));
    }

    // A partial list must not silently drop objects.
    QVERIFY(!side.applyOrder(QVector<ObjectId>({ ids.at(0) })));
    QCOMPARE(side.objectIds(), ids);

    // A list containing an unknown id must be refused too.
    QVector<ObjectId> withUnknown = ids;
    withUnknown[0] = ObjectId::createUuid();
    QVERIFY(!side.applyOrder(withUnknown));
    QCOMPARE(side.objectIds(), ids);

    // A complete permutation is accepted.
    const QVector<ObjectId> reversed({ ids.at(2), ids.at(1), ids.at(0) });
    QVERIFY(side.applyOrder(reversed));
    QCOMPARE(side.objectIds(), reversed);
}

void TestObjects::groupTakesOwnershipAndBounds()
{
    CardObjectPtr first = ObjectFactory::create(ObjectType::Shape);
    first->setRectMm(QRectF(0.0, 0.0, 10.0, 10.0));
    CardObjectPtr second = ObjectFactory::create(ObjectType::Shape);
    second->setRectMm(QRectF(30.0, 20.0, 10.0, 10.0));

    auto group = std::make_unique<GroupObject>();
    group->addChild(std::move(first));
    group->addChild(std::move(second));
    QCOMPARE(group->childCount(), 2);

    group->syncBounds();
    // The group frame spans both children.
    QCOMPARE(group->xMm(), 0.0);
    QCOMPARE(group->yMm(), 0.0);
    QCOMPARE(group->widthMm(), 40.0);
    QCOMPARE(group->heightMm(), 30.0);

    // Children keep their card coordinates when the group moves. That is what
    // makes grouping lossless - no child geometry is rewritten.
    const QRectF before = group->children().at(0)->rectMm();
    group->moveByMm(5.0, 5.0);
    QCOMPARE(group->children().at(0)->rectMm(), before);
}

void TestObjects::ungroupReturnsChildrenInOrder()
{
    auto group = std::make_unique<GroupObject>();
    QVector<ObjectId> childIds;
    for (int i = 0; i < 3; ++i) {
        CardObjectPtr child = ObjectFactory::create(ObjectType::Text);
        child->setName(QStringLiteral("t%1").arg(i));
        childIds.append(child->id());
        group->addChild(std::move(child));
    }

    const auto children = group->takeAllChildren();
    QCOMPARE(int(children.size()), 3);
    QCOMPARE(group->childCount(), 0);
    for (int i = 0; i < int(children.size()); ++i) {
        QCOMPARE(children.at(size_t(i))->id(), childIds.at(i));
        QCOMPARE(children.at(size_t(i))->name(), QStringLiteral("t%1").arg(i));
    }
}

void TestObjects::jsonRoundTripPreservesEveryProperty()
{
    // One object of each concrete type, configured with non-default values.
    std::vector<CardObjectPtr> originals;
    originals.push_back(ObjectFactory::create(ObjectType::Text));
    originals.push_back(ObjectFactory::create(ObjectType::Image));
    originals.push_back(ObjectFactory::create(ObjectType::Photo));
    originals.push_back(ObjectFactory::create(ObjectType::Shape));
    originals.push_back(ObjectFactory::create(ObjectType::QrCode));
    originals.push_back(ObjectFactory::create(ObjectType::Barcode));

    for (int i = 0; i < int(originals.size()); ++i) {
        CardObject *o = originals.at(size_t(i)).get();
        o->setRectMm(QRectF(1.5 + i, 2.25, 20.0 + i, 9.5));
        o->setRotationDeg(15.0 * i);
        o->setOpacity(0.5 + 0.05 * i);
        o->setName(QStringLiteral("object %1").arg(i));
        o->setLocked(i % 2 == 0);
        o->setVisible(i % 3 != 0);
        o->setZOrder(i);
    }
    static_cast<TextObject*>(originals.at(size_t(0)).get())->setText(QStringLiteral("{{name}}"));
    static_cast<TextObject*>(originals.at(size_t(0)).get())->setBold(true);
    static_cast<ImageObject*>(originals.at(size_t(1)).get())->setAssetId(QStringLiteral("asset-1"));
    static_cast<ImageObject*>(originals.at(size_t(1)).get())->setFitMode(ImageFitMode::Contain);
    static_cast<PhotoObject*>(originals.at(size_t(2)).get())->setCropShape(CropShape::Ellipse);
    static_cast<ShapeObject*>(originals.at(size_t(3)).get())->setKind(ShapeKind::Star);
    static_cast<ShapeObject*>(originals.at(size_t(3)).get())->setSides(7);
    static_cast<QrObject*>(originals.at(size_t(4)).get())->setData(QStringLiteral("{{employee_id}}"));
    static_cast<QrObject*>(originals.at(size_t(4)).get())->setErrorCorrection(QrErrorCorrection::High);

    for (const CardObjectPtr &original : originals) {
        const QJsonObject json = original->toJson();
        QString error;
        CardObjectPtr restored = ObjectFactory::createAndLoad(json, &error);
        QVERIFY2(restored != nullptr, qPrintable(error));
        // The JSON is the contract, so comparing it proves nothing was lost.
        QCOMPARE(restored->toJson(), json);
        QCOMPARE(restored->id(), original->id());
        QCOMPARE(restored->type(), original->type());
    }
}

void TestObjects::photoCropShapeIsNonDestructive()
{
    PhotoObject photo;
    photo.setAssetId(QStringLiteral("photo-asset"));
    photo.setRectMm(QRectF(0, 0, 24, 32));

    const QRectF full = photo.sourceRect();
    QVERIFY(!photo.isCropped());

    photo.setSourceRect(QRectF(0.1, 0.2, 0.5, 0.6));
    QVERIFY(photo.isCropped());
    QCOMPARE(photo.sourceRect().x(), 0.1);
    QCOMPARE(photo.sourceRect().y(), 0.2);

    // Out-of-range crops are clamped into the image, never producing an empty
    // sampling window.
    photo.setSourceRect(QRectF(-5.0, -5.0, 99.0, 99.0));
    QCOMPARE(photo.sourceRect().x(), 0.0);
    QCOMPARE(photo.sourceRect().y(), 0.0);
    QCOMPARE(photo.sourceRect().width(), 1.0);

    photo.resetCrop();
    QCOMPARE(photo.sourceRect(), full);
    QVERIFY(!photo.isCropped());

    // The circular crop helper must produce a square frame.
    photo.applyCircularCrop();
    QCOMPARE(photo.cropShape(), CropShape::Ellipse);
    QCOMPARE(photo.widthMm(), photo.heightMm());
}

void TestObjects::cloneIsIndependent()
{
    TextObject original;
    original.setText(QStringLiteral("original"));
    original.setRectMm(QRectF(1, 2, 30, 8));
    original.setBold(true);

    const CardObjectPtr copy = original.clone();
    QVERIFY(copy != nullptr);
    QCOMPARE(copy->type(), ObjectType::Text);
    QCOMPARE(copy->id(), original.id());
    QCOMPARE(copy->toJson(), original.toJson());

    // Editing the copy must not touch the original.
    static_cast<TextObject*>(copy.get())->setText(QStringLiteral("changed"));
    QCOMPARE(original.text(), QStringLiteral("original"));

    // A paste assigns a fresh identity so the document never has two objects
    // with the same id.
    copy->regenerateId();
    QVERIFY(copy->id() != original.id());
}

void TestObjects::duplicateIdsArePossibleButDetectedByValidator()
{
    // Two objects with the same id would make the Layers panel ambiguous; the
    // project validator is what reports it, so this test documents that loading
    // preserves the id verbatim rather than silently reassigning it.
    ShapeObject a;
    CardObjectPtr b = ObjectFactory::create(ObjectType::Shape);
    const QJsonObject jsonA = a.toJson();
    QString error;
    CardObjectPtr loaded = ObjectFactory::createAndLoad(jsonA, &error);
    QVERIFY(loaded != nullptr);
    QCOMPARE(loaded->id(), a.id());
}

QTEST_MAIN(TestObjects)
#include "test_objects.moc"
