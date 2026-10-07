// ---------------------------------------------------------------------------
// Unit tests: undo / redo.
//
// The command layer is what makes Ctrl+Z trustworthy, so every command is
// exercised in both directions and the result is compared against the exact
// state from before the operation, not merely "something changed".
// ---------------------------------------------------------------------------
#include "commands/UndoCommands.h"
#include "core/CardDocument.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"

#include <QUndoStack>
#include <QtTest/QtTest>

using namespace occ;

class TestUndo : public QObject
{
    Q_OBJECT
private slots:
    void addIsUndoneAndRedoneExactly();
    void removeRestoresPaintOrder();
    void modifyCoversMoveResizeAndStyle();
    void modifyMergesConsecutiveEdits();
    void reorderIsUndoneExactly();
};

namespace {

QVector<QJsonObject> snapshot(const CardSide &side)
{
    QVector<QJsonObject> result;
    for (CardObject *object : side.objects())
        result.append(object->toJson());
    return result;
}

QJsonObject shapeJson(const QString &name, const QRectF &rect)
{
    ShapeObject shape;
    shape.setName(name);
    shape.setRectMm(rect);
    return shape.toJson();
}

} // namespace

void TestUndo::addIsUndoneAndRedoneExactly()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());
    QUndoStack stack;

    const QVector<QJsonObject> objects{ shapeJson(QStringLiteral("A"), QRectF(0, 0, 10, 10)),
                                        shapeJson(QStringLiteral("B"), QRectF(20, 0, 10, 10)) };

    stack.push(new AddObjectsCommand(&doc, CardSideId::Front, objects));
    QCOMPARE(doc.front().count(), 2);
    QCOMPARE(stack.count(), 1);
    // Every command must describe itself, because the Edit menu shows it.
    QVERIFY(!stack.undoText().isEmpty());

    stack.undo();
    QCOMPARE(doc.front().count(), 0);

    stack.redo();
    QCOMPARE(doc.front().count(), 2);
    for (int i = 0; i < objects.size(); ++i) {
        QCOMPARE(doc.front().objects().at(i)->name(),
                 objects.at(i).value(QStringLiteral("name")).toString());
    }
}

void TestUndo::removeRestoresPaintOrder()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());
    QUndoStack stack;

    QVector<QJsonObject> objects;
    for (int i = 0; i < 4; ++i)
        objects.append(shapeJson(QStringLiteral("s%1").arg(i), QRectF(i * 5.0, 0, 4, 4)));

    stack.push(new AddObjectsCommand(&doc, CardSideId::Front, objects));
    const QVector<QJsonObject> before = snapshot(doc.front());
    QCOMPARE(before.size(), 4);

    // Remove the middle two.
    const QVector<QJsonObject> toRemove{ before.at(1), before.at(2) };
    stack.push(new RemoveObjectsCommand(&doc, CardSideId::Front, toRemove, 1));
    QCOMPARE(doc.front().count(), 2);

    stack.undo();
    QCOMPARE(doc.front().count(), 4);
    // The order must be restored exactly, not appended at the end.
    QCOMPARE(snapshot(doc.front()), before);

    stack.redo();
    QCOMPARE(doc.front().count(), 2);
}

void TestUndo::modifyCoversMoveResizeAndStyle()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());
    QUndoStack stack;

    auto text = std::make_unique<TextObject>();
    text->setText(QStringLiteral("before"));
    text->setRectMm(QRectF(5, 5, 30, 8));
    const ObjectId id = text->id();
    doc.front().insertObject(std::move(text));

    const QJsonObject before = doc.front().object(id)->toJson();

    // One command must cover a geometry change and a property change together,
    // which is exactly what a single drag or one property edit produces.
    doc.front().object(id)->setRectMm(QRectF(20, 12, 40, 10));
    doc.front().object(id)->setRotationDeg(30.0);
    doc.front().object(id)->setOpacity(0.4);
    static_cast<TextObject *>(doc.front().object(id))->setText(QStringLiteral("after"));

    const QJsonObject after = doc.front().object(id)->toJson();
    stack.push(new ModifyObjectsCommand(&doc, CardSideId::Front,
                                        ModifyObjectsCommand::single(id, before),
                                        ModifyObjectsCommand::single(id, after),
                                        QStringLiteral("Change text")));

    stack.undo();
    QCOMPARE(doc.front().object(id)->toJson(), before);
    QCOMPARE(doc.front().object(id)->rectMm(), QRectF(5, 5, 30, 8));
    QCOMPARE(static_cast<TextObject *>(doc.front().object(id))->text(),
             QStringLiteral("before"));

    stack.redo();
    QCOMPARE(doc.front().object(id)->toJson(), after);
    QCOMPARE(doc.front().object(id)->rotationDeg(), 30.0);
}

void TestUndo::modifyMergesConsecutiveEdits()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());
    QUndoStack stack;

    auto shape = std::make_unique<ShapeObject>();
    shape->setRectMm(QRectF(0, 0, 10, 10));
    const ObjectId id = shape->id();
    doc.front().insertObject(std::move(shape));

    // Three consecutive edits sharing a merge key must collapse into one step,
    // so dragging a slider does not create two hundred undo entries.
    for (int step = 1; step <= 3; ++step) {
        const QJsonObject before = doc.front().object(id)->toJson();
        doc.front().object(id)->setXMm(double(step) * 5.0);
        const QJsonObject after = doc.front().object(id)->toJson();
        stack.push(new ModifyObjectsCommand(&doc, CardSideId::Front,
                                            ModifyObjectsCommand::single(id, before),
                                            ModifyObjectsCommand::single(id, after),
                                            QStringLiteral("Move"),
                                            QStringLiteral("x-slider")));
    }

    QCOMPARE(doc.front().object(id)->xMm(), 15.0);
    QCOMPARE(stack.count(), 1);

    // A single undo must return to where the drag started.
    stack.undo();
    QCOMPARE(doc.front().object(id)->xMm(), 0.0);
}

void TestUndo::reorderIsUndoneExactly()
{
    CardDocument doc;
    doc.setGeometry(CardGeometry::isoId1());
    QUndoStack stack;

    QVector<QJsonObject> objects;
    for (int i = 0; i < 3; ++i)
        objects.append(shapeJson(QStringLiteral("o%1").arg(i), QRectF(i * 10.0, 0, 8, 8)));
    stack.push(new AddObjectsCommand(&doc, CardSideId::Front, objects));

    const QVector<ObjectId> before = doc.front().objectIds();
    QCOMPARE(before.size(), 3);

    // Bring the bottom layer to the front, as the Arrange menu does.
    QVERIFY(doc.front().moveToFront(before.at(0)));
    const QVector<ObjectId> after = doc.front().objectIds();
    QVERIFY(before != after);

    stack.push(new ReorderObjectsCommand(&doc, CardSideId::Front, before, after,
                                         QStringLiteral("Bring to front")));

    stack.undo();
    QCOMPARE(doc.front().objectIds(), before);

    stack.redo();
    QCOMPARE(doc.front().objectIds(), after);
}

QTEST_MAIN(TestUndo)
#include "test_undo.moc"
