#pragma once

#include "printing/ICardPrinter.h"
#include "utils/BidiClient.h"

#include <QString>

namespace occ {

// ---------------------------------------------------------------------------
// EntrustXpsPrinter - a real Entrust / Datacard XPS Card Printer queue.
//
// Status, capabilities and supplies come from the print spooler's BidiSpl
// interface with the schemas defined by the XPS Card Printer Driver SDK
// (samples/cpp/common/DXP01SDK.H). Printing uses the same Win32 GDI path as the
// SDK "print" sample. Nothing here is simulated: every value the device does not
// report is left empty / -1 / NotAvailable rather than guessed, and a job is
// only reported as successful when the device actually says JobSucceeded.
// ---------------------------------------------------------------------------
class EntrustXpsPrinter : public ICardPrinter
{
public:
    explicit EntrustXpsPrinter(const QString &printerName);
    ~EntrustXpsPrinter() override;

    QString name() const override { return m_printerName; }
    PrinterBackendKind backend() const override { return PrinterBackendKind::EntrustXps; }
    PrinterIdentity identity() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }

    PrinterStatus status() override;

    // ICardPrinter::print() returns whether the card was printed; the full
    // result (job ids, final state, error text) is available from printJob().
    bool print(const PrintJob &job) override;
    PrintResult printJob(const PrintJob &job);
    PrintResult lastPrintResult() const { return m_lastResult; }

    QString lastError() const override { return m_lastError; }
    QString diagnosticsReport() override;
    bool isRealHardware() const override { return true; }
    bool cancelJob(int printerJobId = 0) override;

    // --- static XML parsers (also used by the unit tests) -------------------
    // Each returns false with a user facing `error` when the text is not valid
    // XML or not the expected document. Fields the device omits are left at
    // their "not available" defaults.
    static bool parsePrinterOptionsXml(const QString &xml, PrinterIdentity *id,
                                       PrinterStatus *st, QString *error);
    static bool parseSuppliesXml(const QString &xml, PrinterStatus *st, QString *error);
    // ClientID / PrinterJobID / ErrorCode / ErrorSeverity / ErrorString.
    static bool parsePrinterStatusXml(const QString &xml, PrinterStatus *st, QString *error);
    static bool parseJobStatusXml(const QString &xml, JobState *state, int *windowsJobId,
                                  QString *error);

    static TopcoatPreset topcoatFromString(const QString &text);
    // The driver escape for a top coat preset ("~TA%" top coat add and "~PB%"
    // print blocking, exactly as the SDK sample writes them). "" for
    // DriverDefault, so the driver's own setting is left untouched.
    static QString topcoatEscapes(TopcoatPreset preset, bool frontSide);

private:
    bool ensureConnected();
    void pollForCompletion(const PrintJob &job, PrintResult &result);
    int printerJobIdFromStatusXml(const QString &xml) const;

    QString m_printerName;
    BidiClient m_bidi;

    bool m_connected = false;
    bool m_bidiAvailable = false;

    QString m_sdkVersion;
    QString m_colorMode;
    QString m_lastError;
    QString m_lastTechnical;

    PrinterIdentity m_identity;
    PrintResult m_lastResult;
};

} // namespace occ
