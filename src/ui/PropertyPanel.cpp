#include "ui/PropertyPanel.h"

#include "canvas/CardCanvas.h"
#include "commands/UndoCommands.h"
#include "core/BarcodeObject.h"
#include "core/CardDocument.h"
#include "core/CardSide.h"
#include "core/GroupObject.h"
#include "core/ImageObject.h"
#include "core/PhotoObject.h"
#include "core/QrObject.h"
#include "core/ShapeObject.h"
#include "core/TextObject.h"
#include "codes/BarcodeEncoder.h"
#include "project/AssetStore.h"
#include "ui/IconFactory.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>

namespace occ {

// ---------------------------------------------------------------------------
// ColorButton - a swatch that opens a colour dialog.
//
// It is deliberately not a QColorDialog with a preview label next to it: a
// designer changes a fill colour dozens of times per card, and one button that
// shows the current colour and opens the picker is two clicks fewer every time.
//
// Defined here because it is not part of any public interface - no other file
// has any business constructing one.
// ---------------------------------------------------------------------------
class ColorButton : public QToolButton
{
public:
    explicit ColorButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        setAutoRaise(false);
        setColor(Qt::black);
        connect(this, &QToolButton::clicked, this, [this] { chooseColor(); });
    }

    QColor color() const { return m_color; }

    void setColor(const QColor &color)
    {
        m_color = color;
        QPixmap swatch(16, 16);
        swatch.fill(Qt::transparent);
        {
            QPainter painter(&swatch);
            painter.setRenderHint(QPainter::Antialiasing, true);
            // A checkerboard shows through a translucent colour so an
            // "invisible" fill is visible as such instead of looking solid.
            painter.fillRect(QRect(0, 0, 8, 8), QColor(200, 200, 200));
            painter.fillRect(QRect(8, 0, 8, 8), QColor(120, 120, 120));
            painter.fillRect(QRect(0, 8, 8, 8), QColor(120, 120, 120));
            painter.fillRect(QRect(8, 8, 8, 8), QColor(200, 200, 200));
            painter.setBrush(m_color);
            painter.setPen(QPen(QColor(80, 80, 80), 1));
            painter.drawRect(QRectF(0.5, 0.5, 15, 15));
        }
        setIcon(QIcon(swatch));
        const QString text = m_color.alpha() == 255
                                 ? m_color.name(QColor::HexRgb).toUpper()
                                 : m_color.name(QColor::HexArgb).toUpper();
        setText(text);
    }

    // Called with the new colour after the user confirmed one.
    std::function<void(const QColor &)> onColorChosen;
    // Title of the picker dialog, e.g. "Text colour".
    void setPickTitle(const QString &title) { m_pickTitle = title; }

private:
    void chooseColor()
    {
        QColorDialog dialog(m_color, this);
        dialog.setOption(QColorDialog::ShowAlphaChannel, true);
        dialog.setWindowTitle(m_pickTitle.isEmpty() ? text() : m_pickTitle);
        if (dialog.exec() != QDialog::Accepted)
            return;
        const QColor chosen = dialog.currentColor();
        if (!chosen.isValid() || chosen == m_color)
            return;
        setColor(chosen);
        if (onColorChosen)
            onColorChosen(chosen);
    }

    QColor  m_color = Qt::black;
    QString m_pickTitle;
};

namespace {

// Spin boxes use keyboard tracking off: without it every digit typed into
// "12.5" would push an undo step for "1" and "12".
QDoubleSpinBox *makeSpin(QWidget *parent, double minimum, double maximum, int decimals,
                         double step, const QString &suffix = QString())
{
    auto *box = new QDoubleSpinBox(parent);
    box->setRange(minimum, maximum);
    box->setDecimals(decimals);
    box->setSingleStep(step);
    box->setKeyboardTracking(false);
    box->setMinimumWidth(76);
    box->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    if (!suffix.isEmpty())
        box->setSuffix(suffix);
    return box;
}

QGroupBox *makeGroup(const QString &title, QWidget *parent, QVBoxLayout **inner)
{
    auto *group = new QGroupBox(title, parent);
    auto *layout = new QVBoxLayout(group);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    if (inner)
        *inner = layout;
    return group;
}

QFormLayout *addForm(QVBoxLayout *outer)
{
    auto *form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    outer->addLayout(form);
    return form;
}

QToolButton *makeToggle(QWidget *parent, const QString &iconName, const QString &tip)
{
    auto *button = new QToolButton(parent);
    button->setIcon(IconFactory::icon(iconName));
    button->setIconSize(QSize(18, 18));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolTip(tip);
    return button;
}

} // namespace

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void PropertyPanel::applyEdit(const std::function<void(CardObject &)> &mutate,
                              const QString &text, const QString &mergeKey)
{
    // A change that came from the document must never be written back.
    if (m_updating || !editable())
        return;

    const QVector<CardObject *> objects = selectedObjects();
    if (objects.isEmpty())
        return;

    const CardSideId side = currentSide();
    CardSide &cardSide = m_document->side(side);

    QVector<ObjectId> ids;
    ids.reserve(objects.size());
    for (const CardObject *object : objects)
        ids.append(object->id());

    // One capture before, one after: every property change in this panel - and
    // every object in a multi selection - becomes a single history entry.
    const QVector<QPair<ObjectId, QJsonObject>> before =
        ModifyObjectsCommand::captureState(cardSide, ids);
    for (CardObject *object : objects)
        mutate(*object);
    const QVector<QPair<ObjectId, QJsonObject>> after =
        ModifyObjectsCommand::captureState(cardSide, ids);

    if (before == after)
        return;

    m_canvas->undoStack()->push(
        new ModifyObjectsCommand(m_document, side, before, after, text, mergeKey));
    emit statusMessage(text);
    emit requestRepaint();
}

void PropertyPanel::showPageFor(const QVector<CardObject *> &objects)
{
    auto *message = findChild<QLabel *>(QStringLiteral("PropertyPanelMessage"));

    if (objects.isEmpty()) {
        m_stack->setCurrentIndex(0);
        if (message)
            message->setText(tr("Select an object on the card or in the Layers panel to "
                                "edit its properties."));
        return;
    }

    if (objects.size() > 1) {
        const ObjectType type = objects.first()->type();
        bool sameType = true;
        for (const CardObject *object : objects) {
            if (object->type() != type) {
                sameType = false;
                break;
            }
        }
        if (!sameType) {
            m_stack->setCurrentIndex(0);
            if (message)
                message->setText(tr("%1 objects of different types are selected. The common "
                                    "fields below are applied to all of them.")
                                     .arg(objects.size()));
            return;
        }
    }

    switch (objects.first()->type()) {
    case ObjectType::Text:    m_stack->setCurrentIndex(1); break;
    case ObjectType::Image:   m_stack->setCurrentIndex(2); break;
    case ObjectType::Photo:   m_stack->setCurrentIndex(3); break;
    case ObjectType::Shape:   m_stack->setCurrentIndex(4); break;
    case ObjectType::QrCode:  m_stack->setCurrentIndex(5); break;
    case ObjectType::Barcode: m_stack->setCurrentIndex(6); break;
    case ObjectType::Group:   m_stack->setCurrentIndex(0); break;
    }
    if (objects.first()->type() == ObjectType::Group && message)
        message->setText(tr("A group is edited as a whole. Ungroup it to edit its members."));
}

void PropertyPanel::reportValidity(QLabel *label, const QString &error, const QString &warning)
{
    if (!label)
        return;

    QPalette palette = label->palette();
    if (!error.isEmpty()) {
        palette.setColor(QPalette::WindowText, QColor(190, 40, 40));
        label->setText(tr("Not encodable: %1").arg(error));
    } else if (!warning.isEmpty()) {
        palette.setColor(QPalette::WindowText, QColor(180, 110, 0));
        label->setText(warning);
    } else {
        palette.setColor(QPalette::WindowText, QColor(20, 130, 60));
        label->setText(tr("Encodable with the current settings."));
    }
    label->setPalette(palette);
}

double PropertyPanel::toDisplayLength(double mm) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::mmToInch(mm)
               : mm;
}

double PropertyPanel::fromDisplayLength(double value) const
{
    return AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches
               ? units::inchToMm(value)
               : value;
}

void PropertyPanel::updateUnitSuffixes()
{
    const bool inches =
        AppSettings::instance().unitSystem() == AppSettings::UnitSystem::Inches;
    const QString suffix = inches ? tr(" in") : tr(" mm");
    const int decimals = inches ? 3 : 2;

    for (QDoubleSpinBox *box : { m_x, m_y, m_w, m_h }) {
        if (!box)
            continue;
        box->setSuffix(suffix);
        box->setDecimals(decimals);
        box->setSingleStep(inches ? 0.02 : 0.5);
    }
    if (m_w && m_h) {
        const double minSize = inches ? 0.01 : 0.1;
        m_w->setRange(minSize, toDisplayLength(kMaxCardMm));
        m_h->setRange(minSize, toDisplayLength(kMaxCardMm));
    }
}

PropertyPanel::PropertyPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PropertyPanel"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->addWidget(scroll);

    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    m_selectionLabel = new QLabel(tr("No selection"), content);
    QFont selectionFont = m_selectionLabel->font();
    selectionFont.setBold(true);
    m_selectionLabel->setFont(selectionFont);
    m_selectionLabel->setWordWrap(true);
    layout->addWidget(m_selectionLabel);

    layout->addWidget(buildCommonSection());

    m_stack = new QStackedWidget(content);
    m_stack->addWidget(buildMessagePage());   // 0: nothing selected
    m_stack->addWidget(buildTextPage());      // 1
    m_stack->addWidget(buildImagePage());     // 2
    m_stack->addWidget(buildPhotoPage());     // 3
    m_stack->addWidget(buildShapePage());     // 4
    m_stack->addWidget(buildQrPage());        // 5
    m_stack->addWidget(buildBarcodePage());   // 6
    layout->addWidget(m_stack);

    layout->addStretch(1);
    scroll->setWidget(content);

    updateUnitSuffixes();
    showPageFor({});
}

void PropertyPanel::setDocument(CardDocument *document)
{
    if (m_document == document)
        return;
    if (m_document)
        m_document->disconnect(this);
    m_document = document;
    if (m_document) {
        connect(m_document, &CardDocument::contentsChanged, this, &PropertyPanel::refresh);
        connect(m_document, &CardDocument::sideChanged, this, [this](CardSideId) { refresh(); });
        connect(m_document, &CardDocument::geometryChanged, this, &PropertyPanel::refresh);
    }
    refresh();
}

void PropertyPanel::setCanvas(CardCanvas *canvas)
{
    if (m_canvas == canvas)
        return;
    if (m_canvas)
        m_canvas->disconnect(this);
    m_canvas = canvas;
    if (m_canvas)
        connect(m_canvas, &CardCanvas::selectionChanged, this, &PropertyPanel::refresh);
    refresh();
}

CardSideId PropertyPanel::currentSide() const
{
    return m_canvas ? m_canvas->currentSide() : CardSideId::Front;
}

bool PropertyPanel::editable() const
{
    return m_document && m_canvas && m_canvas->undoStack();
}

QVector<CardObject *> PropertyPanel::selectedObjects() const
{
    if (!m_canvas)
        return {};
    return m_canvas->selection();
}

QWidget *PropertyPanel::buildMessagePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *label = new QLabel(tr("Select an object on the card or in the Layers panel "
                                "to edit its properties."), page);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setObjectName(QStringLiteral("PropertyPanelMessage"));
    layout->addWidget(label);
    layout->addStretch(1);
    return page;
}

QWidget *PropertyPanel::buildTextPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QVBoxLayout *fontInner = nullptr;
    QGroupBox *fontGroup = makeGroup(tr("Font"), page, &fontInner);
    QFormLayout *fontForm = addForm(fontInner);

    m_fontFamily = new QFontComboBox(fontGroup);
    m_fontFamily->setEditable(false);
    m_fontFamily->setMinimumWidth(120);
    // A QFontComboBox draws every family in its own face, which is the live
    // preview: what the list shows is what the card will use.
    m_fontFamily->setToolTip(tr("Preview each family in its own typeface."));
    fontForm->addRow(tr("Family"), m_fontFamily);

    m_fontSize = makeSpin(fontGroup, 1.0, 400.0, 1, 0.5, QStringLiteral(" pt"));
    fontForm->addRow(tr("Size"), m_fontSize);

    auto *styleRow = new QHBoxLayout();
    styleRow->setSpacing(2);
    m_bold = new QToolButton(fontGroup);
    m_italic = new QToolButton(fontGroup);
    m_underline = new QToolButton(fontGroup);
    QToolButton *styleButtons[] = { m_bold, m_italic, m_underline };
    for (QToolButton *button : styleButtons) {
        button->setCheckable(true);
        button->setAutoRaise(true);
        button->setMinimumWidth(28);
    }
    QFont boldFont = m_bold->font();
    boldFont.setBold(true);
    boldFont.setItalic(false);
    QFont italicFont = m_italic->font();
    italicFont.setItalic(true);
    QFont underlineFont = m_underline->font();
    underlineFont.setUnderline(true);
    m_bold->setText(QStringLiteral("B"));
    m_bold->setToolTip(tr("Bold"));
    m_bold->setFont(boldFont);
    m_italic->setText(QStringLiteral("I"));
    m_italic->setToolTip(tr("Italic"));
    m_italic->setFont(italicFont);
    m_underline->setText(QStringLiteral("U"));
    m_underline->setToolTip(tr("Underline"));
    m_underline->setFont(underlineFont);
    styleRow->addWidget(m_bold);
    styleRow->addWidget(m_italic);
    styleRow->addWidget(m_underline);
    styleRow->addSpacing(6);

    m_textColor = new ColorButton(fontGroup);
    m_textColor->setPickTitle(tr("Text colour"));
    m_textColor->setToolTip(tr("Text colour"));
    styleRow->addWidget(new QLabel(tr("Colour"), fontGroup));
    styleRow->addWidget(m_textColor, 1);
    fontInner->addLayout(styleRow);

    auto *alignRow = new QHBoxLayout();
    alignRow->setSpacing(2);
    m_alignH = new QButtonGroup(page);
    m_alignH->setExclusive(true);
    const char *hIcons[] = { "align_left", "align_center_h", "align_right" };
    const char *hTexts[] = { QT_TRANSLATE_NOOP("occ::PropertyPanel", "Align left"),
                             QT_TRANSLATE_NOOP("occ::PropertyPanel", "Centre"),
                             QT_TRANSLATE_NOOP("occ::PropertyPanel", "Align right") };
    for (int i = 0; i < 3; ++i) {
        QToolButton *button = makeToggle(fontGroup, QString::fromLatin1(hIcons[i]),
                                         tr(hTexts[i]));
        m_alignH->addButton(button, i);
        alignRow->addWidget(button);
    }
    alignRow->addSpacing(6);
    m_alignV = new QButtonGroup(page);
    m_alignV->setExclusive(true);
    const char *vIcons[] = { "align_top", "align_middle_v", "align_bottom" };
    const char *vTexts[] = { QT_TRANSLATE_NOOP("occ::PropertyPanel", "Align top"),
                             QT_TRANSLATE_NOOP("occ::PropertyPanel", "Middle"),
                             QT_TRANSLATE_NOOP("occ::PropertyPanel", "Align bottom") };
    for (int i = 0; i < 3; ++i) {
        QToolButton *button = makeToggle(fontGroup, QString::fromLatin1(vIcons[i]),
                                         tr(vTexts[i]));
        m_alignV->addButton(button, i);
        alignRow->addWidget(button);
    }
    alignRow->addStretch(1);
    fontInner->addLayout(alignRow);

    QFormLayout *spacingForm = addForm(fontInner);
    m_lineSpacing = makeSpin(fontGroup, 50.0, 300.0, 0, 5.0, QStringLiteral(" %"));
    m_lineSpacing->setToolTip(tr("100 % is the font's natural line height."));
    spacingForm->addRow(tr("Line spacing"), m_lineSpacing);
    m_letterSpacing = makeSpin(fontGroup, -1.0, 20.0, 2, 0.1, QStringLiteral(" mm"));
    m_letterSpacing->setToolTip(tr("Extra space between characters."));
    spacingForm->addRow(tr("Letter spacing"), m_letterSpacing);

    m_wordWrap = new QCheckBox(tr("Wrap text to the box width"), fontGroup);
    m_autoShrink = new QCheckBox(tr("Shrink text to fit the box"), fontGroup);
    m_autoShrink->setToolTip(tr("Reduces the drawn size (never the requested size) so the "
                                "text cannot overflow."));
    fontInner->addWidget(m_wordWrap);
    fontInner->addWidget(m_autoShrink);

    layout->addWidget(fontGroup);

    QVBoxLayout *outlineInner = nullptr;
    QGroupBox *outlineGroup = makeGroup(tr("Outline"), page, &outlineInner);
    QFormLayout *outlineForm = addForm(outlineInner);
    m_outlineColor = new ColorButton(outlineGroup);
    m_outlineColor->setPickTitle(tr("Outline colour"));
    outlineForm->addRow(tr("Colour"), m_outlineColor);
    m_outlineWidth = makeSpin(outlineGroup, 0.0, 10.0, 2, 0.1, QStringLiteral(" mm"));
    outlineForm->addRow(tr("Width"), m_outlineWidth);
    layout->addWidget(outlineGroup);

    QVBoxLayout *contentInner = nullptr;
    QGroupBox *contentGroup = makeGroup(tr("Content"), page, &contentInner);
    m_text = new QPlainTextEdit(contentGroup);
    m_text->setPlaceholderText(tr("Text, or a placeholder such as {{employee_id}}"));
    m_text->setMinimumHeight(72);
    m_text->setTabChangesFocus(true);
    contentInner->addWidget(m_text);
    layout->addWidget(contentGroup);

    // --- wiring -------------------------------------------------------------
    connect(m_fontFamily, &QFontComboBox::currentFontChanged, this, [this](const QFont &font) {
        const QString family = font.family();
        // The closed combo previews the chosen face as well.
        QFont preview = m_fontFamily->font();
        preview.setFamily(family);
        m_fontFamily->setFont(preview);
        applyEdit([family](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setFontFamily(family);
        }, tr("Change font"), QStringLiteral("prop.font"));
        emit requestRepaint();
    });
    connect(m_fontSize, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setFontSizePt(value);
        }, tr("Change font size"), QStringLiteral("prop.fontSize"));
    });
    connect(m_bold, &QToolButton::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setBold(on);
        }, on ? tr("Bold") : tr("Remove bold"));
    });
    connect(m_italic, &QToolButton::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setItalic(on);
        }, on ? tr("Italic") : tr("Remove italic"));
    });
    connect(m_underline, &QToolButton::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setUnderline(on);
        }, on ? tr("Underline") : tr("Remove underline"));
    });
    m_textColor->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setColor(colour);
        }, tr("Change text colour"));
    };
    connect(m_alignH, &QButtonGroup::idClicked, this, [this](int id) {
        const HorizontalAlign align = id == 1 ? HorizontalAlign::Center
                                               : (id == 2 ? HorizontalAlign::Right
                                                          : HorizontalAlign::Left);
        applyEdit([align](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setHorizontalAlign(align);
        }, tr("Change text alignment"));
    });
    connect(m_alignV, &QButtonGroup::idClicked, this, [this](int id) {
        const VerticalAlign align = id == 1 ? VerticalAlign::Middle
                                            : (id == 2 ? VerticalAlign::Bottom
                                                       : VerticalAlign::Top);
        applyEdit([align](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setVerticalAlign(align);
        }, tr("Change text alignment"));
    });
    connect(m_lineSpacing, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setLineSpacingPercent(value);
        }, tr("Change line spacing"), QStringLiteral("prop.lineSpacing"));
    });
    connect(m_letterSpacing, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setLetterSpacingMm(value);
        }, tr("Change letter spacing"), QStringLiteral("prop.letterSpacing"));
    });
    connect(m_wordWrap, &QCheckBox::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setWordWrap(on);
        }, on ? tr("Wrap text") : tr("Do not wrap text"));
    });
    connect(m_autoShrink, &QCheckBox::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setAutoShrink(on);
        }, on ? tr("Shrink text to fit") : tr("Stop shrinking text"));
    });
    m_outlineColor->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setOutlineColor(colour);
        }, tr("Change outline colour"));
    };
    connect(m_outlineWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setOutlineWidthMm(value);
        }, tr("Change outline width"), QStringLiteral("prop.outlineWidth"));
    });
    // Every keystroke merges into one history entry through the shared merge
    // key, so typing a sentence does not need twenty Ctrl+Z presses.
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        if (m_updating || !editable())
            return;
        const QString value = m_text->toPlainText();
        applyEdit([value](CardObject &o) {
            if (auto *text = dynamic_cast<TextObject *>(&o))
                text->setText(value);
        }, tr("Edit text"), QStringLiteral("prop.text"));
    });

    layout->addStretch(1);
    return page;
}

QWidget *PropertyPanel::buildImagePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QVBoxLayout *sourceInner = nullptr;
    QGroupBox *sourceGroup = makeGroup(tr("Source"), page, &sourceInner);
    QFormLayout *sourceForm = addForm(sourceInner);

    m_imageSource = new QLabel(sourceGroup);
    m_imageSource->setWordWrap(true);
    m_imageSource->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceForm->addRow(tr("File"), m_imageSource);

    m_imagePixels = new QLabel(sourceGroup);
    m_imagePixels->setWordWrap(true);
    sourceForm->addRow(tr("Original"), m_imagePixels);

    m_fitMode = new QComboBox(sourceGroup);
    const char *fitTexts[] = { QT_TRANSLATE_NOOP("occ::PropertyPanel", "Stretch to fit"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "Fit inside"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "Fill the frame"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "Centre"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "Tile") };
    for (const char *text : fitTexts)
        m_fitMode->addItem(tr(text));
    m_fitMode->setToolTip(tr("How the image is mapped into its frame. The source file is "
                             "never modified."));
    sourceForm->addRow(tr("Fit"), m_fitMode);

    m_imageBackground = new ColorButton(sourceGroup);
    m_imageBackground->setPickTitle(tr("Image background colour"));
    sourceForm->addRow(tr("Background"), m_imageBackground);

    auto *buttonRow = new QHBoxLayout();
    m_replaceImage = new QPushButton(IconFactory::icon(QStringLiteral("image")),
                                     tr("Replace image..."), sourceGroup);
    m_replaceImage->setToolTip(tr("Point this object at a different file. The card layout "
                                  "does not change."));
    m_resetCrop = new QPushButton(tr("Reset crop"), sourceGroup);
    buttonRow->addWidget(m_replaceImage);
    buttonRow->addWidget(m_resetCrop);
    buttonRow->addStretch(1);
    sourceInner->addLayout(buttonRow);
    layout->addWidget(sourceGroup);

    QVBoxLayout *cropInner = nullptr;
    QGroupBox *cropGroup = makeGroup(tr("Crop (non-destructive)"), page, &cropInner);
    QFormLayout *cropForm = addForm(cropInner);
    auto *cropHint = new QLabel(tr("Percent of the source image cut away from each edge. "
                                   "The file itself is untouched."), cropGroup);
    cropHint->setWordWrap(true);
    cropForm->addRow(cropHint);

    m_cropLeft = makeSpin(cropGroup, 0.0, 49.0, 1, 1.0, QStringLiteral(" %"));
    m_cropTop = makeSpin(cropGroup, 0.0, 49.0, 1, 1.0, QStringLiteral(" %"));
    m_cropRight = makeSpin(cropGroup, 0.0, 49.0, 1, 1.0, QStringLiteral(" %"));
    m_cropBottom = makeSpin(cropGroup, 0.0, 49.0, 1, 1.0, QStringLiteral(" %"));
    cropForm->addRow(tr("Left"), m_cropLeft);
    cropForm->addRow(tr("Top"), m_cropTop);
    cropForm->addRow(tr("Right"), m_cropRight);
    cropForm->addRow(tr("Bottom"), m_cropBottom);
    layout->addWidget(cropGroup);

    // --- wiring -------------------------------------------------------------
    connect(m_fitMode, &QComboBox::currentIndexChanged, this, [this](int index) {
        const ImageFitMode mode = static_cast<ImageFitMode>(index);
        applyEdit([mode](CardObject &o) {
            if (auto *image = dynamic_cast<ImageObject *>(&o))
                image->setFitMode(mode);
        }, tr("Change image fit"));
    });
    m_imageBackground->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *image = dynamic_cast<ImageObject *>(&o))
                image->setBackgroundFill(colour);
        }, tr("Change image background"));
    };
    connect(m_replaceImage, &QPushButton::clicked, this,
            [this] { chooseReplacementImage(); });
    connect(m_resetCrop, &QPushButton::clicked, this, [this] {
        applyEdit([](CardObject &o) {
            if (auto *image = dynamic_cast<ImageObject *>(&o))
                image->resetCrop();
        }, tr("Reset crop"));
        emit statusMessage(tr("Crop reset."));
        refresh();
    });

    const auto cropChanged = [this] {
        if (m_updating || !editable())
            return;
        const double left = m_cropLeft->value() / 100.0;
        const double top = m_cropTop->value() / 100.0;
        const double right = m_cropRight->value() / 100.0;
        const double bottom = m_cropBottom->value() / 100.0;
        const QRectF rect(left, top, qMax(0.01, 1.0 - left - right),
                          qMax(0.01, 1.0 - top - bottom));
        applyEdit([rect](CardObject &o) {
            if (auto *image = dynamic_cast<ImageObject *>(&o))
                image->setSourceRect(rect);
        }, tr("Crop image"), QStringLiteral("prop.crop"));
    };
    connect(m_cropLeft, &QDoubleSpinBox::valueChanged, this, cropChanged);
    connect(m_cropTop, &QDoubleSpinBox::valueChanged, this, cropChanged);
    connect(m_cropRight, &QDoubleSpinBox::valueChanged, this, cropChanged);
    connect(m_cropBottom, &QDoubleSpinBox::valueChanged, this, cropChanged);

    layout->addStretch(1);
    return page;
}

void PropertyPanel::chooseReplacementImage()
{
    if (!editable() || !m_document || !m_document->assets())
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Replace image"), AppSettings::instance().lastOpenDirectory(),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff *.webp);;All files (*)"));
    if (path.isEmpty())
        return;

    AppSettings::instance().setLastOpenDirectory(QFileInfo(path).absolutePath());

    QString error;
    const QString assetId = m_document->assets()->addImageFile(path, &error);
    if (assetId.isEmpty()) {
        emit statusMessage(tr("The image could not be imported: %1").arg(error));
        return;
    }

    applyEdit([assetId](CardObject &o) {
        if (auto *image = dynamic_cast<ImageObject *>(&o))
            image->setAssetId(assetId);
    }, tr("Replace image"));
    emit statusMessage(tr("Image replaced with \"%1\".").arg(QFileInfo(path).fileName()));
    refresh();
}

QWidget *PropertyPanel::buildPhotoPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QVBoxLayout *shapeInner = nullptr;
    QGroupBox *shapeGroup = makeGroup(tr("Photo frame"), page, &shapeInner);
    QFormLayout *form = addForm(shapeInner);

    m_cropShape = new QComboBox(shapeGroup);
    m_cropShape->addItem(tr("Rectangle"));
    m_cropShape->addItem(tr("Rounded rectangle"));
    m_cropShape->addItem(tr("Circle"));
    form->addRow(tr("Crop shape"), m_cropShape);

    m_cornerRadius = makeSpin(shapeGroup, 0.0, 20.0, 2, 0.5, QStringLiteral(" mm"));
    form->addRow(tr("Corner radius"), m_cornerRadius);

    m_borderWidth = makeSpin(shapeGroup, 0.0, 10.0, 2, 0.1, QStringLiteral(" mm"));
    form->addRow(tr("Border width"), m_borderWidth);

    m_borderColor = new ColorButton(shapeGroup);
    m_borderColor->setPickTitle(tr("Border colour"));
    form->addRow(tr("Border colour"), m_borderColor);

    m_lockAspect = new QCheckBox(tr("Lock the current aspect ratio"), shapeGroup);
    m_lockAspect->setToolTip(tr("Keeps the photo from being stretched while it is resized "
                                "on the card."));
    shapeInner->addWidget(m_lockAspect);
    m_aspectLabel = new QLabel(shapeGroup);
    m_aspectLabel->setObjectName(QStringLiteral("PhotoAspectLabel"));
    shapeInner->addWidget(m_aspectLabel);

    auto *buttonRow = new QHBoxLayout();
    m_replacePhoto = new QPushButton(IconFactory::icon(QStringLiteral("photo")),
                                     tr("Replace photo..."), shapeGroup);
    m_makeCircular = new QPushButton(tr("Make circular"), shapeGroup);
    m_makeCircular->setToolTip(tr("Makes the frame square and clips the photograph to a "
                                  "circle."));
    buttonRow->addWidget(m_replacePhoto);
    buttonRow->addWidget(m_makeCircular);
    buttonRow->addStretch(1);
    shapeInner->addLayout(buttonRow);
    layout->addWidget(shapeGroup);

    // --- wiring -------------------------------------------------------------
    connect(m_cropShape, &QComboBox::currentIndexChanged, this, [this](int index) {
        const CropShape shape = index == 1 ? CropShape::RoundedRectangle
                                           : (index == 2 ? CropShape::Ellipse
                                                         : CropShape::Rectangle);
        applyEdit([shape](CardObject &o) {
            if (auto *photo = dynamic_cast<PhotoObject *>(&o))
                photo->setCropShape(shape);
        }, tr("Change photo shape"));
    });
    connect(m_cornerRadius, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *photo = dynamic_cast<PhotoObject *>(&o))
                photo->setCornerRadiusMm(value);
        }, tr("Change corner radius"), QStringLiteral("prop.cornerRadius"));
    });
    connect(m_borderWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *photo = dynamic_cast<PhotoObject *>(&o))
                photo->setBorderWidthMm(value);
        }, tr("Change border width"), QStringLiteral("prop.borderWidth"));
    });
    m_borderColor->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *photo = dynamic_cast<PhotoObject *>(&o))
                photo->setBorderColor(colour);
        }, tr("Change border colour"));
    };
    connect(m_lockAspect, &QCheckBox::toggled, this, [this](bool on) {
        const QVector<CardObject *> objects = selectedObjects();
        const CardObject *first = objects.isEmpty() ? nullptr : objects.first();
        if (!first)
            return;
        const double ratio = on && first->heightMm() > 0.0
                                 ? first->widthMm() / first->heightMm()
                                 : 0.0;
        applyEdit([ratio](CardObject &o) {
            if (auto *photo = dynamic_cast<PhotoObject *>(&o))
                photo->setLockedAspectRatio(ratio);
        }, on ? tr("Lock aspect ratio") : tr("Unlock aspect ratio"));
    });
    connect(m_replacePhoto, &QPushButton::clicked, this,
            [this] { chooseReplacementImage(); });
    connect(m_makeCircular, &QPushButton::clicked, this, [this] {
        applyEdit([](CardObject &o) {
            if (auto *photo = dynamic_cast<PhotoObject *>(&o))
                photo->applyCircularCrop();
        }, tr("Make photo circular"));
        emit statusMessage(tr("The photo frame is now circular."));
        refresh();
    });

    layout->addStretch(1);
    return page;
}

QWidget *PropertyPanel::buildShapePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QVBoxLayout *inner = nullptr;
    QGroupBox *group = makeGroup(tr("Shape"), page, &inner);
    QFormLayout *form = addForm(inner);

    m_shapeKind = new QComboBox(group);
    const char *kindTexts[] = { QT_TRANSLATE_NOOP("occ::PropertyPanel", "Rectangle"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Rounded rectangle"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Ellipse"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Line"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Triangle"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Polygon"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Star"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Arrow") };
    for (const char *text : kindTexts)
        m_shapeKind->addItem(tr(text));
    form->addRow(tr("Kind"), m_shapeKind);

    m_fillColor = new ColorButton(group);
    m_fillColor->setPickTitle(tr("Fill colour"));
    form->addRow(tr("Fill"), m_fillColor);

    m_strokeColor = new ColorButton(group);
    m_strokeColor->setPickTitle(tr("Stroke colour"));
    form->addRow(tr("Stroke"), m_strokeColor);

    m_strokeWidth = makeSpin(group, 0.0, 20.0, 2, 0.1, QStringLiteral(" mm"));
    form->addRow(tr("Stroke width"), m_strokeWidth);

    m_shapeCornerRadius = makeSpin(group, 0.0, 30.0, 2, 0.5, QStringLiteral(" mm"));
    form->addRow(tr("Corner radius"), m_shapeCornerRadius);

    m_sides = makeSpin(group, 3, 64, 0, 1.0);
    m_sides->setToolTip(tr("Number of sides for a polygon, or points for a triangle."));
    form->addRow(tr("Sides"), m_sides);

    m_starRatio = makeSpin(group, 0.05, 0.95, 2, 0.05);
    m_starRatio->setToolTip(tr("Inner radius of a star as a fraction of the outer radius."));
    form->addRow(tr("Star inner ratio"), m_starRatio);

    layout->addWidget(group);
    layout->addStretch(1);

    // --- wiring -------------------------------------------------------------
    connect(m_shapeKind, &QComboBox::currentIndexChanged, this, [this](int index) {
        const ShapeKind kind = static_cast<ShapeKind>(index);
        applyEdit([kind](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setKind(kind);
        }, tr("Change shape kind"));
        // Only the fields a kind actually uses stay enabled; the rest are
        // greyed out rather than silently ignored.
        m_shapeCornerRadius->setEnabled(kind == ShapeKind::RoundedRectangle);
        m_sides->setEnabled(kind == ShapeKind::Polygon || kind == ShapeKind::Triangle);
        m_starRatio->setEnabled(kind == ShapeKind::Star);
        m_fillColor->setEnabled(kind != ShapeKind::Line && kind != ShapeKind::Arrow);
        refresh();
    });
    m_fillColor->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setFillColor(colour);
        }, tr("Change fill colour"));
    };
    m_strokeColor->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setStrokeColor(colour);
        }, tr("Change stroke colour"));
    };
    connect(m_strokeWidth, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setStrokeWidthMm(value);
        }, tr("Change stroke width"), QStringLiteral("prop.strokeWidth"));
    });
    connect(m_shapeCornerRadius, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setCornerRadiusMm(value);
        }, tr("Change corner radius"), QStringLiteral("prop.shapeCorner"));
    });
    connect(m_sides, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const int sides = int(value);
        applyEdit([sides](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setSides(sides);
        }, tr("Change polygon sides"), QStringLiteral("prop.sides"));
    });
    connect(m_starRatio, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *shape = dynamic_cast<ShapeObject *>(&o))
                shape->setStarInnerRatio(value);
        }, tr("Change star inner radius"), QStringLiteral("prop.starRatio"));
    });

    return page;
}

QWidget *PropertyPanel::buildQrPage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QVBoxLayout *inner = nullptr;
    QGroupBox *group = makeGroup(tr("QR code"), page, &inner);
    QFormLayout *form = addForm(inner);

    m_qrKind = new QComboBox(group);
    const char *kindTexts[] = { QT_TRANSLATE_NOOP("occ::PropertyPanel", "Plain text"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Web address"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Employee ID"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Student ID"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Patient ID"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Contact card"),
                                QT_TRANSLATE_NOOP("occ::PropertyPanel", "Custom") };
    for (const char *text : kindTexts)
        m_qrKind->addItem(tr(text));
    form->addRow(tr("Content kind"), m_qrKind);

    m_qrEcc = new QComboBox(group);
    const char *eccTexts[] = { QT_TRANSLATE_NOOP("occ::PropertyPanel", "L - 7 % recovery"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "M - 15 % recovery"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "Q - 25 % recovery"),
                               QT_TRANSLATE_NOOP("occ::PropertyPanel", "H - 30 % recovery") };
    for (const char *text : eccTexts)
        m_qrEcc->addItem(tr(text));
    form->addRow(tr("Error correction"), m_qrEcc);

    m_qrQuietZone = makeSpin(group, 0, 16, 0, 1.0);
    m_qrQuietZone->setToolTip(tr("Quiet zone in modules. The standard requires 4."));
    form->addRow(tr("Quiet zone"), m_qrQuietZone);

    m_qrForeground = new ColorButton(group);
    m_qrForeground->setPickTitle(tr("QR symbol colour"));
    form->addRow(tr("Symbol"), m_qrForeground);

    m_qrBackground = new ColorButton(group);
    m_qrBackground->setPickTitle(tr("QR background colour"));
    form->addRow(tr("Background"), m_qrBackground);

    layout->addWidget(group);

    QVBoxLayout *dataInner = nullptr;
    QGroupBox *dataGroup = makeGroup(tr("Data"), page, &dataInner);
    m_qrData = new QPlainTextEdit(dataGroup);
    m_qrData->setPlaceholderText(tr("Data to encode, or a placeholder such as {{employee_id}}"));
    m_qrData->setMinimumHeight(80);
    m_qrData->setTabChangesFocus(true);
    dataInner->addWidget(m_qrData);

    m_qrValidity = new QLabel(dataGroup);
    m_qrValidity->setWordWrap(true);
    m_qrValidity->setObjectName(QStringLiteral("QrValidityLabel"));
    dataInner->addWidget(m_qrValidity);
    layout->addWidget(dataGroup);
    layout->addStretch(1);

    // --- wiring -------------------------------------------------------------
    connect(m_qrKind, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QrContentKind kind = static_cast<QrContentKind>(index);
        applyEdit([kind](CardObject &o) {
            if (auto *qr = dynamic_cast<QrObject *>(&o))
                qr->setContentKind(kind);
        }, tr("Change QR content kind"));
        refresh();
    });
    connect(m_qrEcc, &QComboBox::currentIndexChanged, this, [this](int index) {
        const QrErrorCorrection ecc = static_cast<QrErrorCorrection>(index);
        applyEdit([ecc](CardObject &o) {
            if (auto *qr = dynamic_cast<QrObject *>(&o))
                qr->setErrorCorrection(ecc);
        }, tr("Change QR error correction"));
        refresh();
    });
    connect(m_qrQuietZone, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const int modules = int(value);
        applyEdit([modules](CardObject &o) {
            if (auto *qr = dynamic_cast<QrObject *>(&o))
                qr->setQuietZoneModules(modules);
        }, tr("Change QR quiet zone"), QStringLiteral("prop.qrQuiet"));
    });
    m_qrForeground->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *qr = dynamic_cast<QrObject *>(&o))
                qr->setForegroundColor(colour);
        }, tr("Change QR colour"));
    };
    m_qrBackground->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *qr = dynamic_cast<QrObject *>(&o))
                qr->setBackgroundColor(colour);
        }, tr("Change QR background"));
    };
    connect(m_qrData, &QPlainTextEdit::textChanged, this, [this] {
        if (m_updating || !editable())
            return;
        const QString value = m_qrData->toPlainText();
        applyEdit([value](CardObject &o) {
            if (auto *qr = dynamic_cast<QrObject *>(&o))
                qr->setData(value);
        }, tr("Edit QR data"), QStringLiteral("prop.qrData"));
        const QVector<CardObject *> objects = selectedObjects();
        if (!objects.isEmpty()) {
            if (auto *qr = dynamic_cast<QrObject *>(objects.first()))
                reportValidity(m_qrValidity, qr->validationError(), QString());
        }
    });

    return page;
}

QWidget *PropertyPanel::buildBarcodePage()
{
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    QVBoxLayout *inner = nullptr;
    QGroupBox *group = makeGroup(tr("Barcode"), page, &inner);
    QFormLayout *form = addForm(inner);

    m_bcSymbology = new QComboBox(group);
    for (int i = 0; i <= int(BarcodeSymbology::Itf14); ++i) {
        const auto symbology = static_cast<BarcodeSymbology>(i);
        // The display name comes from the encoder so the UI cannot disagree
        // with the symbology that is actually encoded.
        m_bcSymbology->addItem(BarcodeEncoder::displayName(symbology));
    }
    m_bcSymbology->setToolTip(tr("Symbology used for the bars."));
    form->addRow(tr("Symbology"), m_bcSymbology);

    m_bcHumanText = new QCheckBox(tr("Print the value under the bars"), group);
    m_bcHumanText->setToolTip(tr("Not every symbology supports human readable text; the "
                                 "option is disabled for those."));
    form->addRow(m_bcHumanText);

    m_bcHumanSize = makeSpin(group, 2.0, 40.0, 1, 0.5, QStringLiteral(" pt"));
    form->addRow(tr("Text size"), m_bcHumanSize);

    m_bcQuietZone = makeSpin(group, 0, 40, 0, 1.0);
    m_bcQuietZone->setToolTip(tr("Quiet zone in modules on each side (10 is the usual "
                                 "recommendation)."));
    form->addRow(tr("Quiet zone"), m_bcQuietZone);

    m_bcForeground = new ColorButton(group);
    m_bcForeground->setPickTitle(tr("Bar colour"));
    form->addRow(tr("Bars"), m_bcForeground);

    m_bcBackground = new ColorButton(group);
    m_bcBackground->setPickTitle(tr("Barcode background colour"));
    form->addRow(tr("Background"), m_bcBackground);

    layout->addWidget(group);

    QVBoxLayout *dataInner = nullptr;
    QGroupBox *dataGroup = makeGroup(tr("Data"), page, &dataInner);
    m_bcData = new QLineEdit(dataGroup);
    m_bcData->setPlaceholderText(tr("Value, or a placeholder such as {{employee_id}}"));
    dataInner->addWidget(m_bcData);

    m_bcValidity = new QLabel(dataGroup);
    m_bcValidity->setWordWrap(true);
    m_bcValidity->setObjectName(QStringLiteral("BarcodeValidityLabel"));
    dataInner->addWidget(m_bcValidity);
    layout->addWidget(dataGroup);
    layout->addStretch(1);

    // --- wiring -------------------------------------------------------------
    connect(m_bcSymbology, &QComboBox::currentIndexChanged, this, [this](int index) {
        const BarcodeSymbology symbology = static_cast<BarcodeSymbology>(index);
        applyEdit([symbology](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setSymbology(symbology);
        }, tr("Change barcode symbology"));
        refresh();
    });
    connect(m_bcHumanText, &QCheckBox::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setShowHumanText(on);
        }, on ? tr("Show barcode text") : tr("Hide barcode text"));
    });
    connect(m_bcHumanSize, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setHumanTextSizePt(value);
        }, tr("Change barcode text size"), QStringLiteral("prop.bcTextSize"));
    });
    connect(m_bcQuietZone, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const int modules = int(value);
        applyEdit([modules](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setQuietZoneModules(modules);
        }, tr("Change barcode quiet zone"), QStringLiteral("prop.bcQuiet"));
    });
    m_bcForeground->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setForegroundColor(colour);
        }, tr("Change bar colour"));
    };
    m_bcBackground->onColorChosen = [this](const QColor &colour) {
        applyEdit([colour](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setBackgroundColor(colour);
        }, tr("Change barcode background"));
    };
    connect(m_bcData, &QLineEdit::textEdited, this, [this](const QString &value) {
        applyEdit([value](CardObject &o) {
            if (auto *barcode = dynamic_cast<BarcodeObject *>(&o))
                barcode->setData(value);
        }, tr("Edit barcode data"), QStringLiteral("prop.bcData"));
        refresh();
    });

    return page;
}

QWidget *PropertyPanel::buildCommonSection()
{
    QVBoxLayout *inner = nullptr;
    QGroupBox *group = makeGroup(tr("Position and size"), this, &inner);

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(6);
    grid->setVerticalSpacing(4);
    inner->addLayout(grid);

    m_x = makeSpin(group, -kMaxCardMm, kMaxCardMm, 2, 0.5);
    m_y = makeSpin(group, -kMaxCardMm, kMaxCardMm, 2, 0.5);
    m_w = makeSpin(group, 0.1, kMaxCardMm, 2, 0.5);
    m_h = makeSpin(group, 0.1, kMaxCardMm, 2, 0.5);

    m_rotation = makeSpin(group, -360.0, 360.0, 1, 1.0, QStringLiteral(" \u00B0"));
    m_opacity = makeSpin(group, 0.0, 100.0, 0, 5.0, QStringLiteral(" %"));

    grid->addWidget(new QLabel(tr("X"), group), 0, 0);
    grid->addWidget(m_x, 0, 1);
    grid->addWidget(new QLabel(tr("Y"), group), 0, 2);
    grid->addWidget(m_y, 0, 3);
    grid->addWidget(new QLabel(tr("W"), group), 1, 0);
    grid->addWidget(m_w, 1, 1);
    grid->addWidget(new QLabel(tr("H"), group), 1, 2);
    grid->addWidget(m_h, 1, 3);
    grid->addWidget(new QLabel(tr("Rotate"), group), 2, 0);
    grid->addWidget(m_rotation, 2, 1);
    grid->addWidget(new QLabel(tr("Opacity"), group), 2, 2);
    grid->addWidget(m_opacity, 2, 3);

    m_visible = new QCheckBox(tr("Visible"), group);
    m_locked = new QCheckBox(tr("Locked"), group);
    m_visible->setToolTip(tr("Hidden objects are not printed and cannot be picked."));
    m_locked->setToolTip(tr("Locked objects cannot be moved or resized on the card."));
    grid->addWidget(m_visible, 3, 0, 1, 2);
    grid->addWidget(m_locked, 3, 2, 1, 2);

    connect(m_x, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double mm = fromDisplayLength(value);
        applyEdit([mm](CardObject &o) { o.setXMm(mm); }, tr("Move"), QStringLiteral("prop.x"));
    });
    connect(m_y, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double mm = fromDisplayLength(value);
        applyEdit([mm](CardObject &o) { o.setYMm(mm); }, tr("Move"), QStringLiteral("prop.y"));
    });
    connect(m_w, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double mm = fromDisplayLength(value);
        applyEdit([mm](CardObject &o) { o.setWidthMm(mm); }, tr("Resize"),
                  QStringLiteral("prop.w"));
    });
    connect(m_h, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double mm = fromDisplayLength(value);
        applyEdit([mm](CardObject &o) { o.setHeightMm(mm); }, tr("Resize"),
                  QStringLiteral("prop.h"));
    });
    connect(m_rotation, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        applyEdit([value](CardObject &o) { o.setRotationDeg(value); }, tr("Rotate"),
                  QStringLiteral("prop.rot"));
    });
    connect(m_opacity, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double opacity = value / 100.0;
        applyEdit([opacity](CardObject &o) { o.setOpacity(opacity); }, tr("Change opacity"),
                  QStringLiteral("prop.opacity"));
    });
    connect(m_visible, &QCheckBox::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) { o.setVisible(on); },
                  on ? tr("Show") : tr("Hide"));
    });
    connect(m_locked, &QCheckBox::toggled, this, [this](bool on) {
        applyEdit([on](CardObject &o) { o.setLocked(on); },
                  on ? tr("Lock") : tr("Unlock"));
    });

    return group;
}

// ---------------------------------------------------------------------------
// Refreshing from the document
// ---------------------------------------------------------------------------

void PropertyPanel::refresh()
{
    if (!m_stack)
        return;

    updateUnitSuffixes();

    const bool canEdit = editable();
    const QVector<CardObject *> objects = selectedObjects();
    CardObject *first = objects.isEmpty() ? nullptr : objects.first();

    // Widgets are only written when the value actually differs, and never while
    // they have focus: fighting the user for the caret position in a spin box
    // is the classic property-panel bug.
    const auto setSpin = [](QDoubleSpinBox *box, double value, bool enabled) {
        if (!box)
            return;
        box->setEnabled(enabled);
        if (box->hasFocus() || qFuzzyCompare(box->value() + 1.0, value + 1.0))
            return;
        QSignalBlocker blocker(box);
        box->setValue(value);
    };
    const auto setCombo = [](QComboBox *combo, int index, bool enabled) {
        if (!combo)
            return;
        combo->setEnabled(enabled);
        if (combo->currentIndex() == index)
            return;
        QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    };
    const auto setCheck = [](QCheckBox *box, bool on, bool enabled) {
        if (!box)
            return;
        box->setEnabled(enabled);
        if (box->isChecked() == on)
            return;
        QSignalBlocker blocker(box);
        box->setChecked(on);
    };
    const auto setToggle = [](QToolButton *button, bool on) {
        if (!button || button->isChecked() == on)
            return;
        QSignalBlocker blocker(button);
        button->setChecked(on);
    };
    const auto setPlain = [](QPlainTextEdit *edit, const QString &text) {
        if (!edit || edit->hasFocus() || edit->toPlainText() == text)
            return;
        QSignalBlocker blocker(edit);
        edit->setPlainText(text);
    };
    const auto setLine = [](QLineEdit *edit, const QString &text) {
        if (!edit || edit->hasFocus() || edit->text() == text)
            return;
        QSignalBlocker blocker(edit);
        edit->setText(text);
    };
    const auto setColor = [](ColorButton *button, const QColor &colour) {
        if (!button || button->color() == colour)
            return;
        button->setColor(colour);
    };

    m_updating = true;

    // --- header -------------------------------------------------------------
    if (!first)
        m_selectionLabel->setText(tr("No selection"));
    else if (objects.size() == 1)
        m_selectionLabel->setText(tr("%1: %2").arg(first->typeDisplayName(), first->name()));
    else
        m_selectionLabel->setText(tr("%1 objects selected").arg(objects.size()));

    // --- common section -----------------------------------------------------
    const bool hasSelection = first != nullptr;
    setSpin(m_x, first ? toDisplayLength(first->xMm()) : 0.0, hasSelection && canEdit);
    setSpin(m_y, first ? toDisplayLength(first->yMm()) : 0.0, hasSelection && canEdit);
    setSpin(m_w, first ? toDisplayLength(first->widthMm()) : 0.0, hasSelection && canEdit);
    setSpin(m_h, first ? toDisplayLength(first->heightMm()) : 0.0, hasSelection && canEdit);
    setSpin(m_rotation, first ? first->rotationDeg() : 0.0, hasSelection && canEdit);
    setSpin(m_opacity, first ? first->opacity() * 100.0 : 100.0, hasSelection && canEdit);
    setCheck(m_visible, first ? first->isVisible() : true, hasSelection && canEdit);
    setCheck(m_locked, first ? first->isLocked() : false, hasSelection && canEdit);

    showPageFor(objects);

    if (!first) {
        m_updating = false;
        return;
    }

    // --- type specific pages ------------------------------------------------
    if (auto *text = dynamic_cast<TextObject *>(first)) {
        if (!m_fontFamily->hasFocus())
            m_fontFamily->setCurrentFont(QFont(text->fontFamily()));
        setSpin(m_fontSize, text->fontSizePt(), true);
        setToggle(m_bold, text->bold());
        setToggle(m_italic, text->italic());
        setToggle(m_underline, text->underline());
        setColor(m_textColor, text->color());
        if (QAbstractButton *button = m_alignH->button(int(text->horizontalAlign())))
            button->setChecked(true);
        if (QAbstractButton *button = m_alignV->button(int(text->verticalAlign())))
            button->setChecked(true);
        setSpin(m_lineSpacing, text->lineSpacingPercent(), true);
        setSpin(m_letterSpacing, text->letterSpacingMm(), true);
        setCheck(m_wordWrap, text->wordWrap(), true);
        setCheck(m_autoShrink, text->autoShrink(), true);
        setColor(m_outlineColor, text->outlineColor());
        setSpin(m_outlineWidth, text->outlineWidthMm(), true);
        setPlain(m_text, text->text());
    } else if (auto *photo = dynamic_cast<PhotoObject *>(first)) {
        updateImageInfo(photo);
        setCombo(m_fitMode, int(photo->fitMode()), true);
        setColor(m_imageBackground, photo->backgroundFill());
        const QRectF source = photo->sourceRect();
        setSpin(m_cropLeft, source.x() * 100.0, true);
        setSpin(m_cropTop, source.y() * 100.0, true);
        setSpin(m_cropRight, (1.0 - source.x() - source.width()) * 100.0, true);
        setSpin(m_cropBottom, (1.0 - source.y() - source.height()) * 100.0, true);
        setCombo(m_cropShape, int(photo->cropShape()), true);
        setSpin(m_cornerRadius, photo->cornerRadiusMm(), true);
        setSpin(m_borderWidth, photo->borderWidthMm(), true);
        setColor(m_borderColor, photo->borderColor());
        const double ratio = photo->lockedAspectRatio();
        setCheck(m_lockAspect, ratio > 0.0, true);
        m_aspectLabel->setText(ratio > 0.0
                                   ? tr("Locked at %1 : 1").arg(QString::number(ratio, 'f', 3))
                                   : tr("Not locked"));
    } else if (auto *image = dynamic_cast<ImageObject *>(first)) {
        updateImageInfo(image);
        setCombo(m_fitMode, int(image->fitMode()), true);
        setColor(m_imageBackground, image->backgroundFill());
        const QRectF source = image->sourceRect();
        setSpin(m_cropLeft, source.x() * 100.0, true);
        setSpin(m_cropTop, source.y() * 100.0, true);
        setSpin(m_cropRight, (1.0 - source.x() - source.width()) * 100.0, true);
        setSpin(m_cropBottom, (1.0 - source.y() - source.height()) * 100.0, true);
    } else if (auto *shape = dynamic_cast<ShapeObject *>(first)) {
        setCombo(m_shapeKind, int(shape->kind()), true);
        setColor(m_fillColor, shape->fillColor());
        setColor(m_strokeColor, shape->strokeColor());
        setSpin(m_strokeWidth, shape->strokeWidthMm(), true);
        setSpin(m_shapeCornerRadius, shape->cornerRadiusMm(), true);
        setSpin(m_sides, shape->sides(), true);
        setSpin(m_starRatio, shape->starInnerRatio(), true);
        const ShapeKind kind = shape->kind();
        m_shapeCornerRadius->setEnabled(kind == ShapeKind::RoundedRectangle);
        m_sides->setEnabled(kind == ShapeKind::Polygon || kind == ShapeKind::Triangle);
        m_starRatio->setEnabled(kind == ShapeKind::Star);
        m_fillColor->setEnabled(kind != ShapeKind::Line && kind != ShapeKind::Arrow);
    } else if (auto *qr = dynamic_cast<QrObject *>(first)) {
        setCombo(m_qrKind, int(qr->contentKind()), true);
        setCombo(m_qrEcc, int(qr->errorCorrection()), true);
        setSpin(m_qrQuietZone, qr->quietZoneModules(), true);
        setColor(m_qrForeground, qr->foregroundColor());
        setColor(m_qrBackground, qr->backgroundColor());
        setPlain(m_qrData, qr->data());
        reportValidity(m_qrValidity, qr->validationError(), QString());
    } else if (auto *barcode = dynamic_cast<BarcodeObject *>(first)) {
        setCombo(m_bcSymbology, int(barcode->symbology()), true);
        setCheck(m_bcHumanText, barcode->showHumanText(),
                 BarcodeEncoder::supportsHumanText(barcode->symbology()));
        setSpin(m_bcHumanSize, barcode->humanTextSizePt(), true);
        setSpin(m_bcQuietZone, barcode->quietZoneModules(), true);
        setColor(m_bcForeground, barcode->foregroundColor());
        setColor(m_bcBackground, barcode->backgroundColor());
        setLine(m_bcData, barcode->data());

        double moduleMm = 0.0;
        QString warning;
        if (barcode->moduleWidthIsRisky(&moduleMm)) {
            warning = tr("The narrow bar is only %1 mm wide; below %2 mm readers start to "
                         "fail. Widen the object or shorten the value.")
                          .arg(QString::number(moduleMm, 'f', 3),
                               QString::number(BarcodeObject::minimumReliableModuleMm(), 'f', 2));
        }
        reportValidity(m_bcValidity, barcode->validationError(), warning);
    }

    m_updating = false;
}

void PropertyPanel::updateImageInfo(CardObject *object)
{
    auto *image = dynamic_cast<ImageObject *>(object);
    if (!image) {
        m_imageSource->setText(tr("Not an image"));
        m_imagePixels->setText(QString());
        return;
    }

    const AssetStore *assets = m_document ? m_document->assets() : nullptr;
    const QString assetId = image->assetId();
    if (assetId.isEmpty() || !assets || !assets->has(assetId)) {
        m_imageSource->setText(tr("No image yet - use \"Replace image...\"."));
        m_imagePixels->setText(QString());
        return;
    }

    const QString fileName = assets->fileName(assetId);
    m_imageSource->setText(fileName.isEmpty() ? tr("(unnamed image)") : fileName);

    // The decoded size is what the user needs to judge whether the image is
    // large enough for the card; the store caches the decode.
    const QSize size = assets->image(assetId).size();
    m_imagePixels->setText(size.isEmpty()
                               ? tr("Size not available")
                               : tr("%1 x %2 pixels").arg(size.width()).arg(size.height()));
}

} // namespace occ
