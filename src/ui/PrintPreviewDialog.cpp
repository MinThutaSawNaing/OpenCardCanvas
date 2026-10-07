#include "ui/PrintPreviewDialog.h"

#include "core/CardDocument.h"
#include "printing/PrinterManager.h"
#include "rendering/CardRenderer.h"
#include "ui/IconFactory.h"
#include "ui/PrinterSettingsDialog.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace occ {

namespace {

// Print resolution for the preview. High enough that what the user judges is
// what the printer will produce, and one card is fast enough to do it while the
// dialog opens.
constexpr int kPreviewDpi = 600;

} // namespace

PrintPreviewDialog::PrintPreviewDialog(const CardDocument &document, const Request &request,
                                       PrinterManager *printerManager, QWidget *parent)
    : QDialog(parent),
      m_document(&document),
      m_printers(printerManager),
      m_request(request)
{
    setWindowTitle(tr("Print preview"));
    setWindowIcon(IconFactory::icon(QStringLiteral("preview")));
    resize(900, 640);
    buildUi();
    renderPreview();
    // Fitting needs the real viewport size, which only exists after the dialog
    // has been laid out - so it is deferred by one event loop turn.
    QTimer::singleShot(0, this, &PrintPreviewDialog::fitToWindow);
}

void PrintPreviewDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *split = new QHBoxLayout();
    split->setSpacing(10);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setBackgroundRole(QPalette::Dark);
    m_scroll->setFrameShape(QFrame::StyledPanel);

    auto *canvas = new QWidget(m_scroll);
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->setContentsMargins(12, 12, 12, 12);
    canvasLayout->setSpacing(10);

    m_frontLabel = new QLabel(canvas);
    m_frontLabel->setAlignment(Qt::AlignCenter);
    m_frontLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_frontSideLabel = new QLabel(tr("Front"), canvas);
    m_frontSideLabel->setAlignment(Qt::AlignCenter);
    canvasLayout->addWidget(m_frontLabel);
    canvasLayout->addWidget(m_frontSideLabel);

    m_backLabel = new QLabel(canvas);
    m_backLabel->setAlignment(Qt::AlignCenter);
    m_backLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_backSideLabel = new QLabel(tr("Back"), canvas);
    m_backSideLabel->setAlignment(Qt::AlignCenter);
    canvasLayout->addWidget(m_backLabel);
    canvasLayout->addWidget(m_backSideLabel);

    canvasLayout->addStretch(1);
    m_scroll->setWidget(canvas);
    split->addWidget(m_scroll, 3);

    auto *side = new QGroupBox(tr("Print settings"), this);
    auto *sideLayout = new QVBoxLayout(side);

    auto *copiesRow = new QHBoxLayout();
    copiesRow->addWidget(new QLabel(tr("Copies"), side));
    m_copies = new QSpinBox(side);
    m_copies->setRange(1, 1000);
    m_copies->setValue(qBound(1, m_request.copies, 1000));
    copiesRow->addWidget(m_copies);
    copiesRow->addStretch(1);
    sideLayout->addLayout(copiesRow);

    auto *zoomRow = new QHBoxLayout();
    zoomRow->addWidget(new QLabel(tr("Zoom"), side));
    m_zoom = new QSlider(Qt::Horizontal, side);
    m_zoom->setRange(10, 400);
    m_zoom->setValue(100);
    m_zoomLabel = new QLabel(tr("100 %"), side);
    auto *fit = new QPushButton(tr("Fit"), side);
    zoomRow->addWidget(m_zoom, 1);
    zoomRow->addWidget(m_zoomLabel);
    zoomRow->addWidget(fit);
    sideLayout->addLayout(zoomRow);
    connect(fit, &QPushButton::clicked, this, &PrintPreviewDialog::fitToWindow);
    connect(m_zoom, &QSlider::valueChanged, this, &PrintPreviewDialog::setZoom);

    sideLayout->addSpacing(6);
    m_summary = new QLabel(side);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sideLayout->addWidget(m_summary);
    sideLayout->addStretch(1);

    auto *settingsButton = new QPushButton(IconFactory::icon(QStringLiteral("printer")),
                                           tr("Printer settings..."), side);
    settingsButton->setToolTip(tr("Choose the printer and check the driver before printing."));
    sideLayout->addWidget(settingsButton);
    connect(settingsButton, &QPushButton::clicked, this, [this] {
        PrinterSettingsDialog dialog(m_printers, PrinterSettingsDialog::Mode::Settings, this);
        dialog.selectPrinter(m_request.printerName);
        dialog.exec();
        // The dialog writes the choice through AppSettings, so re-reading it is
        // what keeps the summary and the request in step with reality.
        m_request.printerName = AppSettings::instance().defaultPrinter();
        updateSummary();
    });

    split->addWidget(side, 2);
    layout->addLayout(split, 1);

    auto *buttons = new QHBoxLayout();
    auto *hint = new QLabel(tr("Rendered by the same code that produces the print output."), this);
    hint->setWordWrap(true);
    buttons->addWidget(hint, 1);
    auto *print = new QPushButton(IconFactory::icon(QStringLiteral("print")), tr("Print"), this);
    print->setDefault(true);
    auto *close = new QPushButton(tr("Close"), this);
    buttons->addWidget(print);
    buttons->addWidget(close);
    layout->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(print, &QPushButton::clicked, this, [this] {
        m_request.copies = m_copies->value();
        emit printConfirmed(m_request);
        accept();
    });
    connect(m_copies, &QSpinBox::valueChanged, this, [this](int value) {
        m_request.copies = value;
        updateSummary();
    });

    updateSummary();
}

QImage PrintPreviewDialog::renderSide(CardSideId side) const
{
    if (!m_document)
        return QImage();

    CardRenderer::Options options;
    options.dpi = kPreviewDpi;
    options.includeBleed = m_document->geometry().bleedMm() > 0.0;
    options.forPrinting = true;
    return CardRenderer::renderSide(*m_document, side, options);
}

void PrintPreviewDialog::renderPreview()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_front = renderSide(CardSideId::Front);
    m_back = renderSide(CardSideId::Back);
    QApplication::restoreOverrideCursor();
    updateImages();
}

void PrintPreviewDialog::updateImages()
{
    const auto scale = [this](const QImage &image) {
        if (image.isNull())
            return QPixmap();
        const double factor = m_zoom->value() / 100.0;
        return QPixmap::fromImage(image.scaled(qMax(1, int(image.width() * factor)),
                                               qMax(1, int(image.height() * factor)),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
    };

    const QPixmap front = scale(m_front);
    const QPixmap back = scale(m_back);
    m_frontLabel->setPixmap(front);
    m_backLabel->setPixmap(back);
    m_frontLabel->setVisible(!front.isNull());
    m_frontSideLabel->setVisible(!front.isNull() && m_request.frontEnabled);
    m_backLabel->setVisible(!back.isNull() && m_request.backEnabled);
    m_backSideLabel->setVisible(!back.isNull() && m_request.backEnabled);
    m_zoomLabel->setText(tr("%1 %").arg(m_zoom->value()));
}

void PrintPreviewDialog::setZoom(int percent)
{
    Q_UNUSED(percent);
    updateImages();
}

void PrintPreviewDialog::fitToWindow()
{
    if (m_front.isNull() || !m_scroll)
        return;

    const QSize viewport = m_scroll->viewport()->size();
    const int width = qMax(40, viewport.width() - 40);
    const int height = qMax(40, (viewport.height() - 100) / (m_request.backEnabled ? 2 : 1));
    const double factor = qMin(double(width) / m_front.width(),
                              double(height) / m_front.height());
    m_zoom->setValue(qBound(m_zoom->minimum(), int(factor * 100.0), 200));
}

void PrintPreviewDialog::updateSummary()
{
    if (!m_document || !m_summary)
        return;

    const CardGeometry &geometry = m_document->geometry();
    QStringList lines;
    lines << tr("Card: %1 x %2 mm (%3 x %4 pixels at %5 dpi)")
                 .arg(QString::number(geometry.widthMm(), 'f', 2),
                      QString::number(geometry.heightMm(), 'f', 2))
                 .arg(geometry.pixelSize().width())
                 .arg(geometry.pixelSize().height())
                 .arg(geometry.renderDpi());
    if (geometry.bleedMm() > 0.0)
        lines << tr("Bleed: %1 mm on every side")
                     .arg(QString::number(geometry.bleedMm(), 'f', 2));
    // The orientation decides how the card is laid on the page, so it belongs in
    // the summary: it is the difference between a card that prints upright and
    // one that prints sideways.
    lines << tr("Orientation: %1")
                 .arg(geometry.isLandscape() ? tr("landscape") : tr("portrait"));

    const bool simulator = m_request.printerName.isEmpty();
    lines << tr("Printer: %1")
                 .arg(simulator
                          ? tr("Simulator (no hardware - renders a file, does not print)")
                          : m_request.printerName);

    if (!simulator && m_printers) {
        if (CardPrinterPtr printer = m_printers->printer(m_request.printerName)) {
            const PrinterIdentity identity = printer->identity();
            lines << tr("Driver: %1")
                         .arg(identity.driverName.isEmpty() ? tr("not reported")
                                                            : identity.driverName);
            lines << tr("Backend: %1")
                         .arg(printer->isRealHardware()
                                  ? tr("real card printer")
                                  : tr("simulation only - no card will be printed"));
        } else {
            lines << tr("Driver: the printer could not be opened (%1)")
                         .arg(m_printers->lastError());
        }
    } else if (simulator) {
        lines << tr("Backend: simulation only - no card will be printed");
    }

    lines << tr("Copies: %1").arg(m_copies ? m_copies->value() : m_request.copies);
    lines << tr("Sides: %1").arg(
        m_request.frontEnabled && m_request.backEnabled
            ? tr("front and back")
            : (m_request.frontEnabled ? tr("front only")
                                      : (m_request.backEnabled ? tr("back only")
                                                               : tr("neither - nothing to "
                                                                    "print"))));
    lines << tr("Duplex: %1").arg(m_request.duplex ? tr("yes") : tr("no"));

    if (m_request.records.isEmpty())
        lines << tr("Records: printing the design as it is (no data set)");
    else
        lines << tr("Records: %1 selected (%2)")
                     .arg(m_request.records.size())
                     .arg(tr("personalised values are substituted at print time"));

    m_summary->setText(lines.join(QLatin1Char('\n')));
}

PrintPreviewDialog::Request PrintPreviewDialog::request() const
{
    Request result = m_request;
    if (m_copies)
        result.copies = m_copies->value();
    return result;
}

} // namespace occ
