#include "ui/AlignPanel.h"

#include "canvas/CardCanvas.h"
#include "canvas/SnapEngine.h"
#include "commands/UndoCommands.h"
#include "core/CardDocument.h"
#include "core/CardSide.h"
#include "ui/IconFactory.h"
#include "utils/Settings.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace occ {

namespace {

QToolButton *makeIconButton(QWidget *parent, const QString &iconName, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setIcon(IconFactory::icon(iconName));
    button->setIconSize(QSize(20, 20));
    button->setAutoRaise(true);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    button->setMinimumSize(28, 28);
    return button;
}

} // namespace

AlignPanel::AlignPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("AlignPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // --- align and distribute ----------------------------------------------
    auto *alignGroup = new QGroupBox(tr("Align and distribute"), this);
    auto *alignLayout = new QVBoxLayout(alignGroup);
    alignLayout->setContentsMargins(6, 6, 6, 6);
    alignLayout->setSpacing(4);

    auto *grid = new QGridLayout();
    grid->setSpacing(2);
    m_alignButtons << addAlignButton(grid, 0, 0, QStringLiteral("align_left"),
                                     tr("Align left edges"), Qt::AlignLeft)
                   << addAlignButton(grid, 0, 1, QStringLiteral("align_center_h"),
                                     tr("Align horizontal centres"), Qt::AlignHCenter)
                   << addAlignButton(grid, 0, 2, QStringLiteral("align_right"),
                                     tr("Align right edges"), Qt::AlignRight)
                   << addAlignButton(grid, 1, 0, QStringLiteral("align_top"),
                                     tr("Align top edges"), Qt::AlignTop)
                   << addAlignButton(grid, 1, 1, QStringLiteral("align_middle_v"),
                                     tr("Align vertical centres"), Qt::AlignVCenter)
                   << addAlignButton(grid, 1, 2, QStringLiteral("align_bottom"),
                                     tr("Align bottom edges"), Qt::AlignBottom);
    alignLayout->addLayout(grid);

    auto *distributeRow = new QHBoxLayout();
    distributeRow->setSpacing(2);
    m_distributeH = makeIconButton(alignGroup, QStringLiteral("distribute_h"),
                                   tr("Distribute horizontally (equal gaps)"));
    m_distributeV = makeIconButton(alignGroup, QStringLiteral("distribute_v"),
                                   tr("Distribute vertically (equal gaps)"));
    distributeRow->addWidget(m_distributeH);
    distributeRow->addWidget(m_distributeV);
    distributeRow->addStretch(1);
    alignLayout->addLayout(distributeRow);
    layout->addWidget(alignGroup);

    // --- snapping -----------------------------------------------------------
    auto *snapGroup = new QGroupBox(tr("Snapping and aids"), this);
    auto *snapLayout = new QVBoxLayout(snapGroup);
    snapLayout->setContentsMargins(6, 6, 6, 6);
    snapLayout->setSpacing(4);

    m_snapCard = new QCheckBox(tr("Snap to the card edges and centre"), snapGroup);
    m_snapGuides = new QCheckBox(tr("Snap to guides"), snapGroup);
    m_snapObjects = new QCheckBox(tr("Snap to other objects"), snapGroup);
    m_snapGrid = new QCheckBox(tr("Snap to the grid"), snapGroup);
    snapLayout->addWidget(m_snapCard);
    snapLayout->addWidget(m_snapGuides);
    snapLayout->addWidget(m_snapObjects);
    snapLayout->addWidget(m_snapGrid);

    auto *gridForm = new QFormLayout();
    gridForm->setContentsMargins(0, 0, 0, 0);
    gridForm->setSpacing(4);
    m_gridSpacing = new QDoubleSpinBox(snapGroup);
    m_gridSpacing->setRange(0.5, 50.0);
    m_gridSpacing->setDecimals(2);
    m_gridSpacing->setSingleStep(0.5);
    m_gridSpacing->setKeyboardTracking(false);
    m_gridSpacing->setToolTip(tr("Distance between grid lines."));
    gridForm->addRow(tr("Grid spacing"), m_gridSpacing);
    snapLayout->addLayout(gridForm);

    m_showGrid = new QCheckBox(tr("Show the grid"), snapGroup);
    m_showGuides = new QCheckBox(tr("Show guides"), snapGroup);
    snapLayout->addWidget(m_showGrid);
    snapLayout->addWidget(m_showGuides);
    layout->addWidget(snapGroup);

    // --- exact position of the selection ------------------------------------
    auto *positionGroup = new QGroupBox(tr("Selection"), this);
    auto *positionLayout = new QVBoxLayout(positionGroup);
    positionLayout->setContentsMargins(6, 6, 6, 6);
    positionLayout->setSpacing(4);

    m_selectionLabel = new QLabel(tr("No selection"), positionGroup);
    m_selectionLabel->setWordWrap(true);
    positionLayout->addWidget(m_selectionLabel);

    auto *positionGrid = new QGridLayout();
    positionGrid->setSpacing(4);
    m_x = new QDoubleSpinBox(positionGroup);
    m_y = new QDoubleSpinBox(positionGroup);
    m_w = new QDoubleSpinBox(positionGroup);
    m_h = new QDoubleSpinBox(positionGroup);
    for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h }) {
        box->setDecimals(2);
        box->setSingleStep(0.5);
        box->setKeyboardTracking(false);
        box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        box->setMinimumWidth(76);
    }
    m_x->setRange(-kMaxCardMm, kMaxCardMm);
    m_y->setRange(-kMaxCardMm, kMaxCardMm);
    m_w->setRange(0.1, kMaxCardMm);
    m_h->setRange(0.1, kMaxCardMm);
    positionGrid->addWidget(new QLabel(tr("X"), positionGroup), 0, 0);
    positionGrid->addWidget(m_x, 0, 1);
    positionGrid->addWidget(new QLabel(tr("Y"), positionGroup), 0, 2);
    positionGrid->addWidget(m_y, 0, 3);
    positionGrid->addWidget(new QLabel(tr("W"), positionGroup), 1, 0);
    positionGrid->addWidget(m_w, 1, 1);
    positionGrid->addWidget(new QLabel(tr("H"), positionGroup), 1, 2);
    positionGrid->addWidget(m_h, 1, 3);
    positionLayout->addLayout(positionGrid);

    auto *positionHint = new QLabel(tr("Applies to every selected object as one undo step."),
                                    positionGroup);
    positionHint->setWordWrap(true);
    positionLayout->addWidget(positionHint);
    layout->addWidget(positionGroup);

    layout->addStretch(1);

    // --- wiring -------------------------------------------------------------
    for (QToolButton *button : m_alignButtons) {
        const Qt::Alignment flag = button->property("occAlignFlag").value<Qt::Alignment>();
        connect(button, &QToolButton::clicked, this, [this, flag] {
            if (!m_canvas)
                return;
            m_canvas->alignSelection(flag);
            emit statusMessage(tr("Selection aligned."));
            emit requestRepaint();
        });
    }
    connect(m_distributeH, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        m_canvas->distributeSelectionHorizontally();
        emit statusMessage(tr("Selection distributed horizontally."));
        emit requestRepaint();
    });
    connect(m_distributeV, &QToolButton::clicked, this, [this] {
        if (!m_canvas)
            return;
        m_canvas->distributeSelectionVertically();
        emit statusMessage(tr("Selection distributed vertically."));
        emit requestRepaint();
    });

    const auto snapChanged = [this] { pushSnapOptions(); };
    connect(m_snapCard, &QCheckBox::toggled, this, snapChanged);
    connect(m_snapGuides, &QCheckBox::toggled, this, snapChanged);
    connect(m_snapObjects, &QCheckBox::toggled, this, snapChanged);
    connect(m_snapGrid, &QCheckBox::toggled, this, snapChanged);
    connect(m_gridSpacing, &QDoubleSpinBox::valueChanged, this, snapChanged);

    connect(m_showGrid, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_canvas)
            return;
        m_canvas->setShowGrid(on);
        AppSettings::instance().setShowGrid(on);
        emit statusMessage(on ? tr("Grid shown.") : tr("Grid hidden."));
    });
    connect(m_showGuides, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_canvas)
            return;
        m_canvas->setShowGuides(on);
        AppSettings::instance().setShowGuides(on);
        emit statusMessage(on ? tr("Guides shown.") : tr("Guides hidden."));
    });

    for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h })
        connect(box, &QDoubleSpinBox::valueChanged, this,
                [this] { applyGeometry(tr("Set position and size")); });
}

QToolButton *AlignPanel::addAlignButton(QGridLayout *grid, int row, int column,
                                        const QString &iconName, const QString &tip,
                                        Qt::Alignment flag)
{
    QToolButton *button = makeIconButton(grid->parentWidget(), iconName, tip);
    button->setProperty("occAlignFlag", QVariant::fromValue(flag));
    grid->addWidget(button, row, column);
    return button;
}

void AlignPanel::setDocument(CardDocument *document)
{
    if (m_document == document)
        return;
    if (m_document)
        m_document->disconnect(this);
    m_document = document;
    if (m_document)
        connect(m_document, &CardDocument::contentsChanged, this, &AlignPanel::refresh);
    refresh();
}

void AlignPanel::setCanvas(CardCanvas *canvas)
{
    if (m_canvas == canvas)
        return;
    if (m_canvas)
        m_canvas->disconnect(this);
    m_canvas = canvas;
    if (m_canvas) {
        connect(m_canvas, &CardCanvas::selectionChanged, this, &AlignPanel::refresh);
        connect(m_canvas, &CardCanvas::documentModified, this, &AlignPanel::refresh);
        connect(m_canvas, &CardCanvas::currentSideChanged, this,
                [this](CardSideId) { refresh(); });
    }
    refresh();
}

void AlignPanel::pushSnapOptions()
{
    if (m_updating || !m_canvas)
        return;

    SnapEngine::Options options = m_canvas->snapOptions();
    options.toCard = m_snapCard->isChecked();
    options.toGuides = m_snapGuides->isChecked();
    options.toObjects = m_snapObjects->isChecked();
    options.toGrid = m_snapGrid->isChecked();
    options.gridSpacingMm = m_gridSpacing->value();
    m_canvas->setSnapOptions(options);

    // Persisted as well, so the next session starts with the user's habits.
    AppSettings &settings = AppSettings::instance();
    settings.setSnapToCard(options.toCard);
    settings.setSnapToGuides(options.toGuides);
    settings.setSnapToObjects(options.toObjects);
    settings.setSnapToGrid(options.toGrid);
    settings.setGridSpacingMm(options.gridSpacingMm);
}

void AlignPanel::applyGeometry(const QString &text)
{
    if (m_updating || !m_canvas || !m_document || !m_canvas->undoStack())
        return;

    const QVector<CardObject *> objects = m_canvas->selection();
    if (objects.isEmpty())
        return;

    const double x = fromDisplayLength(m_x->value());
    const double y = fromDisplayLength(m_y->value());
    const double w = fromDisplayLength(m_w->value());
    const double h = fromDisplayLength(m_h->value());

    const CardSideId side = m_canvas->currentSide();
    CardSide &cardSide = m_document->side(side);

    QVector<ObjectId> ids;
    ids.reserve(objects.size());
    for (const CardObject *object : objects)
        ids.append(object->id());

    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(cardSide, ids);
    for (CardObject *object : objects)
        object->setRectMm(QRectF(x, y, w, h));
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(cardSide, ids);
    if (before == after)
        return;

    m_canvas->undoStack()->push(
        new ModifyObjectsCommand(m_document, side, before, after, text,
                                 QStringLiteral("align.geometry")));
    emit statusMessage(text);
    emit requestRepaint();
}

double AlignPanel::toDisplayLength(double mm) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::mmToInch(mm)
               : mm;
}

double AlignPanel::fromDisplayLength(double value) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::inchToMm(value)
               : value;
}

void AlignPanel::refresh()
{
    if (!m_selectionLabel)
        return;

    const bool inches =
        AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches;
    const QString suffix = inches ? tr(" in") : tr(" mm");
    const int decimals = inches ? 3 : 2;

    m_updating = true;

    for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h }) {
        box->setSuffix(suffix);
        box->setDecimals(decimals);
        box->setSingleStep(inches ? 0.02 : 0.5);
    }

    const int count = m_canvas ? m_canvas->selectionCount() : 0;
    const QVector<CardObject *> objects =
        m_canvas ? m_canvas->selection() : QVector<CardObject *>();

    // Aligning needs two objects, distributing three. The buttons say so by
    // being disabled rather than by doing nothing.
    for (QToolButton *button : m_alignButtons)
        button->setEnabled(count >= 2);
    m_distributeH->setEnabled(count >= 3);
    m_distributeV->setEnabled(count >= 3);

    if (count == 0)
        m_selectionLabel->setText(tr("No selection. Aligning needs at least two objects, "
                                     "distributing at least three."));
    else if (count == 1)
        m_selectionLabel->setText(tr("1 object selected. Aligning needs at least two objects, "
                                     "distributing at least three."));
    else
        m_selectionLabel->setText(tr("%1 objects selected.").arg(count));

    if (const CardObject *first = objects.isEmpty() ? nullptr : objects.first()) {
        for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h })
            box->setEnabled(true);
        // A field the user is typing into is left alone.
        if (!m_x->hasFocus())
            m_x->setValue(toDisplayLength(first->xMm()));
        if (!m_y->hasFocus())
            m_y->setValue(toDisplayLength(first->yMm()));
        if (!m_w->hasFocus())
            m_w->setValue(toDisplayLength(first->widthMm()));
        if (!m_h->hasFocus())
            m_h->setValue(toDisplayLength(first->heightMm()));
    } else {
        for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h }) {
            box->setEnabled(false);
            box->setValue(0.0);
        }
    }

    if (m_canvas) {
        const SnapEngine::Options options = m_canvas->snapOptions();
        m_snapCard->setChecked(options.toCard);
        m_snapGuides->setChecked(options.toGuides);
        m_snapObjects->setChecked(options.toObjects);
        m_snapGrid->setChecked(options.toGrid);
        if (!m_gridSpacing->hasFocus())
            m_gridSpacing->setValue(options.gridSpacingMm);
        m_showGrid->setChecked(m_canvas->showGrid());
        m_showGuides->setChecked(m_canvas->showGuides());
    }

    m_updating = false;
}

} // namespace occ
