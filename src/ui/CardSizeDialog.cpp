#include "ui/CardSizeDialog.h"

#include "core/CardDocument.h"
#include "core/CardSide.h"
#include "ui/IconFactory.h"
#include "utils/Settings.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace occ {

// ---------------------------------------------------------------------------
// RatioPreview - the new card drawn to scale, with the old size drawn faintly
// behind it.
//
// Seeing the new aspect ratio against the old one is the cheapest possible way
// of catching a mistyped dimension.
// ---------------------------------------------------------------------------
class RatioPreview : public QWidget
{
public:
    explicit RatioPreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumSize(160, 110);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setRatios(const QSizeF &oldSize, const QSizeF &newSize)
    {
        m_old = oldSize;
        m_new = newSize;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.fillRect(rect(), palette().base());

        const double margin = 8.0;
        const QRectF area = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
        if (area.width() <= 1.0 || area.height() <= 1.0)
            return;

        const auto fit = [&area](const QSizeF &size) {
            if (size.width() <= 0.0 || size.height() <= 0.0)
                return QRectF();
            const double scale =
                qMin(area.width() / size.width(), area.height() / size.height());
            QRectF result(QPointF(0, 0), QSizeF(size.width() * scale, size.height() * scale));
            result.moveCenter(area.center());
            return result;
        };

        if (m_old.isValid()) {
            const QRectF previous = fit(m_old);
            painter.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DashLine));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(previous);
        }

        if (m_new.isValid()) {
            const QRectF now = fit(m_new);
            painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
            painter.setBrush(palette().color(QPalette::Highlight).lighter(180));
            painter.drawRect(now);
        }
    }

private:
    QSizeF m_old;
    QSizeF m_new;
};

CardSizeDialog::CardSizeDialog(const CardGeometry &current, const CardDocument *document,
                               QWidget *parent)
    : QDialog(parent),
      m_geometry(current),
      m_document(document)
{
    setWindowTitle(tr("Change card size"));
    setWindowIcon(IconFactory::icon(QStringLiteral("logo")));
    setModal(true);
    buildUi(current);
}

void CardSizeDialog::buildUi(const CardGeometry &current)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 10, 10, 10);
    outer->setSpacing(8);

    auto *columns = new QHBoxLayout();
    columns->setSpacing(10);

    auto *formGroup = new QGroupBox(tr("Dimensions"), this);
    auto *form = new QFormLayout(formGroup);
    form->setSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    const bool inches =
        AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches;
    const QString suffix = inches ? tr(" in") : tr(" mm");

    m_width = new QDoubleSpinBox(formGroup);
    m_width->setRange(1.0, 500.0);
    m_width->setDecimals(inches ? 3 : 2);
    m_width->setSingleStep(0.5);
    m_width->setSuffix(suffix);
    m_width->setKeyboardTracking(false);
    m_width->setValue(toDisplay(current.widthMm()));
    form->addRow(tr("Width"), m_width);

    m_height = new QDoubleSpinBox(formGroup);
    m_height->setRange(1.0, 500.0);
    m_height->setDecimals(inches ? 3 : 2);
    m_height->setSingleStep(0.5);
    m_height->setSuffix(suffix);
    m_height->setKeyboardTracking(false);
    m_height->setValue(toDisplay(current.heightMm()));
    form->addRow(tr("Height"), m_height);

    m_dpi = new QSpinBox(formGroup);
    m_dpi->setRange(kMinDpi, kMaxDpi);
    m_dpi->setSingleStep(50);
    m_dpi->setValue(current.renderDpi());
    form->addRow(tr("Render resolution"), m_dpi);

    m_bleed = new QDoubleSpinBox(formGroup);
    m_bleed->setRange(0.0, 10.0);
    m_bleed->setDecimals(2);
    m_bleed->setSingleStep(0.5);
    m_bleed->setSuffix(suffix);
    m_bleed->setValue(toDisplay(current.bleedMm()));
    form->addRow(tr("Bleed"), m_bleed);

    auto *orientationGroup = new QButtonGroup(this);
    m_landscape = new QRadioButton(tr("Landscape"), formGroup);
    m_portrait = new QRadioButton(tr("Portrait"), formGroup);
    orientationGroup->addButton(m_landscape);
    orientationGroup->addButton(m_portrait);
    (current.isLandscape() ? m_landscape : m_portrait)->setChecked(true);
    auto *orientationRow = new QHBoxLayout();
    orientationRow->addWidget(m_landscape);
    orientationRow->addWidget(m_portrait);
    orientationRow->addStretch(1);
    form->addRow(tr("Orientation"), orientationRow);

    columns->addWidget(formGroup);

    auto *previewGroup = new QGroupBox(tr("Aspect ratio"), this);
    auto *previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new RatioPreview(previewGroup);
    previewLayout->addWidget(m_preview);
    m_sizeLabel = new QLabel(previewGroup);
    m_sizeLabel->setWordWrap(true);
    previewLayout->addWidget(m_sizeLabel);
    columns->addWidget(previewGroup, 1);

    outer->addLayout(columns);

    m_warning = new QLabel(this);
    m_warning->setWordWrap(true);
    m_warning->setObjectName(QStringLiteral("CardSizeWarning"));
    outer->addWidget(m_warning);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    outer->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &CardSizeDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_width, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(m_height, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(m_dpi, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(m_bleed, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(m_landscape, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        QSignalBlocker widthBlocker(m_width);
        QSignalBlocker heightBlocker(m_height);
        const double width = m_width->value();
        const double height = m_height->value();
        if (width < height) {
            m_width->setValue(height);
            m_height->setValue(width);
        }
        updatePreview();
    });
    connect(m_portrait, &QRadioButton::toggled, this, [this](bool on) {
        if (!on)
            return;
        QSignalBlocker widthBlocker(m_width);
        QSignalBlocker heightBlocker(m_height);
        const double width = m_width->value();
        const double height = m_height->value();
        if (width >= height) {
            m_width->setValue(height);
            m_height->setValue(width);
        }
        updatePreview();
    });

    updatePreview();
}

double CardSizeDialog::toDisplay(double mm) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::mmToInch(mm)
               : mm;
}

double CardSizeDialog::fromDisplay(double value) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::inchToMm(value)
               : value;
}

void CardSizeDialog::updatePreview()
{
    const QSizeF oldSize(m_geometry.widthMm(), m_geometry.heightMm());
    const QSizeF newSize(fromDisplay(m_width->value()), fromDisplay(m_height->value()));
    m_preview->setRatios(oldSize, newSize);

    const double ratio = newSize.height() > 0.0 ? newSize.width() / newSize.height() : 0.0;
    m_sizeLabel->setText(tr("New card: %1 x %2 mm\nAspect ratio %3 : 1\nWas %4 x %5 mm")
                             .arg(QString::number(newSize.width(), 'f', 2),
                                  QString::number(newSize.height(), 'f', 2),
                                  QString::number(ratio, 'f', 3),
                                  QString::number(oldSize.width(), 'f', 2),
                                  QString::number(oldSize.height(), 'f', 2)));

    updateWarning();
}

void CardSizeDialog::updateWarning()
{
    if (!m_document) {
        m_warning->setText(QString());
        return;
    }

    const double widthMm = fromDisplay(m_width->value());
    const double heightMm = fromDisplay(m_height->value());
    const QRectF newBounds(0.0, 0.0, widthMm, heightMm);

    QStringList examples;
    int outside = 0;
    const CardSideId sides[] = { CardSideId::Front, CardSideId::Back };
    for (CardSideId sideId : sides) {
        const CardSide &side = m_document->side(sideId);
        const QVector<CardObject *> objects = side.objects();
        for (const CardObject *object : objects) {
            if (newBounds.contains(object->boundingRectMm()))
                continue;
            ++outside;
            if (examples.size() < 3)
                examples << object->name();
        }
    }

    QPalette palette = m_warning->palette();
    if (outside == 0) {
        palette.setColor(QPalette::WindowText, QColor(20, 130, 60));
        m_warning->setText(tr("Every object fits inside the new card."));
    } else {
        palette.setColor(QPalette::WindowText, QColor(180, 110, 0));
        QString text = (outside == 1)
                           ? tr("1 object lies outside the new card area (%1). It will be "
                                "moved inside the new bounds when the size is applied.")
                                 .arg(examples.join(QStringLiteral(", ")))
                           : tr("%1 objects lie outside the new card area (%2). They will be "
                                "moved inside the new bounds when the size is applied.")
                                 .arg(outside)
                                 .arg(examples.join(QStringLiteral(", "))
                                      + (outside > examples.size() ? tr(", ...") : QString()));
        m_warning->setText(text);
    }
    m_warning->setPalette(palette);
}

void CardSizeDialog::accept()
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
        QMessageBox::warning(this, tr("Change card size"), error);
        return;
    }

    m_geometry = geometry;
    QDialog::accept();
}

} // namespace occ
