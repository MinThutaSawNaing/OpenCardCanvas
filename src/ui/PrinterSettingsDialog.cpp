#include "ui/PrinterSettingsDialog.h"

#include "core/CardGeometry.h"
#include "printing/ICardPrinter.h"
#include "printing/PrinterManager.h"
#include "ui/IconFactory.h"
#include "utils/AppPaths.h"
#include "utils/BidiClient.h"
#include "utils/Logger.h"
#include "utils/Settings.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace occ {

namespace {

// The simulator is not a printer. Spelling that out in the one place a user
// picks a device is deliberate: a card that was rendered to a PNG must never be
// mistaken for a card that came out of a printer.
const char *kSimulatorLabel = QT_TRANSLATE_NOOP(
    "occ::PrinterSettingsDialog",
    "Simulator (no hardware - renders a file, does not print)");
const char *kSimulatorName = QT_TRANSLATE_NOOP(
    "occ::PrinterSettingsDialog",
    "OpenCardCanvas Simulator");

QString describeConnection(PrinterConnection connection)
{
    switch (connection) {
    case PrinterConnection::Usb:      return QStringLiteral("USB");
    case PrinterConnection::Network:  return QStringLiteral("Network");
    case PrinterConnection::Serial:   return QStringLiteral("Serial");
    case PrinterConnection::Parallel: return QStringLiteral("Parallel");
    case PrinterConnection::Virtual:  return QStringLiteral("Virtual");
    case PrinterConnection::Unknown:  break;
    }
    return QString();
}

QString describeState(PrinterState state)
{
    switch (state) {
    case PrinterState::Ready:    return QObject::tr("Ready");
    case PrinterState::Printing: return QObject::tr("Printing");
    case PrinterState::Busy:     return QObject::tr("Busy");
    case PrinterState::Offline:  return QObject::tr("Offline");
    case PrinterState::Warning:  return QObject::tr("Warning");
    case PrinterState::Error:    return QObject::tr("Error");
    case PrinterState::NotAvailable: return QObject::tr("Not available");
    case PrinterState::NotQueried:   break;
    }
    return QObject::tr("Not queried");
}

} // namespace

PrinterSettingsDialog::PrinterSettingsDialog(PrinterManager *manager, Mode mode, QWidget *parent)
    : QDialog(parent),
      m_manager(manager),
      m_mode(mode)
{
    setWindowTitle(mode == Mode::Diagnostics ? tr("Printer diagnostics") : tr("Printer settings"));
    setWindowIcon(IconFactory::icon(QStringLiteral("logo")));
    resize(940, 620);
    buildUi();
    refreshPrinters();

    if (mode == Mode::Diagnostics) {
        if (m_manager) {
            if (CardPrinterPtr printer = m_manager->printer(selectedPrinter())) {
                appendDiagnostics(tr("Diagnostics report for %1").arg(printer->name()),
                                  printer->diagnosticsReport());
            }
        }
        showSdkInformation();
    }
}

void PrinterSettingsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *splitter = new QSplitter(Qt::Vertical, this);

    m_table = new QTableWidget(splitter);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(QStringList{
        tr("Name"), tr("Manufacturer"), tr("Model"), tr("Driver"), tr("Port"),
        tr("Connection"), tr("Backend"), tr("Status") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 8; ++column)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    splitter->addWidget(m_table);
    connect(m_table, &QTableWidget::itemSelectionChanged,
            this, &PrinterSettingsDialog::updateSelection);

    auto *diagnosticsGroup = new QGroupBox(tr("Diagnostics"), splitter);
    auto *diagnosticsLayout = new QVBoxLayout(diagnosticsGroup);
    m_diagnostics = new QPlainTextEdit(diagnosticsGroup);
    m_diagnostics->setReadOnly(true);
    m_diagnostics->setLineWrapMode(QPlainTextEdit::NoWrap);
    diagnosticsLayout->addWidget(m_diagnostics);
    splitter->addWidget(diagnosticsGroup);
    // The diagnostics pane starts folded in Settings mode so the printer list
    // gets the space, and open in Diagnostics mode where it is the point.
    splitter->setStretchFactor(0, m_mode == Mode::Settings ? 3 : 1);
    splitter->setStretchFactor(1, m_mode == Mode::Settings ? 1 : 3);
    layout->addWidget(splitter, 1);

    auto *buttons = new QHBoxLayout();
    m_refresh = new QPushButton(IconFactory::icon(QStringLiteral("refresh")), tr("Refresh"),
                                this);
    m_testConnection = new QPushButton(tr("Test connection"), this);
    m_testPage = new QPushButton(IconFactory::icon(QStringLiteral("print")),
                                 tr("Printer test"), this);
    m_driverInfo = new QPushButton(tr("Driver information"), this);
    m_sdkInfo = new QPushButton(tr("SDK information"), this);
    buttons->addWidget(m_refresh);
    buttons->addWidget(m_testConnection);
    buttons->addWidget(m_testPage);
    buttons->addWidget(m_driverInfo);
    buttons->addWidget(m_sdkInfo);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto *bottom = new QHBoxLayout();
    auto *hint = new QLabel(tr("Selecting a printer makes it the default for printing."), this);
    hint->setWordWrap(true);
    bottom->addWidget(hint, 1);
    auto *close = new QPushButton(tr("Close"), this);
    bottom->addWidget(close);
    layout->addLayout(bottom);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_refresh, &QPushButton::clicked, this, &PrinterSettingsDialog::refreshPrinters);
    connect(m_testConnection, &QPushButton::clicked, this, &PrinterSettingsDialog::testConnection);
    connect(m_testPage, &QPushButton::clicked, this, &PrinterSettingsDialog::runPrinterTest);
    connect(m_driverInfo, &QPushButton::clicked, this,
            &PrinterSettingsDialog::showDriverInformation);
    connect(m_sdkInfo, &QPushButton::clicked, this, &PrinterSettingsDialog::showSdkInformation);
}

QString PrinterSettingsDialog::backendLabel(PrinterBackendKind kind) const
{
    switch (kind) {
    case PrinterBackendKind::EntrustXps:
        return tr("Entrust XPS Card Printer");
    case PrinterBackendKind::Windows:
        return tr("Windows printer driver");
    case PrinterBackendKind::Simulator:
        // Never just "Simulator": the label has to carry the consequence.
        return tr("Simulator (no hardware)");
    }
    return tr("Unknown");
}

void PrinterSettingsDialog::refreshPrinters()
{
    if (!m_manager)
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<PrinterIdentity> printers = m_manager->refresh();
    QStringList printerNames;
    printerNames.reserve(printers.size());
    for (const PrinterIdentity &identity : printers)
        printerNames << identity.name;
    const QVector<PrinterStatus> statuses = m_manager->statusesFor(printerNames);
    QApplication::restoreOverrideCursor();

    int previousRow = m_table->currentRow();
    const QString previousName = selectedPrinter();

    m_table->setRowCount(0);
    for (const PrinterIdentity &identity : printers) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(identity.name));
        m_table->setItem(row, 1, new QTableWidgetItem(identity.manufacturer.isEmpty()
                                                          ? tr("Not available")
                                                          : identity.manufacturer));
        m_table->setItem(row, 2, new QTableWidgetItem(identity.model.isEmpty()
                                                          ? tr("Not available")
                                                          : identity.model));
        m_table->setItem(row, 3, new QTableWidgetItem(identity.driverName.isEmpty()
                                                          ? tr("Not available")
                                                          : identity.driverName));
        m_table->setItem(row, 4, new QTableWidgetItem(identity.port.isEmpty()
                                                          ? tr("Not available")
                                                          : identity.port));
        const QString connection = describeConnection(identity.connection);
        m_table->setItem(row, 5, new QTableWidgetItem(connection.isEmpty()
                                                          ? tr("Not available")
                                                          : connection));
        m_table->setItem(row, 6, new QTableWidgetItem(backendLabel(identity.backend)));
        const QString status = row < statuses.size() ? describeState(statuses.at(row).state)
                                                     : tr("Not queried");
        m_table->setItem(row, 7, new QTableWidgetItem(status));
        m_table->item(row, 0)->setData(Qt::UserRole, identity.name);
    }

    // The simulator is always available and always last, so it is easy to find
    // and impossible to confuse with a real queue.
    const int simulatorRow = m_table->rowCount();
    m_table->insertRow(simulatorRow);
    m_table->setItem(simulatorRow, 0, new QTableWidgetItem(tr(kSimulatorLabel)));
    for (int column = 1; column < 8; ++column)
        m_table->setItem(simulatorRow, column, new QTableWidgetItem(QString()));
    m_table->item(simulatorRow, 6)->setText(tr("Simulator (no hardware)"));
    m_table->item(simulatorRow, 7)->setText(tr("Ready"));
    m_table->item(simulatorRow, 0)->setData(Qt::UserRole, QString());

    // Restore the selection: the previously selected printer, else the
    // configured default, else the first real printer.
    QString wanted = previousName;
    if (wanted.isEmpty())
        wanted = AppSettings::instance().defaultPrinter();
    bool selected = false;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->data(Qt::UserRole).toString() == wanted) {
            m_table->selectRow(row);
            selected = true;
            break;
        }
    }
    if (!selected && m_table->rowCount() > 0)
        m_table->selectRow(qBound(0, previousRow, m_table->rowCount() - 1));

    updateSelection();
}

QString PrinterSettingsDialog::selectedPrinter() const
{
    if (!m_table || m_table->currentRow() < 0)
        return QString();
    QTableWidgetItem *item = m_table->item(m_table->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void PrinterSettingsDialog::selectPrinter(const QString &printerName)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->data(Qt::UserRole).toString() == printerName) {
            m_table->selectRow(row);
            return;
        }
    }
}

void PrinterSettingsDialog::updateSelection()
{
    if (m_table->currentRow() < 0)
        return;

    // Selecting a printer here is what makes it the default: the dialog is the
    // only place a user chooses a device, so it must not be a decision that is
    // thrown away when the window closes.
    AppSettings::instance().setDefaultPrinter(selectedPrinter());
}

void PrinterSettingsDialog::appendDiagnostics(const QString &title, const QString &text)
{
    if (!m_diagnostics)
        return;
    if (!m_diagnostics->toPlainText().isEmpty())
        m_diagnostics->appendPlainText(QString(60, QLatin1Char('-')));
    m_diagnostics->appendPlainText(title);
    m_diagnostics->appendPlainText(text.isEmpty() ? tr("(no information was reported)")
                                                  : text);
}

void PrinterSettingsDialog::testConnection()
{
    if (!m_manager)
        return;

    const QString name = selectedPrinter();
    CardPrinterPtr printer = name.isEmpty() ? m_manager->simulator() : m_manager->printer(name);
    if (!printer) {
        appendDiagnostics(tr("Test connection"),
                          tr("The printer \"%1\" could not be opened: %2")
                              .arg(name.isEmpty() ? tr("simulator") : name,
                                   m_manager->lastError()));
        return;
    }

    const bool connected = printer->connect();
    const PrinterStatus status = printer->status();

    QStringList lines;
    lines << tr("Printer: %1").arg(printer->name());
    lines << tr("Backend: %1").arg(backendLabel(printer->backend()));
    lines << tr("Connected: %1").arg(connected ? tr("yes") : tr("no"));
    if (!connected)
        lines << tr("Reason: %1").arg(printer->lastError());
    lines << tr("State: %1%2").arg(describeState(status.state),
                                   status.stateText.isEmpty()
                                       ? QString()
                                       : QStringLiteral(" (%1)").arg(status.stateText));
    if (!status.detail.isEmpty())
        lines << tr("Detail: %1").arg(status.detail);
    lines << tr("Bidirectional interface available: %1")
                 .arg(status.iBidiAvailable ? tr("yes") : tr("no"));
    if (!status.sdkVersion.isEmpty())
        lines << tr("Driver SDK version: %1").arg(status.sdkVersion);
    lines << tr("This backend can put ink on a card: %1")
                 .arg(printer->isRealHardware() ? tr("yes") : tr("no"));
    appendDiagnostics(tr("Test connection"), lines.join(QLatin1Char('\n')));
}

void PrinterSettingsDialog::showDriverInformation()
{
    if (!m_manager)
        return;

    const QString name = selectedPrinter();
    CardPrinterPtr printer = name.isEmpty() ? m_manager->simulator() : m_manager->printer(name);
    if (!printer) {
        appendDiagnostics(tr("Driver information"),
                          tr("No printer information is available: %1")
                              .arg(m_manager->lastError()));
        return;
    }

    const PrinterIdentity identity = printer->identity();
    QStringList lines;
    lines << tr("Name: %1").arg(identity.name);
    lines << tr("Manufacturer: %1").arg(identity.manufacturer.isEmpty()
                                            ? tr("Not available")
                                            : identity.manufacturer);
    lines << tr("Model: %1").arg(identity.model.isEmpty() ? tr("Not available")
                                                          : identity.model);
    lines << tr("Driver: %1").arg(identity.driverName.isEmpty() ? tr("Not available")
                                                                : identity.driverName);
    lines << tr("Driver version: %1").arg(identity.driverVersion.isEmpty()
                                              ? tr("Not available")
                                              : identity.driverVersion);
    lines << tr("Port: %1").arg(identity.port.isEmpty() ? tr("Not available") : identity.port);
    lines << tr("Serial number: %1").arg(identity.serialNumber.isEmpty()
                                             ? tr("Not available")
                                             : identity.serialNumber);
    lines << tr("Firmware: %1").arg(identity.firmwareVersion.isEmpty()
                                        ? tr("Not available")
                                        : identity.firmwareVersion);
    lines << tr("Colour resolution: %1").arg(identity.colorResolution.isEmpty()
                                                 ? tr("Not available")
                                                 : identity.colorResolution);
    lines << tr("Monochrome resolution: %1").arg(identity.monochromeResolution.isEmpty()
                                                     ? tr("Not available")
                                                     : identity.monochromeResolution);

    const auto capability = [](const char *label, bool known, bool supported) {
        if (!known)
            return QObject::tr("%1: the device does not report this").arg(QLatin1String(label));
        return QObject::tr("%1: %2").arg(QLatin1String(label),
                                        supported ? QObject::tr("yes") : QObject::tr("no"));
    };
    lines << capability(QT_TRANSLATE_NOOP("occ::PrinterSettingsDialog", "Duplex"), identity.duplexKnown,
                        identity.duplexSupported);
    lines << capability(QT_TRANSLATE_NOOP("occ::PrinterSettingsDialog", "Laminator"),
                        identity.laminatorKnown, identity.hasLaminator);
    lines << capability(QT_TRANSLATE_NOOP("occ::PrinterSettingsDialog", "Magnetic stripe"),
                        identity.magstripeKnown, identity.hasMagstripe);
    lines << capability(QT_TRANSLATE_NOOP("occ::PrinterSettingsDialog", "Smart card"),
                        identity.smartcardKnown, identity.hasSmartcard);
    lines << capability(QT_TRANSLATE_NOOP("occ::PrinterSettingsDialog", "Embosser"),
                        identity.embosserKnown, identity.hasEmbosser);
    lines << capability(QT_TRANSLATE_NOOP("occ::PrinterSettingsDialog", "Barcode reader"),
                        identity.barcodeReaderKnown, identity.hasBarcodeReader);

    appendDiagnostics(tr("Driver information"), lines.join(QLatin1Char('\n')));
}

void PrinterSettingsDialog::showSdkInformation()
{
    if (!m_manager)
        return;

    const QString name = selectedPrinter();
    QStringList lines;
    lines << tr("Application: OpenCardCanvas");
    lines << tr("BidiSpl schemas in use: %1")
                 .arg(BidiClient::schemaPrinterOptions());

    if (name.isEmpty()) {
        lines << tr("The simulator does not use a driver, so there is no SDK to query.");
        appendDiagnostics(tr("SDK information"), lines.join(QLatin1Char('\n')));
        return;
    }

    QString sdkVersion;
    const bool isCardPrinter = PrinterManager::isEntrustCardPrinter(name, &sdkVersion);
    lines << tr("Driver identified as an XPS Card Printer: %1")
                 .arg(isCardPrinter ? tr("yes") : tr("no"));
    lines << tr("Driver reported SDK version: %1")
                 .arg(sdkVersion.isEmpty() ? tr("not reported") : sdkVersion);

    // A real round trip: opening the BidiSpl channel and asking for the SDK
    // version is what proves the driver is present and answering.
    BidiClient bidi;
    QString error;
    if (bidi.open(name, &error)) {
        QString xml;
        QString queryError;
        if (bidi.query(BidiClient::schemaSdkVersion(), &xml, &queryError)) {
            lines << tr("%1: %2").arg(BidiClient::schemaSdkVersion(), xml.simplified());
        } else {
            lines << tr("%1: %2").arg(BidiClient::schemaSdkVersion(), queryError);
        }
        QString optionsXml;
        if (bidi.query(BidiClient::schemaPrinterOptions(), &optionsXml, &queryError)) {
            lines << tr("%1: %2").arg(BidiClient::schemaPrinterOptions(),
                                      optionsXml.simplified());
        } else {
            lines << tr("%1: %2").arg(BidiClient::schemaPrinterOptions(), queryError);
        }
        bidi.close();
    } else {
        lines << tr("The print spooler's BidiSpl interface is not available for this queue: %1")
                     .arg(error);
        lines << tr("Without it, status, supplies and card options cannot be read. Printing "
                    "still goes through the driver, but OpenCardCanvas cannot report what the "
                    "device is doing.");
    }

    appendDiagnostics(tr("SDK information"), lines.join(QLatin1Char('\n')));
}

void PrinterSettingsDialog::runPrinterTest()
{
    if (!m_manager)
        return;

    const QString name = selectedPrinter();
    const auto answer = QMessageBox::question(
        this, tr("Printer test"),
        tr("Send a printer test page to \"%1\"?\n\nThe test page is generated by "
           "OpenCardCanvas and is clearly marked as a test page. A card printer will "
           "consume one card if the job is accepted.")
            .arg(name.isEmpty() ? tr(kSimulatorLabel) : name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    const TestPageResult result = printTestPage(*m_manager, name, 300);
    QStringList lines;
    lines << tr("Printer: %1").arg(name.isEmpty() ? tr(kSimulatorLabel) : name);
    if (result.ok && result.simulated) {
        lines << tr("The test page was SIMULATED. No card was printed and no hardware was "
                    "used. The image was written to the simulator's output folder.");
    } else if (result.ok) {
        lines << tr("The printer accepted the test page.");
    } else {
        lines << tr("The test page was not printed: %1").arg(result.error);
    }
    if (!result.detail.isEmpty())
        lines << tr("Technical detail: %1").arg(result.detail);
    appendDiagnostics(tr("Printer test"), lines.join(QLatin1Char('\n')));

    if (result.ok && result.simulated) {
        QMessageBox::information(this, tr("Printer test"),
                                 tr("The card was simulated, not printed."));
    } else if (!result.ok) {
        QMessageBox::warning(this, tr("Printer test"),
                             tr("The test page was not printed.\n\n%1").arg(result.error));
    }
}

QImage PrinterSettingsDialog::buildTestPage(int dpi)
{
    // A standard ID-1 card, so the sheet is dimensionally correct whatever the
    // printer's own resolution is.
    CardGeometry geometry = CardGeometry::defaultGeometry();
    geometry.setRenderDpi(qBound(kMinDpi, dpi, kMaxDpi));

    const QSize size = geometry.pixelSize();
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    const int dotsPerMeter = qRound(geometry.renderDpi() / 25.4 * 1000.0);
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    image.fill(Qt::white);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    // One user-space unit is one millimetre from here on.
    painter.scale(double(size.width()) / geometry.widthMm(),
                  double(size.height()) / geometry.heightMm());

    const QRectF card = geometry.boundsMm();
    painter.setPen(QPen(Qt::black, 0.6));
    painter.drawRect(card.adjusted(0.4, 0.4, -0.4, -0.4));

    QFont title = painter.font();
    title.setPointSizeF(9.0);
    title.setBold(true);
    painter.setFont(title);
    painter.drawText(card.adjusted(2.0, 2.0, -2.0, -2.0), Qt::AlignTop | Qt::AlignLeft,
                     QCoreApplication::translate("occ::PrinterSettingsDialog",
                                                 "OpenCardCanvas printer test page"));

    QFont body = painter.font();
    body.setPointSizeF(5.5);
    body.setBold(false);
    painter.setFont(body);
    const QString lines = QCoreApplication::translate(
        "occ::PrinterSettingsDialog",
        "This is a generated test sheet, not a card design.\n"
        "If the colour blocks below are distinct and this text is sharp, the\n"
        "print path and the driver are working.");
    painter.drawText(QRectF(card.left() + 2.0, card.top() + 5.5,
                            card.width() - 4.0, card.height() - 8.0),
                     Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, lines);

    // Colour ramp: the fastest way to see a misaligned head or a swapped ribbon.
    const double rampTop = card.bottom() - 12.0;
    const double blockWidth = (card.width() - 4.0) / 7.0;
    const QColor ramp[] = { Qt::black, Qt::white, Qt::red, Qt::green, Qt::blue,
                            Qt::cyan, Qt::magenta };
    for (int i = 0; i < 7; ++i) {
        const QRectF block(card.left() + 2.0 + i * blockWidth, rampTop, blockWidth, 5.0);
        painter.fillRect(block, ramp[i]);
        painter.setPen(QPen(Qt::gray, 0.2));
        painter.drawRect(block);
    }

    // Registration bar: a real millimetre scale, so a stretched output is
    // measurable rather than merely visible.
    painter.setPen(QPen(Qt::black, 0.3));
    painter.drawLine(QPointF(card.left() + 2.0, rampTop + 6.5),
                     QPointF(card.right() - 2.0, rampTop + 6.5));
    for (int mm = 0; mm <= int(card.width() - 4.0); ++mm) {
        const double x = card.left() + 2.0 + mm;
        const double length = (mm % 10 == 0) ? 2.0 : ((mm % 5 == 0) ? 1.4 : 0.8);
        painter.drawLine(QPointF(x, rampTop + 6.5), QPointF(x, rampTop + 6.5 + length));
    }
    QFont tiny = painter.font();
    tiny.setPointSizeF(3.5);
    painter.setFont(tiny);
    painter.drawText(QRectF(card.left() + 2.0, rampTop + 9.0, card.width() - 4.0, 2.5),
                     Qt::AlignLeft | Qt::AlignTop,
                     QCoreApplication::translate("occ::PrinterSettingsDialog",
                                                 "10 mm scale, 1 mm subdivisions"));
    painter.end();
    return image;
}

PrinterSettingsDialog::TestPageResult PrinterSettingsDialog::printTestPage(
    PrinterManager &manager, const QString &printerName, int dpi)
{
    TestPageResult result;

    const bool useSimulator = printerName.isEmpty();
    CardPrinterPtr printer = useSimulator ? manager.simulator() : manager.printer(printerName);
    if (!printer) {
        result.error = QCoreApplication::translate(
                           "occ::PrinterSettingsDialog",
                           "The printer \"%1\" is not available. Check that it is installed "
                           "and that its driver is working.").arg(printerName);
        result.detail = manager.lastError();
        return result;
    }

    if (!printer->connect()) {
        result.error = QCoreApplication::translate(
                           "occ::PrinterSettingsDialog",
                           "The printer \"%1\" could not be opened: %2")
                           .arg(printer->name(), printer->lastError());
        return result;
    }

    PrintJob job;
    job.printerName = useSimulator ? QString() : printerName;
    job.documentName = QCoreApplication::translate("occ::PrinterSettingsDialog",
                                                  "OpenCardCanvas printer test page");
    job.copies = 1;
    job.frontEnabled = true;
    job.backEnabled = false;
    job.duplex = false;
    job.frontImage = buildTestPage(dpi);
    job.frontDpi = qBound(kMinDpi, dpi, kMaxDpi);
    job.simulatorOutputDirectory = AppPaths::defaultExportsDir() + QStringLiteral("/simulator");

    const bool accepted = printer->print(job);
    result.ok = accepted;
    // The flag is what stops a caller from ever claiming a card came out of a
    // machine that has no card in it.
    result.simulated = printer->backend() == PrinterBackendKind::Simulator;
    if (!accepted) {
        result.error = printer->lastError().isEmpty()
                           ? QCoreApplication::translate("occ::PrinterSettingsDialog",
                                                         "The printer rejected the test page.")
                           : printer->lastError();
        result.detail = printer->diagnosticsReport();
    }
    return result;
}

} // namespace occ
