#pragma once

#include "core/CardTypes.h"

#include <QVector>
#include <QWidget>

#include <functional>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QToolButton;

namespace occ {

class CardCanvas;
class CardDocument;
class CardObject;
class ColorButton;

// ---------------------------------------------------------------------------
// PropertyPanel - the editor for whatever is selected.
//
// Structure: a COMMON section that is always visible (X, Y, W, H, rotation,
// opacity, visible, locked) plus a stack of type specific editors that switches
// with the selection.
//
// Two rules make this panel safe:
//
//   1. nothing is written to the document directly. Every edit captures the
//      selected objects' state, applies the change, captures the result and
//      pushes ONE ModifyObjectsCommand. A spin box dragged across twenty values
//      therefore produces one history entry, not twenty.
//   2. a refresh caused by the document does not write back. The m_updating
//      guard breaks the round trip, which is what stops a feedback loop
//      between "the document changed" and "the widget changed".
//
// Multi selection shows the common fields and applies a change to every
// selected object in a single undo step; a type page appears only when the
// selection can actually be described by it.
// ---------------------------------------------------------------------------
class PropertyPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PropertyPanel(QWidget *parent = nullptr);

    void setDocument(CardDocument *document);
    void setCanvas(CardCanvas *canvas);
    void refresh();

signals:
    void statusMessage(const QString &message);
    void requestRepaint();

private:
    // --- construction -------------------------------------------------------
    QWidget *buildCommonSection();
    QWidget *buildTextPage();
    QWidget *buildImagePage();
    QWidget *buildPhotoPage();
    QWidget *buildShapePage();
    QWidget *buildQrPage();
    QWidget *buildBarcodePage();
    QWidget *buildMessagePage();

    // --- editing ------------------------------------------------------------
    void applyEdit(const std::function<void(CardObject &)> &mutate,
                   const QString &text, const QString &mergeKey = QString());
    void chooseReplacementImage();
    QVector<CardObject *> selectedObjects() const;
    CardSideId currentSide() const;
    bool editable() const;

    // --- helpers ------------------------------------------------------------
    double toDisplayLength(double mm) const;
    double fromDisplayLength(double value) const;
    void   updateUnitSuffixes();
    void   updateImageInfo(CardObject *object);
    void   showPageFor(const QVector<CardObject *> &objects);
    void   reportValidity(QLabel *label, const QString &error, const QString &warning);

    // --- state --------------------------------------------------------------
    CardDocument *m_document = nullptr;
    CardCanvas   *m_canvas = nullptr;
    bool          m_updating = false;

    QStackedWidget *m_stack = nullptr;
    QLabel *m_selectionLabel = nullptr;

    // Common
    QDoubleSpinBox *m_x = nullptr;
    QDoubleSpinBox *m_y = nullptr;
    QDoubleSpinBox *m_w = nullptr;
    QDoubleSpinBox *m_h = nullptr;
    QDoubleSpinBox *m_rotation = nullptr;
    QDoubleSpinBox *m_opacity = nullptr;
    QCheckBox      *m_visible = nullptr;
    QCheckBox      *m_locked = nullptr;

    // Text
    QFontComboBox  *m_fontFamily = nullptr;
    QDoubleSpinBox *m_fontSize = nullptr;
    QToolButton    *m_bold = nullptr;
    QToolButton    *m_italic = nullptr;
    QToolButton    *m_underline = nullptr;
    ColorButton    *m_textColor = nullptr;
    QButtonGroup   *m_alignH = nullptr;
    QButtonGroup   *m_alignV = nullptr;
    QDoubleSpinBox *m_lineSpacing = nullptr;
    QDoubleSpinBox *m_letterSpacing = nullptr;
    QCheckBox      *m_wordWrap = nullptr;
    QCheckBox      *m_autoShrink = nullptr;
    ColorButton    *m_outlineColor = nullptr;
    QDoubleSpinBox *m_outlineWidth = nullptr;
    QPlainTextEdit *m_text = nullptr;

    // Image
    QLabel         *m_imageSource = nullptr;
    QLabel         *m_imagePixels = nullptr;
    QComboBox      *m_fitMode = nullptr;
    ColorButton    *m_imageBackground = nullptr;
    QPushButton    *m_replaceImage = nullptr;
    QPushButton    *m_resetCrop = nullptr;
    QDoubleSpinBox *m_cropLeft = nullptr;
    QDoubleSpinBox *m_cropTop = nullptr;
    QDoubleSpinBox *m_cropRight = nullptr;
    QDoubleSpinBox *m_cropBottom = nullptr;

    // Photo
    QComboBox      *m_cropShape = nullptr;
    QDoubleSpinBox *m_cornerRadius = nullptr;
    QDoubleSpinBox *m_borderWidth = nullptr;
    ColorButton    *m_borderColor = nullptr;
    QCheckBox      *m_lockAspect = nullptr;
    QLabel         *m_aspectLabel = nullptr;
    QPushButton    *m_replacePhoto = nullptr;
    QPushButton    *m_makeCircular = nullptr;

    // Shape
    QComboBox      *m_shapeKind = nullptr;
    ColorButton    *m_fillColor = nullptr;
    ColorButton    *m_strokeColor = nullptr;
    QDoubleSpinBox *m_strokeWidth = nullptr;
    QDoubleSpinBox *m_shapeCornerRadius = nullptr;
    QDoubleSpinBox *m_sides = nullptr;
    QDoubleSpinBox *m_starRatio = nullptr;

    // QR
    QComboBox      *m_qrKind = nullptr;
    QPlainTextEdit *m_qrData = nullptr;
    QComboBox      *m_qrEcc = nullptr;
    QDoubleSpinBox *m_qrQuietZone = nullptr;
    ColorButton    *m_qrForeground = nullptr;
    ColorButton    *m_qrBackground = nullptr;
    QLabel         *m_qrValidity = nullptr;

    // Barcode
    QComboBox      *m_bcSymbology = nullptr;
    QLineEdit      *m_bcData = nullptr;
    QCheckBox      *m_bcHumanText = nullptr;
    QDoubleSpinBox *m_bcHumanSize = nullptr;
    QDoubleSpinBox *m_bcQuietZone = nullptr;
    ColorButton    *m_bcForeground = nullptr;
    ColorButton    *m_bcBackground = nullptr;
    QLabel         *m_bcValidity = nullptr;
};

} // namespace occ
