#pragma once

#include "printing/ICardPrinter.h"

#include <QString>

namespace occ {

// ---------------------------------------------------------------------------
// WindowsPrinterBackend - prints through any installed Windows print queue via
// Qt's print support.
//
// Used for ordinary office printers (proof sheets, low cost drafts) and as a
// fallback when no Entrust / Datacard card printer is present. It never claims
// to be card printer hardware: isRealHardware() is false unless the queue is
// an actual card printer, and the UI labels it accordingly.
// ---------------------------------------------------------------------------
class WindowsPrinterBackend : public ICardPrinter
{
public:
    explicit WindowsPrinterBackend(const QString &printerName);
    ~WindowsPrinterBackend() override;

    QString name() const override { return m_name; }
    PrinterBackendKind backend() const override { return PrinterBackendKind::Windows; }
    PrinterIdentity identity() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }

    PrinterStatus status() override;
    bool print(const PrintJob &job) override;

    QString lastError() const override { return m_lastError; }
    QString diagnosticsReport() override;
    bool isRealHardware() const override { return true; }
    bool cancelJob(int printerJobId = 0) override;

private:
    QString m_name;
    bool    m_connected = false;
    bool    m_lastPrintSucceeded = false;
    QString m_lastError;
    QString m_lastTechnical;
};

} // namespace occ
