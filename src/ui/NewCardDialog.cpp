#include "ui/NewCardDialog.h"

#include "project/TemplateStore.h"
#include "ui/IconFactory.h"
#include "utils/Settings.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace occ {

NewCardDialog::NewCardDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New card"));
    setWindowIcon(IconFactory::icon(QStringLiteral("logo")));
    setModal(true);
    buildUi();
}

void NewCardDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    auto *columns = new QHBoxLayout();
    columns->setSpacing(10);

    // --- presets ------------------------------------------------------------
    auto *presetGroup = new QGroupBox(tr("Card size"), this);
    auto *presetLayout = new QVBoxLayout(presetGroup);
    presetLayout->setContentsMargins(6, 6, 6, 6);
    m_presets = new QListWidget(presetGroup);
    m_presets->setMinimumWidth(190);
    m_presets->setMinimumHeight(180);
    presetLayout->addWidget(m_presets);
    columns->addWidget(presetGroup, 1);

    // --- details ------------------------------------------------------------
    auto *detailGroup = new QGroupBox(tr("Details"), this);
    auto *detailLayout = new QVBoxLayout(detailGroup);
    detailLayout->setContentsMargins(6, 6, 6, 6);

    auto *form = new QFormLayout();
    form->setSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_width = new QDoubleSpinBox(detailGroup);
    m_width->setDecimals(2);
    m_width->setRange(1.0, 500.0);
    m_width->setSingleStep(0.5);
    m_width->setKeyboardTracking(false);
    form->addRow(tr("Width"), m_width);

    m_height = new QDoubleSpinBox(detailGroup);
    m_height->setDecimals(2);
    m_height->setRange(1.0, 500.0);
    m_height->setSingleStep(0.5);
    m_height->setKeyboardTracking(false);
    form->addRow(tr("Height"), m_height);

    m_dpi = new QSpinBox(detailGroup);
    m_dpi->setRange(kMinDpi, kMaxDpi);
    m_dpi->setSingleStep(50);
    m_dpi->setToolTip(tr("Resolution used when the card is rendered, previewed and printed."));
    form->addRow(tr("Render resolution"), m_dpi);

    m_bleed = new QDoubleSpinBox(detailGroup);
    m_bleed->setRange(0.0, 10.0);
    m_bleed->setDecimals(2);
    m_bleed->setSingleStep(0.5);
    m_bleed->setToolTip(tr("Extends the printable area beyond the trim edge. Leave at 0 for a "
                           "standard card."));
    form->addRow(tr("Bleed"), m_bleed);

    detailLayout->addLayout(form);

    auto *orientationGroup = new QButtonGroup(this);
    m_landscape = new QRadioButton(tr("Landscape"), detailGroup);
    m_portrait = new QRadioButton(tr("Portrait"), detailGroup);
    orientationGroup->addButton(m_landscape);
    orientationGroup->addButton(m_portrait);
    detailLayout->addWidget(new QLabel(tr("Orientation"), detailGroup));
    auto *orientationRow = new QHBoxLayout();
    orientationRow->addWidget(m_landscape);
    orientationRow->addWidget(m_portrait);
    orientationRow->addStretch(1);
    detailLayout->addLayout(orientationRow);

    detailLayout->addSpacing(6);
    m_blank = new QRadioButton(tr("Start with a blank card"), detailGroup);
    m_fromTemplate = new QRadioButton(tr("Start from a template"), detailGroup);
    detailLayout->addWidget(m_blank);
    detailLayout->addWidget(m_fromTemplate);

    m_templates = new QListWidget(detailGroup);
    m_templates->setMinimumHeight(90);
    detailLayout->addWidget(m_templates);
    m_templateHint = new QLabel(detailGroup);
    m_templateHint->setWordWrap(true);
    detailLayout->addWidget(m_templateHint);

    columns->addWidget(detailGroup, 1);
    outer->addLayout(columns);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    outer->addWidget(m_summary);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewCardDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // --- populate -----------------------------------------------------------
    const bool inches =
        AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches;
    const QString suffix = inches ? tr(" in") : tr(" mm");
    for (QDoubleSpinBox *box : { m_width, m_height }) {
        box->setSuffix(suffix);
        box->setDecimals(inches ? 3 : 2);
    }
    m_bleed->setSuffix(suffix);

    const CardGeometry defaults = CardGeometry::defaultGeometry();
    m_dpi->setValue(AppSettings::instance().defaultRenderDpi());
    m_bleed->setValue(toDisplay(AppSettings::instance().defaultBleedMm()));
    m_width->setValue(toDisplay(defaults.widthMm()));
    m_height->setValue(toDisplay(defaults.heightMm()));

    loadPresets();
    loadTemplates();

    m_blank->setChecked(true);
    m_templates->setEnabled(false);
    m_fromTemplate->setEnabled(m_templates->count() > 0);
    if (m_templates->count() == 0)
        m_templateHint->setText(tr("There are no templates yet. Save one with "
                                   "File > Save as Template... and it will appear here."));

    const auto sizeEdited = [this] {
        updateOrientationFromSize();
        m_presets->clearSelection();
        updateSummary();
    };
    connect(m_width, &QDoubleSpinBox::valueChanged, this, sizeEdited);
    connect(m_height, &QDoubleSpinBox::valueChanged, this, sizeEdited);
    connect(m_presets, &QListWidget::currentRowChanged, this, &NewCardDialog::applyPreset);

    connect(m_landscape, &QRadioButton::toggled, this, [this](bool on) {
        if (on)
            setOrientation(true);
    });
    connect(m_portrait, &QRadioButton::toggled, this, [this](bool on) {
        if (on)
            setOrientation(false);
    });

    connect(m_dpi, &QSpinBox::valueChanged, this, [this] { updateSummary(); });
    connect(m_bleed, &QDoubleSpinBox::valueChanged, this, [this] { updateSummary(); });
    connect(m_fromTemplate, &QRadioButton::toggled, this, [this](bool on) {
        m_templates->setEnabled(on);
        updateSummary();
    });
    connect(m_templates, &QListWidget::currentRowChanged, this, [this](int) { updateSummary(); });

    // Start on the preset the user configured as their default.
    const QString defaultPreset = AppSettings::instance().defaultCardPreset();
    for (int row = 0; row < m_presets->count(); ++row) {
        if (m_presets->item(row)->data(Qt::UserRole).toString() == defaultPreset) {
            m_presets->setCurrentRow(row);
            break;
        }
    }
    updateSummary();
}

double NewCardDialog::toDisplay(double mm) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::mmToInch(mm)
               : mm;
}

double NewCardDialog::fromDisplay(double value) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::inchToMm(value)
               : value;
}

void NewCardDialog::loadPresets()
{
    m_presets->clear();
    const QVector<CardPreset> presets = CardGeometry::presets();
    for (const CardPreset &preset : presets) {
        const QString label = tr("%1 (%2 x %3 mm)")
                                  .arg(preset.name,
                                       QString::number(preset.widthMm, 'f', 2),
                                       QString::number(preset.heightMm, 'f', 2));
        auto *item = new QListWidgetItem(label, m_presets);
        item->setData(Qt::UserRole, preset.id);
        item->setToolTip(label);
    }
}

void NewCardDialog::loadTemplates()
{
    m_templates->clear();
    QString error;
    const QVector<TemplateStore::TemplateInfo> templates = TemplateStore::list(&error);
    for (const TemplateStore::TemplateInfo &info : templates) {
        auto *item = new QListWidgetItem(info.name, m_templates);
        item->setData(Qt::UserRole, info.filePath);
        item->setToolTip(info.description.isEmpty() ? info.name : info.description);
    }
    if (!error.isEmpty())
        m_templateHint->setText(tr("Some templates could not be read: %1").arg(error));
}

void NewCardDialog::applyPreset(int row)
{
    if (row < 0 || row >= m_presets->count())
        return;
    const QString id = m_presets->item(row)->data(Qt::UserRole).toString();
    if (id.isEmpty())
        return;

    const CardPreset preset = CardGeometry::preset(id);
    if (preset.id.isEmpty())
        return;

    // The user's orientation is kept when the preset can be turned that way.
    const bool wasLandscape = m_width->value() >= m_height->value();
    const bool presetLandscape = preset.widthMm >= preset.heightMm;
    const bool swap = wasLandscape != presetLandscape;

    QSignalBlocker widthBlocker(m_width);
    QSignalBlocker heightBlocker(m_height);
    m_width->setValue(toDisplay(swap ? preset.heightMm : preset.widthMm));
    m_height->setValue(toDisplay(swap ? preset.widthMm : preset.heightMm));
    updateOrientationFromSize();
    updateSummary();
}

void NewCardDialog::updateOrientationFromSize()
{
    // The radios are derived from the numbers, so the two can never disagree.
    const bool landscape = m_width->value() >= m_height->value();
    QSignalBlocker landBlocker(m_landscape);
    QSignalBlocker portBlocker(m_portrait);
    m_landscape->setChecked(landscape);
    m_portrait->setChecked(!landscape);
}

void NewCardDialog::setOrientation(bool landscape)
{
    const double width = m_width->value();
    const double height = m_height->value();
    if (landscape != (width >= height)) {
        QSignalBlocker widthBlocker(m_width);
        QSignalBlocker heightBlocker(m_height);
        m_width->setValue(height);
        m_height->setValue(width);
    }
    m_presets->clearSelection();
    updateSummary();
}

void NewCardDialog::updateSummary()
{
    CardGeometry geometry;
    geometry.setSize(fromDisplay(m_width->value()), fromDisplay(m_height->value()));
    geometry.setRenderDpi(m_dpi->value());
    geometry.setBleedMm(fromDisplay(m_bleed->value()));

    const QSize pixels = geometry.pixelSize();
    QString text = tr("%1 x %2 mm at %3 dpi (%4 x %5 pixels)")
                       .arg(QString::number(geometry.widthMm(), 'f', 2),
                            QString::number(geometry.heightMm(), 'f', 2))
                       .arg(geometry.renderDpi())
                       .arg(pixels.width())
                       .arg(pixels.height());
    if (geometry.bleedMm() > 0.0) {
        text += QLatin1Char('\n')
                + tr("A bleed of %1 mm is added on every side.")
                      .arg(QString::number(geometry.bleedMm(), 'f', 2));
    }
    if (m_fromTemplate->isChecked() && m_templates->currentItem()) {
        text += QLatin1Char('\n')
                + tr("Starts from the template \"%1\".")
                      .arg(m_templates->currentItem()->text());
    }

    m_summary->setText(text);
}

CardGeometry NewCardDialog::cardGeometry() const
{
    return m_geometry;
}

QString NewCardDialog::templatePath() const
{
    return m_templatePath;
}

void NewCardDialog::accept()
{
    CardGeometry geometry;
    geometry.setSize(fromDisplay(m_width->value()), fromDisplay(m_height->value()));
    geometry.setRenderDpi(m_dpi->value());
    geometry.setBleedMm(fromDisplay(m_bleed->value()));
    geometry.setOrientation(m_width->value() >= m_height->value());
    geometry.setPresetId(CardGeometry::matchingPresetId(geometry.widthMm(),
                                                        geometry.heightMm()));

    QString error;
    if (!geometry.isValid(&error)) {
        QMessageBox::warning(this, tr("New card"), error);
        return;
    }

    QString chosenTemplate;
    if (m_fromTemplate->isChecked()) {
        if (QListWidgetItem *item = m_templates->currentItem())
            chosenTemplate = item->data(Qt::UserRole).toString();
        if (chosenTemplate.isEmpty()) {
            QMessageBox::information(this, tr("New card"),
                                     tr("Choose a template, or select \"Start with a blank "
                                        "card\"."));
            return;
        }
    }

    m_geometry = geometry;
    m_templatePath = chosenTemplate;
    QDialog::accept();
}

} // namespace occ
