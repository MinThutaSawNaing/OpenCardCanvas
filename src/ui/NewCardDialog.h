#pragma once

#include "core/CardGeometry.h"
#include "core/Units.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

// ---------------------------------------------------------------------------
// NewCardDialog - "New card" and "New from template".
//
// The dialog answers three questions in one place: what size is the card, at
// what resolution is it rendered, and does it start empty or from a template.
// Presets come from CardGeometry::presets() so the list and the geometry code
// cannot drift apart, and a custom size is validated by CardGeometry itself
// before the dialog is allowed to close.
//
// Width and height are shown in the unit system the user selected in
// Preferences; the geometry handed back is always in millimetres.
// ---------------------------------------------------------------------------
namespace occ {

class NewCardDialog : public QDialog
{
    Q_OBJECT
public:
    explicit NewCardDialog(QWidget *parent = nullptr);

    // Valid after exec() returned QDialog::Accepted.
    CardGeometry cardGeometry() const;
    // Empty when the user chose a blank card.
    QString templatePath() const;

private:
    void buildUi();
    void loadPresets();
    void loadTemplates();
    void applyPreset(int row);
    void updateOrientationFromSize();
    void setOrientation(bool landscape);
    void updateSummary();
    void accept() override;

    double toDisplay(double mm) const;
    double fromDisplay(double value) const;

    CardGeometry m_geometry = CardGeometry::defaultGeometry();
    QString      m_templatePath;

    QListWidget    *m_presets = nullptr;
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
    QRadioButton   *m_landscape = nullptr;
    QRadioButton   *m_portrait = nullptr;
    QSpinBox       *m_dpi = nullptr;
    QDoubleSpinBox *m_bleed = nullptr;
    QRadioButton   *m_blank = nullptr;
    QRadioButton   *m_fromTemplate = nullptr;
    QListWidget    *m_templates = nullptr;
    QLabel         *m_templateHint = nullptr;
    QLabel         *m_summary = nullptr;
};

} // namespace occ
