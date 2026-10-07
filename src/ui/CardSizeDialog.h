#pragma once

#include "core/CardGeometry.h"
#include "core/Units.h"

#include <QDialog>

class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QSpinBox;

namespace occ {

class CardDocument;
class RatioPreview;

// ---------------------------------------------------------------------------
// CardSizeDialog - change the size of an existing card.
//
// The two things that make this dialog worth having rather than reusing "New
// card" are:
//
//   * a live preview of the aspect ratio, so a wrong number is obvious before
//     the card is resized rather than after
//   * an honest warning naming the objects that currently sit outside the new
//     area, because resizing a card does not damage the design but it does move
//     things, and a silent move is how a card ends up printed wrong
//
// The dialog never touches the document: it hands back a CardGeometry and the
// window applies it through a GeometryCommand, so Ctrl+Z undoes the resize.
// ---------------------------------------------------------------------------
class CardSizeDialog : public QDialog
{
    Q_OBJECT
public:
    CardSizeDialog(const CardGeometry &current, const CardDocument *document,
                   QWidget *parent = nullptr);

    // Valid after exec() returned QDialog::Accepted.
    CardGeometry geometry() const { return m_geometry; }

private:
    void buildUi(const CardGeometry &current);
    void updatePreview();
    void updateWarning();
    void accept() override;
    double toDisplay(double mm) const;
    double fromDisplay(double value) const;

    CardGeometry     m_geometry;
    const CardDocument *m_document = nullptr;

    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QSpinBox       *m_dpi = nullptr;
    QDoubleSpinBox *m_bleed = nullptr;
    QRadioButton   *m_landscape = nullptr;
    QRadioButton   *m_portrait = nullptr;
    RatioPreview   *m_preview = nullptr;
    QLabel         *m_sizeLabel = nullptr;
    QLabel         *m_warning = nullptr;
};

} // namespace occ
