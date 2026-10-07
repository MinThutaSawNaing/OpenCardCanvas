#pragma once

#include "core/CardTypes.h"

#include <QHash>
#include <QString>
#include <QWidget>

class QAction;
class QAction;
class QToolButton;

// ---------------------------------------------------------------------------
// ToolsPanel - the tool palette.
//
// Eight buttons, one behaviour each:
//
//   Select  V   the default tool; the canvas handles everything
//   Text    T   the canvas inserts a text object
//   Image   I   nothing is inserted until a file is chosen - the panel asks the
//   Photo   P   window for a file, so a "Photo" button never inserts a blank
//   Shape   R   a dropdown picks which primitive (rectangle .. arrow)
//   Line    L   a line is a shape whose angle comes from its rotation
//   QR      Q   inserted with the default {{employee_id}} payload
//   Barcode B   inserted with the default payload
//
// The panel is deliberately dumb: it knows about tools and emits requests. The
// window owns the file dialogs and the canvas owns the undo commands, which is
// why inserting an object is a single, testable path.
//
// Buttons are exclusive because exactly one tool is active at a time; the
// canvas is told which one through toolSelected() so it can change the cursor
// and decide what a click does.
// ---------------------------------------------------------------------------
namespace occ {

class ToolsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ToolsPanel(QWidget *parent = nullptr);

    // Identifier of the active tool ("select", "text", "image", "photo",
    // "shape", "line", "qr", "barcode"). Never empty.
    QString currentTool() const { return m_currentTool; }
    // Changes the checked button without emitting toolSelected() - used when
    // another part of the window (a shortcut, a menu entry) already switched
    // the tool.
    void setCurrentTool(const QString &toolId);

    // The shape the Shape tool will insert. Kept here so the dropdown and the
    // canvas agree even before a shape has ever been placed.
    ShapeKind currentShapeKind() const { return m_shapeKind; }
    void setCurrentShapeKind(ShapeKind kind);

signals:
    void toolSelected(const QString &toolId);
    void shapeRequested(occ::ShapeKind kind);
    void imageRequested();
    void photoRequested();

private:
    QToolButton *addTool(const QString &toolId, const QString &iconName,
                         const QString &text, const QString &shortcut);
    void buildShapeMenu();
    void activate(const QString &toolId);
    void updateCheckedState();

    QHash<QString, QToolButton *> m_buttons;
    QHash<int, QAction *>         m_shapeActions;
    QToolButton *m_shapeButton = nullptr;
    QString      m_currentTool = QStringLiteral("select");
    ShapeKind    m_shapeKind = ShapeKind::Rectangle;
};

} // namespace occ
