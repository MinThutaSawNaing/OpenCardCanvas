#include "canvas/CardCanvas.h"
#include "canvas/CanvasGeometry.h"
#include "core/CardDocument.h"
#include "core/ShapeObject.h"
#include "ui/LayersPanel.h"

#include <QCursor>
#include <QContextMenuEvent>
#include <QMenu>
#include <QTimer>
#include <QTreeWidget>
#include <QUndoStack>
#include <QtTest/QtTest>

using namespace occ;

class TestLayerUi : public QObject
{
    Q_OBJECT
private slots:
    void resizeCursors_data()
    {
        QTest::addColumn<int>("handle");
        QTest::addColumn<double>("rotation");
        QTest::addColumn<int>("cursor");
        QTest::newRow("top-left") << int(Handle::TopLeft) << 0.0 << int(Qt::SizeFDiagCursor);
        QTest::newRow("bottom-right") << int(Handle::BottomRight) << 0.0 << int(Qt::SizeFDiagCursor);
        QTest::newRow("top-right") << int(Handle::TopRight) << 0.0 << int(Qt::SizeBDiagCursor);
        QTest::newRow("bottom-left") << int(Handle::BottomLeft) << 0.0 << int(Qt::SizeBDiagCursor);
        QTest::newRow("rotated-corner") << int(Handle::TopLeft) << 90.0 << int(Qt::SizeBDiagCursor);
        QTest::newRow("top") << int(Handle::Top) << 0.0 << int(Qt::SizeVerCursor);
        QTest::newRow("right") << int(Handle::Right) << 0.0 << int(Qt::SizeHorCursor);
    }

    void resizeCursors()
    {
        QFETCH(int, handle);
        QFETCH(double, rotation);
        QFETCH(int, cursor);
        CardDocument document;
        CardCanvas canvas;
        canvas.setDocument(&document);
        auto object = std::make_unique<ShapeObject>();
        object->setRectMm(QRectF(20, 15, 25, 20));
        object->setRotationDeg(rotation);
        const auto id = object->id();
        document.front().insertObject(std::move(object));
        canvas.setSelection({ id });
        canvas.resize(700, 500);
        canvas.show();
        QTest::qWait(30);
        for (const auto &candidate : CanvasGeometry::handlesFor(
                 *document.front().object(id), canvas.pxPerMm(), canvas.originPx(), 8)) {
            if (int(candidate.id) != handle)
                continue;
            const QPointF pos = CanvasGeometry::mmToView(candidate.posMm, canvas.pxPerMm(), canvas.originPx());
            QMouseEvent move(QEvent::MouseMove, pos, pos, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(&canvas, &move);
            QCOMPARE(int(canvas.cursor().shape()), cursor);
        }
    }

    void railClickPreservesSelectionAndUndo()
    {
        CardDocument document;
        CardCanvas canvas;
        QUndoStack stack;
        canvas.setDocument(&document);
        canvas.setUndoStack(&stack);
        canvas.addShape(ShapeKind::Rectangle);
        canvas.addShape(ShapeKind::Ellipse);
        const auto ids = document.front().objectIds();
        canvas.setSelection({ ids.first() });
        LayersPanel panel;
        panel.setDocument(&document);
        panel.setCanvas(&canvas);
        panel.resize(500, 300);
        panel.show();
        QTest::qWait(30);
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("LayersTree"));
        QVERIFY(tree);
        const auto row = tree->visualItemRect(tree->topLevelItem(0));
        const QPoint click(tree->columnViewportPosition(0) + tree->columnWidth(0) / 2, row.center().y());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, click);
        QVERIFY(!document.front().object(ids.last())->isVisible());
        QCOMPARE(canvas.selectionIds(), QVector<ObjectId>{ ids.first() });
        stack.undo();
        QVERIFY(document.front().object(ids.last())->isVisible());
        stack.redo();
        QVERIFY(!document.front().object(ids.last())->isVisible());
        const int count = stack.count();
        QMouseEvent doubleClick(QEvent::MouseButtonDblClick, click, click,
                                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &doubleClick);
        QVERIFY(!document.front().object(ids.last())->isVisible());
        QCOMPARE(stack.count(), count);
        QMouseEvent middleClick(QEvent::MouseButtonDblClick, click, click,
                                Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &middleClick);
        QVERIFY(!document.front().object(ids.last())->isVisible());
        QCOMPARE(stack.count(), count);
    }

    void renameAndStackingStayInSync()
    {
        CardDocument document;
        CardCanvas canvas;
        QUndoStack stack;
        canvas.setDocument(&document);
        canvas.setUndoStack(&stack);
        canvas.addShape(ShapeKind::Rectangle);
        canvas.addShape(ShapeKind::Ellipse);
        const auto ids = document.front().objectIds();
        LayersPanel panel;
        panel.setDocument(&document);
        panel.setCanvas(&canvas);
        panel.resize(500, 300);
        panel.show();
        QTest::qWait(30);
        auto *tree = panel.findChild<QTreeWidget *>(QStringLiteral("LayersTree"));
        QVERIFY(tree);
        tree->topLevelItem(0)->setText(2, QStringLiteral("Renamed layer"));
        QCOMPARE(document.front().object(ids.last())->name(), QStringLiteral("Renamed layer"));
        stack.undo();
        QVERIFY(document.front().object(ids.last())->name() != QStringLiteral("Renamed layer"));
        stack.redo();
        QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Renamed layer"));

        const auto row = tree->visualItemRect(tree->topLevelItem(0));
        const QPoint pos(tree->columnViewportPosition(2) + 30, row.center().y());
        bool menuSeen = false;
        QTimer::singleShot(50, &panel, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            menuSeen = true;
            // Send to Back is the fifth entry: Rename, separator, Front,
            // Forward, Backward, Back.
            auto *action = menu->actions().at(5);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
                              menu->actionGeometry(action).center());
        });
        QContextMenuEvent event(QContextMenuEvent::Mouse, pos, tree->viewport()->mapToGlobal(pos));
        QApplication::sendEvent(tree->viewport(), &event);
        QVERIFY(menuSeen);
        QCOMPARE(document.front().objectIds(), (QVector<ObjectId>{ ids.last(), ids.first() }));
        QCOMPARE(tree->topLevelItem(1)->text(2), QStringLiteral("Renamed layer"));
        stack.undo();
        QCOMPARE(document.front().objectIds(), ids);
        QCOMPARE(tree->topLevelItem(0)->text(2), QStringLiteral("Renamed layer"));
    }
};

QTEST_MAIN(TestLayerUi)
#include "test_layer_ui.moc"