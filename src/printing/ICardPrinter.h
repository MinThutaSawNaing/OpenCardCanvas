#pragma once

#include "printing/PrinterTypes.h"

#include <QString>

#include <functional>
#include <memory>

// ---------------------------------------------------------------------------
// ICardPrinter - the printer abstraction.
//
// The rest of the application never talks to a manufacturer SDK directly. That
// keeps the editor usable when no card printer is installed and makes it
// possible to add another vendor without touching the printing workflow.
//
// Implementations:
//   EntrustXpsPrinter     XPS Card Printer Driver SDK (Windows spooler BidiSpl
//                         status/options/supplies + GDI printing)
//   WindowsPrinterBackend any standard Windows printer driver
//   SimulatorPrinter      clearly separated, renders and records a job but
//                         NEVER reports that a card was printed
// ---------------------------------------------------------------------------
namespace occ {

class ICardPrinter
{
public:
    virtual ~ICardPrinter() = default;

    // --- identity -----------------------------------------------------------
    virtual QString name() const = 0;
    virtual PrinterBackendKind backend() const = 0;
    virtual PrinterIdentity identity() = 0;

    // --- connection ---------------------------------------------------------
    // `connect()` binds to the print queue / device. It must not throw; failures
    // are reported through lastError().
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;

    // --- status -------------------------------------------------------------
    // Queries the device. Returns a status whose fields are explicitly marked
    // NotAvailable where the device does not report them.
    virtual PrinterStatus status() = 0;

    // --- printing -----------------------------------------------------------
    virtual bool print(const PrintJob &job) = 0;

    // --- diagnostics --------------------------------------------------------
    virtual QString lastError() const = 0;
    // Multi line, user facing report for the Diagnostics dialog:
    // driver, SDK availability, raw device values, capabilities.
    virtual QString diagnosticsReport() = 0;

    // True when this backend can actually put ink on a card. The window only
    // claims success for backends that return true.
    virtual bool isRealHardware() const = 0;

    // Optional extras - default to "not supported" so backends stay small.
    // Cancels the job currently being printed. Returns false and sets
    // lastError() when the device cannot be asked to cancel.
    virtual bool cancelJob(int printerJobId = 0)
    {
        Q_UNUSED(printerJobId);
        return false;
    }
};

using CardPrinterPtr = std::shared_ptr<ICardPrinter>;

} // namespace occ
