#pragma once

#include "printing/PrinterTypes.h"

#include <QImage>
#include <QString>
#include <QStringList>

#include <windows.h>

// ---------------------------------------------------------------------------
// GdiPrintSurface - prints a rendered card image to a Windows printer DC.
//
// Why GDI and not QPrinter: the XPS Card Printer Driver SDK's documented C/C++
// printing path IS the Win32 GDI print API. The SDK samples create a printer DC
// with CreateDC("WINSPOOL", printerName, NULL, devmode), call StartDoc /
// StartPage, draw with GDI, then EndPage / EndDoc, and use ResetDC to switch
// orientation between the front and back of a duplex card. Point sizes and
// driver level "escapes" (top coat blocking, magnetic stripe encoding) are all
// sent through this same DC.
//
// This class wraps exactly that path and blits a pre-rendered card image with
// StretchDIBits, so the application renders once (see CardRenderer) and every
// output device receives identical pixels.
// ---------------------------------------------------------------------------
namespace occ {

class GdiPrintSurface
{
public:
    struct Options
    {
        bool   landscape = true;
        bool   duplex = false;
        int    resolutionDpi = 300;      // requested device resolution
        QString documentName;
    };

    GdiPrintSurface();
    ~GdiPrintSurface();

    GdiPrintSurface(const GdiPrintSurface &) = delete;
    GdiPrintSurface &operator=(const GdiPrintSurface &) = delete;

    // Opens the printer and starts a document. `error` receives a user facing
    // message; `technical` receives the Win32 error for the log.
    bool beginDocument(const QString &printerName, const Options &options,
                       QString *error = nullptr, QString *technical = nullptr);

    // Starts a page and draws `image` scaled to the full printable page.
    // `imageDpi` is the resolution the image was rendered at.
    bool drawPage(const QImage &image, int imageDpi, QString *error = nullptr,
                  QString *technical = nullptr);

    // Sends a raw driver escape string through TextOut, which is how the SDK
    // applies top coat blocking regions and magnetic stripe data. Only used
    // when the user explicitly configures those options.
    bool sendEscape(const QString &escape, QString *error = nullptr);

    // Reapplies a modified DEVMODE (used between the front and back page of a
    // duplex card so each side can have its own orientation).
    bool resetDevice(bool landscape, QString *error = nullptr);

    // Ends the page and document. Returns the Windows job id through `jobId`.
    bool endDocument(unsigned long *jobId = nullptr, QString *error = nullptr,
                     QString *technical = nullptr);

    // Aborts without finishing the document (used on error paths).
    void abort();

    // Waits until the spooler has accepted all data for `jobId`. This mirrors
    // util::WaitUntilJobSpooled in the SDK samples and is required before the
    // printer is told the job is complete.
    static bool waitUntilJobSpooled(const QString &printerName, unsigned long jobId,
                                    int timeoutMs, QString *error = nullptr);

    // Enumerates the printers installed for the current user, using the same
    // PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS flags as the SDK samples.
    struct PrinterInfo
    {
        QString name;
        QString port;
        QString driverName;
        QString serverName;
        bool    isNetwork = false;
        DWORD   status = 0;      // PRINTER_INFO_2::Status
        DWORD   attributes = 0;
    };
    static QVector<PrinterInfo> enumeratePrinters();

    // Human readable decoding of a PRINTER_INFO_2 status mask.
    static QStringList describePrinterStatus(DWORD status);
    static QString describePrinterAttributes(DWORD attributes);

    // Width in device pixels of the printable area of `printerName`.
    static bool printableAreaMm(const QString &printerName, const Options &options,
                                double *widthMm, double *heightMm, QString *error = nullptr);

    QString lastError() const { return m_lastError; }
    QString lastTechnicalDetail() const { return m_lastTechnical; }

private:
    bool fail(const QString &userMessage, const QString &technical);

    HDC      m_dc = nullptr;
    HANDLE   m_printerHandle = nullptr;
    QString  m_printerName;
    QByteArray m_devmodeBytes;
    DEVMODE *m_devmode = nullptr;
    bool     m_documentOpen = false;
    bool     m_pageOpen = false;
    Options  m_options;
    QString  m_lastError;
    QString  m_lastTechnical;
};

} // namespace occ
