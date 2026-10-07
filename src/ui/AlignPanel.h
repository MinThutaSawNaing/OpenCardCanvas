#pragma once

#include <QVector>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QToolButton;

namespace occ {

class CardCanvas;
class CardDocument;

// ---------------------------------------------------------------------------
// AlignPanel - alignment, distribution, snapping and the numeric position of
// the selection.
//
// Why a separate panel rather than more menu entries: aligning is the operation
// a card designer repeats most often, and a menu round trip per edge makes it
// painful. The same operations are also in the Arrange menu (built from the
// window's action list), so nothing here is only reachable from one place.
//
// Buttons are disabled - not silently ignored - when the selection cannot
// support them: aligning needs at least two objects, distributing at least
// three, because "distribute one object" has no meaning.
//
// The numeric fields are a second way to type an exact position; they go
// through the same single-undo-command path as the canvas drag and the property
// panel, so the three can never disagree.
// ---------------------------------------------------------------------------
class AlignPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AlignPanel(QWidget *parent = nullptr);

    void setDocument(CardDocument *document);
    void setCanvas(CardCanvas *canvas);
    void refresh();

signals:
    void statusMessage(const QString &message);
    void requestRepaint();

private:
    QToolButton *addAlignButton(QGridLayout *grid, int row, int column,
                                const QString &iconName, const QString &tip,
                                Qt::Alignment flag);
    void applyGeometry(const QString &text);
    void pushSnapOptions();
    double toDisplayLength(double mm) const;
    double fromDisplayLength(double value) const;

    CardDocument *m_document = nullptr;
    CardCanvas   *m_canvas = nullptr;
    bool          m_updating = false;

    QVector<QToolButton *> m_alignButtons;
    QToolButton *m_distributeH = nullptr;
    QToolButton *m_distributeV = nullptr;

    QCheckBox      *m_snapCard = nullptr;
    QCheckBox      *m_snapGuides = nullptr;
    QCheckBox      *m_snapObjects = nullptr;
    QCheckBox      *m_snapGrid = nullptr;
    QDoubleSpinBox *m_gridSpacing = nullptr;
    QCheckBox      *m_showGrid = nullptr;
    QCheckBox      *m_showGuides = nullptr;

    QLabel         *m_selectionLabel = nullptr;
    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_w = nullptr;
    QDoubleSpinBox *m_h = nullptr;
};

} // namespace occ
