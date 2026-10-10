// ---------------------------------------------------------------------------
// WindowsPrinterBackend - printing through a normal Windows print queue.
//
// Used for proof sheets / draft output and as the fallback when no Entrust /
// Datacard card printer is installed. It does not interrogate the spooler's
// Bidi interface, so card printer ink/ribbon state is never claimed here; the
// status shown comes straight from the queue's PRINTER_INFO_2 data.
// ---------------------------------------------------------------------------
#include "printing/WindowsPrinterBackend.h"

#include "utils/GdiPrintSurface.h"

#include <windows.h>
#include <winspool.h>

#include <QByteArray>
#include <QDateTime>
#include <QPageLayout>
#include <QPainter>
#include <QPrinter>
#include <QStringList>

#include <string>

namespace occ {

// Defined in PrinterTypes.cpp; declared here so the diagnostics report can reuse
// the single state-name mapping.
QString printerStateToString(PrinterState state);

namespace {

GdiPrintSurface::PrinterInfo lookupPrinter(const QString &name, bool *found)
{
    if (found)
        *found = false;
    for (const GdiPrintSurface::PrinterInfo &info : GdiPrintSurface::enumeratePrinters()) {
        if (info.name == name) {
            if (found)
                *found = true;
            return info;
        }
    }
    return GdiPrintSurface::PrinterInfo{};
}

PrinterConnection connectionFromPort(const QString &port)
{
    if (port.startsWith(QLatin1String("USB"), Qt::CaseInsensitive))
        return PrinterConnection::Usb;
    if (port.startsWith(QLatin1String("COM"), Qt::CaseInsensitive))
        return PrinterConnection::Serial;
    if (port.startsWith(QLatin1String("LPT"), Qt::CaseInsensitive))
        return PrinterConnection::Parallel;
    if (port.startsWith(QLatin1String("IP_"), Qt::CaseInsensitive)
        || port.contains(QLatin1Char('.')) || port.startsWith(QLatin1String("\\\\")))
        return PrinterConnection::Network;
    if (port.startsWith(QLatin1String("PORTPROMPT"), Qt::CaseInsensitive)
        || port.startsWith(QLatin1String("FILE"), Qt::CaseInsensitive))
        return PrinterConnection::Virtual;
    return PrinterConnection::Unknown;
}

// Draws an image into the whole printable area, preserving its aspect ratio.
void drawPixmapInto(QPainter &painter, const QImage &image, const QRectF &target)
{
    if (image.isNull() || target.isEmpty())
        return;

    const QImage scaled =
        image.scaled(target.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QRectF destination(QPointF(target.left() + (target.width() - scaled.width()) / 2.0,
                                     target.top() + (target.height() - scaled.height()) / 2.0),
                             QSizeF(scaled.size()));
    painter.drawImage(destination, scaled);
}

} // namespace

WindowsPrinterBackend::WindowsPrinterBackend(const QString &printerName)
    : m_name(printerName)
{
}

WindowsPrinterBackend::~WindowsPrinterBackend() = default;

PrinterIdentity WindowsPrinterBackend::identity()
{
    PrinterIdentity id;
    id.name = m_name;
    id.backend = PrinterBackendKind::Windows;

    bool found = false;
    const GdiPrintSurface::PrinterInfo info = lookupPrinter(m_name, &found);
    if (found) {
        id.driverName = info.driverName;
        id.port = info.port;
        id.connection = connectionFromPort(info.port);
        if (info.isNetwork)
            id.connection = PrinterConnection::Network;
    }
    return id;
}

bool WindowsPrinterBackend::connect()
{
    m_lastError.clear();
    m_lastTechnical.clear();

    if (m_name.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("No printer was selected.");
        m_lastTechnical = QStringLiteral("WindowsPrinterBackend::connect(): empty name");
        return false;
    }

    std::wstring wname = m_name.toStdWString();
    HANDLE handle = nullptr;
    if (!::OpenPrinterW(wname.data(), &handle, nullptr)) {
        const DWORD code = ::GetLastError();
        m_lastError = QStringLiteral("The printer \"%1\" could not be opened.").arg(m_name);
        m_lastTechnical = QStringLiteral("OpenPrinter(Win32 %1)").arg(code);
        return false;
    }
    ::ClosePrinter(handle);

    m_connected = true;
    return true;
}

void WindowsPrinterBackend::disconnect()
{
    m_connected = false;
}

// --------------------------------------------------------------------------
// status - from the queue's PRINTER_INFO_2 (status mask and queued jobs).
// --------------------------------------------------------------------------
PrinterStatus WindowsPrinterBackend::status()
{
    PrinterStatus st;
    st.updatedAt = QDateTime::currentDateTime();
    st.iBidiAvailable = false;

    std::wstring wname = m_name.toStdWString();
    HANDLE handle = nullptr;
    if (!::OpenPrinterW(wname.data(), &handle, nullptr)) {
        const DWORD code = ::GetLastError();
        st.state = PrinterState::NotAvailable;
        st.detail = QStringLiteral("The printer \"%1\" could not be queried.").arg(m_name);
        m_lastError = st.detail;
        m_lastTechnical = QStringLiteral("OpenPrinter(Win32 %1)").arg(code);
        return st;
    }

    DWORD needed = 0;
    ::GetPrinterW(handle, 2, nullptr, 0, &needed);

    if (needed > 0) {
        QByteArray buffer(int(needed), 0);
        DWORD returned = 0;
        if (::GetPrinterW(handle, 2, reinterpret_cast<LPBYTE>(buffer.data()), needed,
                          &returned)) {
            const PRINTER_INFO_2W *info =
                reinterpret_cast<const PRINTER_INFO_2W *>(buffer.constData());

            st.stateText = GdiPrintSurface::describePrinterStatus(info->Status)
                               .join(QStringLiteral(", "));

            const DWORD status = info->Status;
            const DWORD hardErrors = PRINTER_STATUS_ERROR | PRINTER_STATUS_PAPER_JAM
                                     | PRINTER_STATUS_PAPER_OUT | PRINTER_STATUS_NO_TONER
                                     | PRINTER_STATUS_OUT_OF_MEMORY
                                     | PRINTER_STATUS_PAPER_PROBLEM;
            const DWORD warnings = PRINTER_STATUS_PAUSED | PRINTER_STATUS_DOOR_OPEN
                                   | PRINTER_STATUS_USER_INTERVENTION
                                   | PRINTER_STATUS_TONER_LOW
                                   | PRINTER_STATUS_OUTPUT_BIN_FULL;
            const DWORD active = PRINTER_STATUS_PRINTING | PRINTER_STATUS_PROCESSING
                                 | PRINTER_STATUS_BUSY | PRINTER_STATUS_IO_ACTIVE
                                 | PRINTER_STATUS_WARMING_UP
                                 | PRINTER_STATUS_INITIALIZING;

            if (status & hardErrors) {
                st.state = PrinterState::Error;
                st.detail = QStringLiteral("The printer reports: %1.").arg(st.stateText);
            } else if (status & PRINTER_STATUS_OFFLINE) {
                st.state = PrinterState::Offline;
                st.detail = QStringLiteral("The printer is offline.");
            } else if (status & warnings) {
                st.state = PrinterState::Warning;
                st.detail = QStringLiteral("The printer needs attention: %1.")
                                .arg(st.stateText);
            } else if (status & active) {
                st.state = (status & PRINTER_STATUS_PRINTING) ? PrinterState::Printing
                                                              : PrinterState::Busy;
                st.detail = QStringLiteral("The printer is working.");
            } else if (info->cJobs > 0) {
                st.state = PrinterState::Busy;
                st.detail =
                    QStringLiteral("%1 document(s) waiting to print.").arg(info->cJobs);
            } else {
                st.state = PrinterState::Ready;
                st.detail = QStringLiteral(
                    "The queue is ready. (This backend does not read card printer "
                    "supplies.)");
            }
        } else {
            st.state = PrinterState::NotAvailable;
            st.detail = QStringLiteral("The printer's status could not be read.");
        }
    } else {
        st.state = PrinterState::NotAvailable;
        st.detail = QStringLiteral("The printer did not report any status.");
    }

    ::ClosePrinter(handle);
    return st;
}

// --------------------------------------------------------------------------
// print - draws the card side(s) through Qt's print support.
// --------------------------------------------------------------------------
bool WindowsPrinterBackend::print(const PrintJob &job)
{
    m_lastError.clear();
    m_lastTechnical.clear();
    m_lastPrintSucceeded = false;

    if (!m_connected && !connect())
        return false;

    if (!job.frontEnabled && !job.backEnabled) {
        m_lastError =
            QStringLiteral("There is nothing to print: both card sides are disabled.");
        return false;
    }
    if (job.frontEnabled && job.frontImage.isNull()) {
        m_lastError = QStringLiteral("The front of the card has no image.");
        return false;
    }
    if (job.backEnabled && job.backImage.isNull()) {
        m_lastError = QStringLiteral("The back of the card has no image.");
        return false;
    }

    QPrinter printer(QPrinter::HighResolution);
    printer.setPrinterName(m_name);
    printer.setOutputFormat(QPrinter::NativeFormat);
    // Match the page to the card: a portrait card must not be forced landscape.
    printer.setPageOrientation(job.landscape ? QPageLayout::Landscape
                                             : QPageLayout::Portrait);
    printer.setDuplex(job.duplex ? QPrinter::DuplexShortSide : QPrinter::DuplexNone);
    printer.setFullPage(true);
    if (!job.documentName.isEmpty())
        printer.setDocName(job.documentName);

    if (job.progress)
        job.progress(0, JobState::Active, QStringLiteral("Sending to %1").arg(m_name));

    QPainter painter;
    if (!painter.begin(&printer)) {
        m_lastError = QStringLiteral("The document could not be sent to \"%1\".").arg(m_name);
        m_lastTechnical = QStringLiteral("QPainter::begin(QPrinter) failed");
        return false;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    drawPixmapInto(painter, job.frontImage, printer.pageRect(QPrinter::DevicePixel));

    if (job.backEnabled && job.duplex) {
        if (!printer.newPage()) {
            painter.end();
            m_lastError = QStringLiteral("The printer would not accept the back of the card.");
            m_lastTechnical = QStringLiteral("QPrinter::newPage() returned false");
            return false;
        }
        drawPixmapInto(painter, job.backImage, printer.pageRect(QPrinter::DevicePixel));
    }

    painter.end();

    if (printer.printerState() == QPrinter::Error
        || printer.printerState() == QPrinter::Aborted) {
        m_lastError = QStringLiteral("The print job to \"%1\" failed.").arg(m_name);
        m_lastTechnical = QStringLiteral("QPrinter::printerState() is Error/Aborted");
        return false;
    }

    m_lastPrintSucceeded = true;
    if (job.progress)
        job.progress(0, JobState::Unknown,
                     QStringLiteral("Submitted to %1; physical printing was not verified").arg(m_name));
    return true;
}

// --------------------------------------------------------------------------
// cancelJob - cancels a queued document by spooler job id.
// --------------------------------------------------------------------------
bool WindowsPrinterBackend::cancelJob(int printerJobId)
{
    m_lastError.clear();
    m_lastTechnical.clear();

    if (printerJobId <= 0) {
        m_lastError = QStringLiteral("There is no print job to cancel.");
        return false;
    }

    std::wstring wname = m_name.toStdWString();
    HANDLE handle = nullptr;
    if (!::OpenPrinterW(wname.data(), &handle, nullptr)) {
        const DWORD code = ::GetLastError();
        m_lastError = QStringLiteral("The printer \"%1\" could not be opened.").arg(m_name);
        m_lastTechnical = QStringLiteral("OpenPrinter(Win32 %1)").arg(code);
        return false;
    }

    const BOOL cancelled =
        ::SetJobW(handle, DWORD(printerJobId), 0, nullptr, JOB_CONTROL_CANCEL);
    const DWORD code = ::GetLastError();
    ::ClosePrinter(handle);

    if (!cancelled) {
        m_lastError =
            QStringLiteral("The print job %1 could not be cancelled.").arg(printerJobId);
        m_lastTechnical = QStringLiteral("SetJob(JOB_CONTROL_CANCEL, Win32 %1)").arg(code);
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// diagnosticsReport
// --------------------------------------------------------------------------
QString WindowsPrinterBackend::diagnosticsReport()
{
    const QString notAvailable = QStringLiteral("Not available");
    const PrinterStatus st = status();

    QStringList lines;
    lines << QStringLiteral("Windows printer driver backend");
    lines << QStringLiteral("Printer            : %1")
                 .arg(m_name.isEmpty() ? notAvailable : m_name);
    lines << QStringLiteral("Connected          : %1")
                 .arg(m_connected ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QStringLiteral("Kind               : Ordinary Windows print queue (not an XPS "
                            "Card Printer)");

    bool found = false;
    const GdiPrintSurface::PrinterInfo info = lookupPrinter(m_name, &found);
    if (found) {
        lines << QStringLiteral("Driver             : %1")
                     .arg(info.driverName.isEmpty() ? notAvailable : info.driverName);
        lines << QStringLiteral("Port               : %1")
                     .arg(info.port.isEmpty() ? notAvailable : info.port);
        lines << QStringLiteral("Server             : %1")
                     .arg(info.serverName.isEmpty() ? notAvailable : info.serverName);
        lines << QStringLiteral("Network printer    : %1")
                     .arg(info.isNetwork ? QStringLiteral("Yes") : QStringLiteral("No"));
        lines << QStringLiteral("Attributes         : %1")
                     .arg(GdiPrintSurface::describePrinterAttributes(info.attributes));
    } else {
        lines << QStringLiteral("Driver             : %1").arg(notAvailable);
        lines << QStringLiteral("Port               : %1").arg(notAvailable);
    }

    lines << QStringLiteral("Queue state        : %1")
                 .arg(st.stateText.isEmpty() ? notAvailable : st.stateText);
    lines << QStringLiteral("Interpreted state  : %1").arg(printerStateToString(st.state));
    lines << QStringLiteral("Detail             : %1")
                 .arg(st.detail.isEmpty() ? notAvailable : st.detail);
    if (!m_lastError.isEmpty())
        lines << QStringLiteral("Last error         : %1").arg(m_lastError);
    if (!m_lastTechnical.isEmpty())
        lines << QStringLiteral("Technical detail   : %1").arg(m_lastTechnical);
    return lines.join(QLatin1Char('\n'));
}

} // namespace occ
