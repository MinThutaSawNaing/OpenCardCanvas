#include "ui/ToolsPanel.h"

#include "ui/IconFactory.h"

#include <QAction>
#include <QActionGroup>
#include <QButtonGroup>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

namespace occ {

namespace {

struct ToolEntry
{
    const char *id;
    const char *icon;
    const char *text;
    const char *shortcut;
};

const ToolEntry kTools[] = {
    { "select",  "select",  QT_TRANSLATE_NOOP("occ::ToolsPanel", "Select"),  "V" },
    { "text",    "text",    QT_TRANSLATE_NOOP("occ::ToolsPanel", "Text"),    "T" },
    { "image",   "image",   QT_TRANSLATE_NOOP("occ::ToolsPanel", "Image"),   "I" },
    { "photo",   "photo",   QT_TRANSLATE_NOOP("occ::ToolsPanel", "Photo"),   "P" },
    { "shape",   "shape",   QT_TRANSLATE_NOOP("occ::ToolsPanel", "Shape"),   "R" },
    { "line",    "shape",   QT_TRANSLATE_NOOP("occ::ToolsPanel", "Line"),    "L" },
    { "qr",      "qr",      QT_TRANSLATE_NOOP("occ::ToolsPanel", "QR code"), "Q" },
    { "barcode", "barcode", QT_TRANSLATE_NOOP("occ::ToolsPanel", "Barcode"), "B" },
};

struct ShapeEntry
{
    ShapeKind   kind;
    const char *text;
};

const ShapeEntry kShapes[] = {
    { ShapeKind::Rectangle,        QT_TRANSLATE_NOOP("occ::ToolsPanel", "Rectangle") },
    { ShapeKind::RoundedRectangle, QT_TRANSLATE_NOOP("occ::ToolsPanel", "Rounded rectangle") },
    { ShapeKind::Ellipse,          QT_TRANSLATE_NOOP("occ::ToolsPanel", "Ellipse") },
    { ShapeKind::Triangle,         QT_TRANSLATE_NOOP("occ::ToolsPanel", "Triangle") },
    { ShapeKind::Polygon,          QT_TRANSLATE_NOOP("occ::ToolsPanel", "Polygon") },
    { ShapeKind::Star,             QT_TRANSLATE_NOOP("occ::ToolsPanel", "Star") },
    { ShapeKind::Arrow,            QT_TRANSLATE_NOOP("occ::ToolsPanel", "Arrow") },
};

} // namespace

ToolsPanel::ToolsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("ToolsPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // A checkable group: the canvas must always have a tool, so a button that
    // could be untoggled again would be the wrong control.
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);

    auto *heading = new QLabel(tr("Tools"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);

    for (const ToolEntry &entry : kTools) {
        const QString id = QString::fromLatin1(entry.id);
        QToolButton *button = addTool(id, QString::fromLatin1(entry.icon),
                                      tr(entry.text),
                                      QString::fromLatin1(entry.shortcut));
        group->addButton(button);
        layout->addWidget(button);
        if (id == QLatin1String("shape"))
            m_shapeButton = button;
    }

    layout->addStretch(1);

    if (m_shapeButton) {
        buildShapeMenu();
        // Dropdown on the arrow, immediate activation on the button itself: a
        // user who just wants a rectangle never has to open a menu.
        m_shapeButton->setPopupMode(QToolButton::MenuButtonPopup);
    }

    updateCheckedState();
}

QToolButton *ToolsPanel::addTool(const QString &toolId, const QString &iconName,
                                 const QString &text, const QString &shortcut)
{
    auto *button = new QToolButton(this);
    button->setText(text);
    button->setIcon(IconFactory::icon(iconName));
    button->setIconSize(QSize(20, 20));
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    button->setProperty("occToolId", toolId);
    // The tooltip names the tool and the key that selects it: a shortcut that
    // is nowhere discoverable is the same as not having one.
    button->setToolTip(shortcut.isEmpty() ? text : tr("%1 (%2)").arg(text, shortcut));
    button->setAccessibleName(text);

    connect(button, &QToolButton::clicked, this, [this, toolId] { activate(toolId); });

    m_buttons.insert(toolId, button);
    return button;
}

void ToolsPanel::buildShapeMenu()
{
    if (!m_shapeButton)
        return;

    auto *menu = new QMenu(m_shapeButton);
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);

    for (const ShapeEntry &entry : kShapes) {
        QAction *action = menu->addAction(tr(entry.text));
        action->setCheckable(true);
        action->setChecked(entry.kind == m_shapeKind);
        action->setData(int(entry.kind));
        group->addAction(action);
        m_shapeActions.insert(int(entry.kind), action);

        const ShapeKind kind = entry.kind;
        connect(action, &QAction::triggered, this, [this, kind] {
            setCurrentShapeKind(kind);
            m_currentTool = QStringLiteral("shape");
            updateCheckedState();
            emit toolSelected(m_currentTool);
            emit shapeRequested(kind);
        });
    }

    m_shapeButton->setMenu(menu);
}

void ToolsPanel::setCurrentShapeKind(ShapeKind kind)
{
    m_shapeKind = kind;
    for (auto it = m_shapeActions.constBegin(); it != m_shapeActions.constEnd(); ++it)
        it.value()->setChecked(it.key() == int(kind));
}

void ToolsPanel::activate(const QString &toolId)
{
    m_currentTool = toolId;
    updateCheckedState();

    // The request signals are emitted after toolSelected() so a listener that
    // reacts to the tool also sees the concrete request.
    emit toolSelected(toolId);

    if (toolId == QLatin1String("image"))
        emit imageRequested();
    else if (toolId == QLatin1String("photo"))
        emit photoRequested();
    else if (toolId == QLatin1String("line"))
        emit shapeRequested(ShapeKind::Line);
    else if (toolId == QLatin1String("shape"))
        emit shapeRequested(m_shapeKind);
}

void ToolsPanel::setCurrentTool(const QString &toolId)
{
    if (toolId.isEmpty())
        return;
    m_currentTool = toolId;
    updateCheckedState();
}

void ToolsPanel::updateCheckedState()
{
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
        it.value()->setChecked(it.key() == m_currentTool);
}

} // namespace occ
