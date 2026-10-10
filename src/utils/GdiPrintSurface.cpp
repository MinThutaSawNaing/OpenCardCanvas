// ---------------------------------------------------------------------------
// GdiPrintSurface - Win32 GDI printing for card images.
//
// This is the documented printing path of the XPS Card Printer Driver SDK
// (samples/cpp/print/print.cpp):
//
//   OpenPrinter -> DocumentProperties(DM_OUT_BUFFER) -> CreateDC("WINSPOOL")
//   -> StartDoc -> StartPage -> draw -> [ResetDC -> StartPage -> draw] -> EndDoc
//
// The card image is blitted with StretchDIBits (the application renders once and
// every device receives identical pixels); driver level options such as top coat
// blocking regions are sent as escape strings through TextOut, exactly as the
// SDK sample's WriteCustomTopcoatBlockingEscapes*() do.
//
// The page for a card side stays open until the next side resets the device or
// the document ends, so an escape sent with sendEscape() lands inside that same
// card side (that is how the SDK applies top coat to the side just drawn).
// ---------------------------------------------------------------------------
#include "utils/GdiPrintSurface.h"

#include <windows.h>
#include <winspool.h>
#include <wingdi.h>

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QPainter>
#include <QStringList>
#include <QVector>

#include <string>

namespace occ {

namespace {

// Human readable text for a Win32 status code.
QString win32Message(DWORD code)
{
    if (code == ERROR_SUCCESS)
        return QString();
    wchar_t *buffer = nullptr;
    const DWORD len = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString text;
    if (len && buffer)
        text = QString::fromWCharArray(buffer, int(len)).trimmed();
    if (buffer)
        ::LocalFree(buffer);
    return text;
}

QString win32Detail(const QString &what)
{
    const DWORD code = ::GetLastError();
    return QStringLiteral("%1 (Win32 %2: %3)")
        .arg(what)
        .arg(code)
        .arg(win32Message(code));
}

// The Windows job id returned by StartDoc is needed by endDocument(), but the
// frozen header has no member to hold it, so it is kept in a small, mutex
// guarded map keyed by the device context. Entries are always removed by
// endDocument()/abort()/the destructor.
QMutex g_jobMutex;
QHash<quintptr, unsigned long> g_jobIds;

void rememberJobId(HDC dc, unsigned long jobId)
{
    if (!dc)
        return;
    QMutexLocker locker(&g_jobMutex);
    g_jobIds.insert(quintptr(dc), jobId);
}

unsigned long takeJobId(HDC dc, HDC newDc = nullptr)
{
    QMutexLocker locker(&g_jobMutex);
    const unsigned long id = g_jobIds.take(quintptr(dc));
    if (newDc && id)
        g_jobIds.insert(quintptr(newDc), id);
    return id;
}

void forgetJobId(HDC dc)
{
    QMutexLocker locker(&g_jobMutex);
    g_jobIds.remove(quintptr(dc));
}

// A tightly packed, top-down 32bpp buffer for StretchDIBits. Translucent pixels
// are composited onto white so a card side with transparency prints as intended.
QImage flattenedForPrint(const QImage &source)
{
    if (source.hasAlphaChannel()) {
        QImage flattened(source.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, source);
        painter.end();
        return flattened;
    }
    return source.convertToFormat(QImage::Format_RGB32);
}

} // namespace

// --------------------------------------------------------------------------
// Construction / destruction
// --------------------------------------------------------------------------
GdiPrintSurface::GdiPrintSurface() = default;

GdiPrintSurface::~GdiPrintSurface()
{
    // Never leave a device context or printer handle open.
    if (m_documentOpen && m_dc)
        ::AbortDoc(m_dc);
    if (m_dc) {
        forgetJobId(m_dc);
        ::DeleteDC(m_dc);
        m_dc = nullptr;
    }
    if (m_printerHandle) {
        ::ClosePrinter(m_printerHandle);
        m_printerHandle = nullptr;
    }
    m_devmode = nullptr;
    m_documentOpen = false;
    m_pageOpen = false;
}

bool GdiPrintSurface::fail(const QString &userMessage, const QString &technical)
{
    m_lastError = userMessage;
    m_lastTechnical = technical;
    return false;
}

// --------------------------------------------------------------------------
// beginDocument
// --------------------------------------------------------------------------
bool GdiPrintSurface::beginDocument(const QString &printerName, const Options &options,
                                    QString *error, QString *technical)
{
    if (m_dc || m_printerHandle) {
        // Reuse of an unfinished surface: abort cleanly first.
        if (m_documentOpen && m_dc)
            ::AbortDoc(m_dc);
        if (m_dc) {
            forgetJobId(m_dc);
            ::DeleteDC(m_dc);
            m_dc = nullptr;
        }
        if (m_printerHandle) {
            ::ClosePrinter(m_printerHandle);
            m_printerHandle = nullptr;
        }
    }
    m_devmode = nullptr;
    m_documentOpen = false;
    m_pageOpen = false;
    m_lastError.clear();
    m_lastTechnical.clear();

    const QString name = printerName.trimmed();
    if (name.isEmpty()) {
        const bool ok = fail(QStringLiteral("No printer was selected."),
                             QStringLiteral("beginDocument: empty printer name"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    m_printerName = name;
    m_options = options;

    std::wstring wname = name.toStdWString();

    if (!::OpenPrinterW(wname.data(), &m_printerHandle, nullptr)) {
        const bool ok = fail(
            QStringLiteral("The printer \"%1\" could not be opened.").arg(name),
            win32Detail(QStringLiteral("OpenPrinter")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    // Buffer size, then the DEVMODE itself, mirroring the SDK sample.
    const LONG devmodeSize =
        ::DocumentPropertiesW(nullptr, m_printerHandle, wname.data(), nullptr, nullptr, 0);
    if (devmodeSize <= 0) {
        const bool ok = fail(
            QStringLiteral("The driver for \"%1\" did not return a valid configuration.")
                .arg(name),
            win32Detail(QStringLiteral("DocumentProperties(size)")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    m_devmodeBytes.resize(int(devmodeSize));
    m_devmode = reinterpret_cast<DEVMODE *>(m_devmodeBytes.data());

    LONG rc = ::DocumentPropertiesW(nullptr, m_printerHandle, wname.data(), m_devmode,
                                    nullptr, DM_OUT_BUFFER);
    if (rc < 0) {
        const bool ok = fail(
            QStringLiteral("The driver for \"%1\" could not provide its settings.")
                .arg(name),
            win32Detail(QStringLiteral("DocumentProperties(DM_OUT_BUFFER)")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    // Card printers print landscape cards; duplex is vertical (front/back).
    m_devmode->dmOrientation = options.landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    m_devmode->dmFields |= DM_ORIENTATION;
    m_devmode->dmDuplex = options.duplex ? DMDUP_VERTICAL : DMDUP_SIMPLEX;
    m_devmode->dmFields |= DM_DUPLEX;

    rc = ::DocumentPropertiesW(nullptr, m_printerHandle, wname.data(), m_devmode, m_devmode,
                               DM_IN_BUFFER | DM_OUT_BUFFER);
    if (rc != IDOK) {
        const bool ok = fail(
            QStringLiteral("The driver for \"%1\" rejected the page settings.").arg(name),
            win32Detail(QStringLiteral("DocumentProperties(DM_IN_BUFFER|DM_OUT_BUFFER)")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    m_dc = ::CreateDCW(L"WINSPOOL", wname.data(), nullptr, m_devmode);
    if (!m_dc) {
        const bool ok = fail(
            QStringLiteral("A printing context for \"%1\" could not be created.").arg(name),
            win32Detail(QStringLiteral("CreateDC")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    DOCINFOW docInfo = {};
    docInfo.cbSize = sizeof(docInfo);
    const QString docName =
        options.documentName.isEmpty() ? QStringLiteral("OpenCardCanvas card")
                                       : options.documentName;
    std::wstring wdocName = docName.toStdWString();
    docInfo.lpszDocName = wdocName.c_str();

    const int jobId = ::StartDocW(m_dc, &docInfo);
    if (jobId <= 0) {
        const bool ok = fail(
            QStringLiteral("The print job for \"%1\" could not be started.").arg(name),
            win32Detail(QStringLiteral("StartDoc")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    rememberJobId(m_dc, static_cast<unsigned long>(jobId));
    m_documentOpen = true;
    m_pageOpen = false;
    return true;
}

// --------------------------------------------------------------------------
// drawPage - starts the page (kept open so escapes land on the same side) and
// blits the card image over the full printable area.
// --------------------------------------------------------------------------
bool GdiPrintSurface::drawPage(const QImage &image, int imageDpi, QString *error,
                               QString *technical)
{
    Q_UNUSED(imageDpi);

    if (!m_dc || !m_documentOpen) {
        const bool ok = fail(QStringLiteral("No print job is open."),
                             QStringLiteral("drawPage: beginDocument() was not called"));
        if (error)
            *error = m_lastError;
        return ok;
    }
    if (image.isNull()) {
        const bool ok = fail(QStringLiteral("The card image is empty."),
                             QStringLiteral("drawPage: null QImage"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    if (!m_pageOpen) {
        if (::StartPage(m_dc) <= 0) {
            const bool ok = fail(
                QStringLiteral("The printer did not accept the page."),
                win32Detail(QStringLiteral("StartPage")));
            if (error)
                *error = m_lastError;
            if (technical)
                *technical = m_lastTechnical;
            return ok;
        }
        m_pageOpen = true;
    }

    const QImage dib = flattenedForPrint(image);
    if (dib.isNull()) {
        const bool ok = fail(QStringLiteral("The card image could not be prepared."),
                             QStringLiteral("drawPage: flattened image is null"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    const int srcW = dib.width();
    const int srcH = dib.height();
    if (srcW <= 0 || srcH <= 0) {
        const bool ok = fail(QStringLiteral("The card image has no size."),
                             QStringLiteral("drawPage: zero sized image"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    int destW = ::GetDeviceCaps(m_dc, HORZRES);
    int destH = ::GetDeviceCaps(m_dc, VERTRES);
    if (destW <= 0 || destH <= 0) {
        // Fall back to the image's own size at the requested resolution.
        destW = srcW;
        destH = srcH;
    }

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = srcW;
    bmi.bmiHeader.biHeight = -srcH;   // negative: top-down rows
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = DWORD(srcW) * DWORD(srcH) * 4u;

    // 32bpp QImage formats are always tightly packed (stride == width * 4).
    ::SetStretchBltMode(m_dc, HALFTONE);
    ::SetBrushOrgEx(m_dc, 0, 0, nullptr);

    const int written = ::StretchDIBits(m_dc, 0, 0, destW, destH, 0, 0, srcW, srcH,
                                        dib.constBits(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    if (written == 0 || written == GDI_ERROR) {
        const bool ok = fail(
            QStringLiteral("The card image could not be sent to the printer."),
            win32Detail(QStringLiteral("StretchDIBits")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    return true;
}

// --------------------------------------------------------------------------
// sendEscape - driver escape through TextOut (top coat / print blocking).
// --------------------------------------------------------------------------
bool GdiPrintSurface::sendEscape(const QString &escape, QString *error)
{
    if (escape.isEmpty())
        return true;

    if (!m_dc || !m_documentOpen) {
        const bool ok = fail(QStringLiteral("No print job is open."),
                             QStringLiteral("sendEscape: beginDocument() was not called"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    // Escapes belong to a card side; open one if none is open so the escape is
    // delivered with a page rather than between pages.
    if (!m_pageOpen) {
        if (::StartPage(m_dc) <= 0) {
            const bool ok = fail(QStringLiteral("The printer did not accept the page."),
                                 win32Detail(QStringLiteral("StartPage (escape)")));
            if (error)
                *error = m_lastError;
            return ok;
        }
        m_pageOpen = true;
    }

    const std::wstring text = escape.toStdWString();
    if (!::TextOutW(m_dc, 0, 0, text.c_str(), int(text.size()))) {
        const bool ok = fail(
            QStringLiteral("The printer rejected an option for this card side."),
            win32Detail(QStringLiteral("TextOut(escape)")));
        if (error)
            *error = m_lastError;
        return ok;
    }
    return true;
}

// --------------------------------------------------------------------------
// resetDevice - switch orientation between the front and back of a duplex card.
// --------------------------------------------------------------------------
bool GdiPrintSurface::resetDevice(bool landscape, QString *error)
{
    if (!m_dc || !m_documentOpen || !m_devmode) {
        const bool ok = fail(QStringLiteral("No print job is open."),
                             QStringLiteral("resetDevice: beginDocument() was not called"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    // ResetDC is only valid between pages.
    if (m_pageOpen) {
        if (!::EndPage(m_dc)) {
            const bool ok = fail(QStringLiteral("The card side could not be finished."),
                                 win32Detail(QStringLiteral("EndPage (before ResetDC)")));
            if (error)
                *error = m_lastError;
            return ok;
        }
        m_pageOpen = false;
    }

    std::wstring wname = m_printerName.toStdWString();
    m_devmode->dmOrientation = landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
    m_devmode->dmFields |= DM_ORIENTATION;

    const LONG rc = ::DocumentPropertiesW(nullptr, m_printerHandle, wname.data(), m_devmode,
                                          m_devmode, DM_IN_BUFFER | DM_OUT_BUFFER);
    if (rc != IDOK) {
        const bool ok = fail(QStringLiteral("The driver rejected the page settings."),
                             win32Detail(QStringLiteral("DocumentProperties (ResetDC)")));
        if (error)
            *error = m_lastError;
        return ok;
    }

    HDC newDc = ::ResetDCW(m_dc, m_devmode);
    if (!newDc) {
        const bool ok = fail(QStringLiteral("The printer could not switch card sides."),
                             win32Detail(QStringLiteral("ResetDC")));
        if (error)
            *error = m_lastError;
        return ok;
    }

    if (newDc != m_dc) {
        // Keep the spooler job id associated with the new device context.
        takeJobId(m_dc, newDc);
        m_dc = newDc;
    }
    m_options.landscape = landscape;
    return true;
}

// --------------------------------------------------------------------------
// endDocument - close the last page and the document, release every handle.
// --------------------------------------------------------------------------
bool GdiPrintSurface::endDocument(unsigned long *jobId, QString *error, QString *technical)
{
    if (jobId)
        *jobId = 0;

    if (!m_dc || !m_documentOpen) {
        const bool ok = fail(QStringLiteral("No print job is open."),
                             QStringLiteral("endDocument: beginDocument() was not called"));
        if (error)
            *error = m_lastError;
        return ok;
    }

    if (m_pageOpen) {
        if (!::EndPage(m_dc)) {
            const bool ok = fail(QStringLiteral("The last card side could not be finished."),
                                 win32Detail(QStringLiteral("EndPage")));
            if (error)
                *error = m_lastError;
            if (technical)
                *technical = m_lastTechnical;
            return ok;
        }
        m_pageOpen = false;
    }

    const unsigned long id = takeJobId(m_dc);

    if (::EndDoc(m_dc) <= 0) {
        const bool ok = fail(QStringLiteral("The print job could not be completed."),
                             win32Detail(QStringLiteral("EndDoc")));
        if (error)
            *error = m_lastError;
        if (technical)
            *technical = m_lastTechnical;
        return ok;
    }

    ::DeleteDC(m_dc);
    m_dc = nullptr;
    if (m_printerHandle) {
        ::ClosePrinter(m_printerHandle);
        m_printerHandle = nullptr;
    }
    m_devmode = nullptr;
    m_documentOpen = false;

    if (jobId)
        *jobId = id;
    return true;
}

// --------------------------------------------------------------------------
// abort - give up without finishing the document and release every handle.
// --------------------------------------------------------------------------
void GdiPrintSurface::abort()
{
    if (m_dc && m_documentOpen)
        ::AbortDoc(m_dc);
    if (m_dc) {
        forgetJobId(m_dc);
        ::DeleteDC(m_dc);
        m_dc = nullptr;
    }
    if (m_printerHandle) {
        ::ClosePrinter(m_printerHandle);
        m_printerHandle = nullptr;
    }
    m_devmode = nullptr;
    m_documentOpen = false;
    m_pageOpen = false;
}

// --------------------------------------------------------------------------
// waitUntilJobSpooled - the SDK's util::WaitUntilJobSpooled(), with a timeout.
// The spooler starts processing a job before all of its data has arrived; the
// iBidi EndJob must not be sent until the spooler is finished with the data.
// --------------------------------------------------------------------------
bool GdiPrintSurface::waitUntilJobSpooled(const QString &printerName, unsigned long jobId,
                                          int timeoutMs, QString *error)
{
    if (error)
        error->clear();
    if (jobId == 0)
    {
        if (error)
            *error = QStringLiteral("The spooler did not return a print job id.");
        return false;
    }

    const int budgetMs = timeoutMs > 0 ? timeoutMs : 60000;

    std::wstring wname = printerName.toStdWString();
    HANDLE printerHandle = nullptr;
    if (!::OpenPrinterW(wname.data(), &printerHandle, nullptr)) {
        if (error)
            *error = QStringLiteral("The printer \"%1\" could not be opened (%2).")
                         .arg(printerName)
                         .arg(win32Message(::GetLastError()));
        return false;
    }

    bool finished = false;
    QString failure;
    QElapsedTimer timer;
    timer.start();

    for (;;) {
        DWORD needed = 0;
        if (!::GetJobW(printerHandle, jobId, 2, nullptr, 0, &needed)) {
            const DWORD lastError = ::GetLastError();
            if (lastError == ERROR_INSUFFICIENT_BUFFER && needed > 0) {
                QByteArray buffer(int(needed), 0);
                DWORD returned = 0;
                if (::GetJobW(printerHandle, jobId, 2,
                              reinterpret_cast<LPBYTE>(buffer.data()), needed, &returned)) {
                    const JOB_INFO_2W *info =
                        reinterpret_cast<const JOB_INFO_2W *>(buffer.constData());
                    if (spoolState(info->Status) == SpoolState::Failed) {
                        failure = QStringLiteral("The spooler failed or cancelled print job %1 (status %2).")
                                      .arg(jobId).arg(info->Status);
                        break;
                    }
                    if (spoolState(info->Status) == SpoolState::Accepted) {
                        finished = true;
                        break;
                    }
                } else {
                    failure = QStringLiteral("Could not read spooler job %1: %2.")
                                  .arg(jobId).arg(win32Message(::GetLastError()));
                    break;
                }
            } else if (lastError == ERROR_INVALID_PARAMETER) {
                // The job no longer exists: it has left the spooler queue.
                finished = true;
                break;
            } else {
                failure = QStringLiteral("The spooler reported \"%1\" for print job %2.")
                              .arg(win32Message(lastError))
                              .arg(jobId);
                break;
            }
        }

        if (timer.elapsed() > budgetMs) {
            failure =
                QStringLiteral("Timed out waiting for the spooler to accept print job %1.")
                    .arg(jobId);
            break;
        }
        ::Sleep(500);
    }

    ::ClosePrinter(printerHandle);

    if (!finished) {
        if (error)
            *error = failure.isEmpty()
                         ? QStringLiteral("Print job %1 did not reach the printer.").arg(jobId)
                         : failure;
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// enumeratePrinters - the same PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS
// query the SDK samples use.
// --------------------------------------------------------------------------
QVector<GdiPrintSurface::PrinterInfo> GdiPrintSurface::enumeratePrinters()
{
    QVector<PrinterInfo> result;

    const DWORD flags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
    DWORD needed = 0;
    DWORD count = 0;

    if (!::EnumPrintersW(flags, nullptr, 2, nullptr, 0, &needed, &count)) {
        const DWORD lastError = ::GetLastError();
        if (lastError != ERROR_INSUFFICIENT_BUFFER)
            return result;   // no printers, or not permitted: an empty list is fine
    }
    if (needed == 0)
        return result;

    QByteArray buffer(int(needed), 0);
    if (!::EnumPrintersW(flags, nullptr, 2, reinterpret_cast<LPBYTE>(buffer.data()), needed,
                         &needed, &count))
        return result;

    const PRINTER_INFO_2W *info =
        reinterpret_cast<const PRINTER_INFO_2W *>(buffer.constData());
    for (DWORD i = 0; i < count; ++i, ++info) {
        PrinterInfo printer;
        if (info->pPrinterName)
            printer.name = QString::fromWCharArray(info->pPrinterName);
        if (info->pPortName)
            printer.port = QString::fromWCharArray(info->pPortName);
        if (info->pDriverName)
            printer.driverName = QString::fromWCharArray(info->pDriverName);
        if (info->pServerName)
            printer.serverName = QString::fromWCharArray(info->pServerName);
        printer.attributes = info->Attributes;
        printer.status = info->Status;
        printer.isNetwork = (info->Attributes & PRINTER_ATTRIBUTE_NETWORK) != 0;
        if (!printer.name.isEmpty())
            result.append(printer);
    }
    return result;
}

// --------------------------------------------------------------------------
// describePrinterStatus / describePrinterAttributes - human readable decoding
// of the PRINTER_INFO_2 bit masks, for the Diagnostics report.
// --------------------------------------------------------------------------
QStringList GdiPrintSurface::describePrinterStatus(DWORD status)
{
    QStringList parts;
    auto add = [&parts, status](DWORD bit, const char *text) {
        if (status & bit)
            parts << QString::fromLatin1(text);
    };

    add(PRINTER_STATUS_PAUSED, "Paused");
    add(PRINTER_STATUS_ERROR, "Error");
    add(PRINTER_STATUS_PENDING_DELETION, "Pending deletion");
    add(PRINTER_STATUS_PAPER_JAM, "Paper jam");
    add(PRINTER_STATUS_PAPER_OUT, "Paper out");
    add(PRINTER_STATUS_MANUAL_FEED, "Manual feed");
    add(PRINTER_STATUS_PAPER_PROBLEM, "Paper problem");
    add(PRINTER_STATUS_OFFLINE, "Offline");
    add(PRINTER_STATUS_IO_ACTIVE, "I/O active");
    add(PRINTER_STATUS_BUSY, "Busy");
    add(PRINTER_STATUS_PRINTING, "Printing");
    add(PRINTER_STATUS_OUTPUT_BIN_FULL, "Output bin full");
    add(PRINTER_STATUS_NOT_AVAILABLE, "Not available");
    add(PRINTER_STATUS_WAITING, "Waiting");
    add(PRINTER_STATUS_PROCESSING, "Processing");
    add(PRINTER_STATUS_INITIALIZING, "Initializing");
    add(PRINTER_STATUS_WARMING_UP, "Warming up");
    add(PRINTER_STATUS_TONER_LOW, "Toner low");
    add(PRINTER_STATUS_NO_TONER, "No toner");
    add(PRINTER_STATUS_USER_INTERVENTION, "User intervention required");
    add(PRINTER_STATUS_OUT_OF_MEMORY, "Out of memory");
    add(PRINTER_STATUS_DOOR_OPEN, "Door open");
    add(PRINTER_STATUS_SERVER_UNKNOWN, "Server unknown");
    add(PRINTER_STATUS_POWER_SAVE, "Power save");
    add(PRINTER_STATUS_PAGE_PUNT, "Page punt");

    if (parts.isEmpty())
        parts << QStringLiteral("Ready");
    return parts;
}

QString GdiPrintSurface::describePrinterAttributes(DWORD attributes)
{
    QStringList parts;
    auto add = [&parts, attributes](DWORD bit, const char *text) {
        if (attributes & bit)
            parts << QString::fromLatin1(text);
    };

    add(PRINTER_ATTRIBUTE_QUEUED, "Queued");
    add(PRINTER_ATTRIBUTE_DIRECT, "Direct");
    add(PRINTER_ATTRIBUTE_DEFAULT, "Default");
    add(PRINTER_ATTRIBUTE_SHARED, "Shared");
    add(PRINTER_ATTRIBUTE_NETWORK, "Network");
    add(PRINTER_ATTRIBUTE_LOCAL, "Local");
    add(PRINTER_ATTRIBUTE_ENABLE_DEVQ, "Enable devq");
    add(PRINTER_ATTRIBUTE_KEEPPRINTEDJOBS, "Keep printed jobs");
    add(PRINTER_ATTRIBUTE_DO_COMPLETE_FIRST, "Complete first");
    add(PRINTER_ATTRIBUTE_WORK_OFFLINE, "Work offline");
    add(PRINTER_ATTRIBUTE_ENABLE_BIDI, "Bidirectional");
    add(PRINTER_ATTRIBUTE_RAW_ONLY, "Raw only");
    add(PRINTER_ATTRIBUTE_PUBLISHED, "Published");

    if (parts.isEmpty())
        return QStringLiteral("None");
    return parts.join(QStringLiteral(", "));
}

// --------------------------------------------------------------------------
// printableAreaMm - the physical size of the printable area of a queue,
// obtained without starting a job (GetDeviceCaps never spools).
// --------------------------------------------------------------------------
bool GdiPrintSurface::printableAreaMm(const QString &printerName, const Options &options,
                                      double *widthMm, double *heightMm, QString *error)
{
    if (widthMm)
        *widthMm = 0.0;
    if (heightMm)
        *heightMm = 0.0;
    if (error)
        error->clear();

    std::wstring wname = printerName.toStdWString();
    HANDLE printerHandle = nullptr;
    if (!::OpenPrinterW(wname.data(), &printerHandle, nullptr)) {
        if (error)
            *error = QStringLiteral("The printer \"%1\" could not be opened (%2).")
                         .arg(printerName)
                         .arg(win32Message(::GetLastError()));
        return false;
    }

    QByteArray devmodeBytes;
    DEVMODE *devmode = nullptr;
    const LONG size =
        ::DocumentPropertiesW(nullptr, printerHandle, wname.data(), nullptr, nullptr, 0);
    if (size > 0) {
        devmodeBytes.resize(int(size));
        devmode = reinterpret_cast<DEVMODE *>(devmodeBytes.data());
        if (::DocumentPropertiesW(nullptr, printerHandle, wname.data(), devmode, nullptr,
                                  DM_OUT_BUFFER) >= 0) {
            devmode->dmOrientation =
                options.landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            devmode->dmFields |= DM_ORIENTATION;
            devmode->dmDuplex = options.duplex ? DMDUP_VERTICAL : DMDUP_SIMPLEX;
            devmode->dmFields |= DM_DUPLEX;
        } else {
            devmode = nullptr;
        }
    }

    HDC dc = ::CreateDCW(L"WINSPOOL", wname.data(), nullptr, devmode);
    if (!dc) {
        ::ClosePrinter(printerHandle);
        if (error)
            *error =
                QStringLiteral("A printing context for \"%1\" could not be created (%2).")
                    .arg(printerName)
                    .arg(win32Message(::GetLastError()));
        return false;
    }

    const int horizPx = ::GetDeviceCaps(dc, HORZRES);
    const int vertPx = ::GetDeviceCaps(dc, VERTRES);
    int dpiX = ::GetDeviceCaps(dc, LOGPIXELSX);
    int dpiY = ::GetDeviceCaps(dc, LOGPIXELSY);
    ::DeleteDC(dc);
    ::ClosePrinter(printerHandle);

    if (dpiX <= 0)
        dpiX = options.resolutionDpi > 0 ? options.resolutionDpi : 300;
    if (dpiY <= 0)
        dpiY = dpiX;
    if (horizPx <= 0 || vertPx <= 0) {
        if (error)
            *error = QStringLiteral("The driver for \"%1\" did not report a printable area.")
                         .arg(printerName);
        return false;
    }

    if (widthMm)
        *widthMm = double(horizPx) * 25.4 / double(dpiX);
    if (heightMm)
        *heightMm = double(vertPx) * 25.4 / double(dpiY);
    return true;
}

} // namespace occ
