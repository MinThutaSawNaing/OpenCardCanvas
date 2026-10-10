#include "canvas/CardCanvas.h"
#include "core/BarcodeObject.h"
#include "core/CardDocument.h"
#include "core/ImageObject.h"
#include "core/PhotoObject.h"
#include "core/QrObject.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"
#include "ui/PropertyPanel.h"

#include <QColorDialog>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QtTest/QtTest>

using namespace occ;

class TestPropertyColors : public QObject
{
    Q_OBJECT
private:
    static QColor color(CardObject *object, int field)
    {
        switch (field) {
        case 0: return static_cast<TextObject *>(object)->color();
        case 1: return static_cast<TextObject *>(object)->outlineColor();
        case 2: return static_cast<ImageObject *>(object)->backgroundFill();
        case 3: return static_cast<PhotoObject *>(object)->borderColor();
        case 4: return static_cast<ShapeObject *>(object)->fillColor();
        case 5: return static_cast<ShapeObject *>(object)->strokeColor();
        case 6: return static_cast<QrObject *>(object)->foregroundColor();
        case 7: return static_cast<QrObject *>(object)->backgroundColor();
        case 8: return static_cast<BarcodeObject *>(object)->foregroundColor();
        default: return static_cast<BarcodeObject *>(object)->backgroundColor();
        }
    }

private slots:
    void pick_data()
    {
        QTest::addColumn<int>("field");
        QTest::addColumn<QString>("title");
        QTest::addColumn<bool>("accept");
        const QStringList titles{ "Text colour", "Outline colour", "Image background colour",
                                  "Border colour", "Fill colour", "Stroke colour",
                                  "QR symbol colour", "QR background colour",
                                  "Bar colour", "Barcode background colour" };
        for (int field = 0; field < titles.size(); ++field) {
            QTest::newRow(qPrintable(titles[field] + " accept")) << field << titles[field] << true;
            QTest::newRow(qPrintable(titles[field] + " cancel")) << field << titles[field] << false;
        }
    }

    void pick()
    {
        QFETCH(int, field);
        QFETCH(QString, title);
        QFETCH(bool, accept);
        CardDocument document;
        QUndoStack stack;
        CardCanvas canvas;
        canvas.setDocument(&document);
        canvas.setUndoStack(&stack);
        CardObjectPtr object;
        if (field < 2) object = std::make_unique<TextObject>();
        else if (field == 2) object = std::make_unique<ImageObject>();
        else if (field == 3) object = std::make_unique<PhotoObject>();
        else if (field < 6) object = std::make_unique<ShapeObject>();
        else if (field < 8) object = std::make_unique<QrObject>();
        else object = std::make_unique<BarcodeObject>();
        const auto id = object->id();
        document.front().insertObject(std::move(object));
        canvas.setSelection({ id });
        PropertyPanel panel;
        panel.setDocument(&document);
        panel.setCanvas(&canvas);
        panel.resize(550, 1000);
        panel.show();
        QTest::qWait(20);
        QToolButton *button = nullptr;
        for (auto *candidate : panel.findChildren<QToolButton *>()) {
            if (candidate->accessibleName() == title)
                button = candidate;
        }
        QVERIFY2(button, qPrintable(title));
        const QColor before = color(document.front().object(id), field);
        const QColor chosen(23, 145, 211, 127);
        bool seen = false;
        bool qtPicker = false;
        bool alphaEnabled = false;
        QColor initial;
        // The timer enters the actual modal nested event loop, not a mocked callback.
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<QColorDialog *>();
            if (!dialog)
                return;
            seen = true;
            qtPicker = dialog->testOption(QColorDialog::DontUseNativeDialog);
            alphaEnabled = dialog->testOption(QColorDialog::ShowAlphaChannel);
            initial = dialog->currentColor();
            panel.refresh(); // A selection/document refresh while the picker is open.
            dialog->setCurrentColor(chosen);
            if (accept) dialog->accept();
            else dialog->reject();
        });
        button->click();
        QVERIFY(seen);
        QVERIFY(qtPicker);
        QVERIFY(alphaEnabled);
        QCOMPARE(initial, before);
        QCOMPARE(color(document.front().object(id), field), accept ? chosen : before);
        QCOMPARE(stack.count(), accept ? 1 : 0);
        if (accept) {
            stack.undo();
            QCOMPARE(color(document.front().object(id), field), before);
            stack.redo();
            QCOMPARE(color(document.front().object(id), field), chosen);
        }
        panel.refresh();
        const QColor expected = accept ? chosen : before;
        QCOMPARE(button->text(), expected.name(expected.alpha() == 255
                                                 ? QColor::HexRgb : QColor::HexArgb).toUpper());
    }
};

QTEST_MAIN(TestPropertyColors)
#include "test_property_colors.moc"