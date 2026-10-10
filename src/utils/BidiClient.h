#pragma once

#include <QString>

#include <memory>

// ---------------------------------------------------------------------------
// BidiClient - the print spooler's BidiSpl COM channel.
//
// This is the documented way to talk to an Entrust / Datacard XPS card printer
// driver. The XPS Card Printer Driver SDK exposes printer information through
// the spooler's COM server (CLSID_BidiSpl / CLSID_BidiRequest from bidispl.h)
// with schema strings such as:
//
//     \Printer.PrinterOptions2:Read      capabilities, model, serial, status
//     \Printer.PrinterOptions3:Read      colour mode
//     \Printer.SuppliesStatus3:Read      ribbon / laminate / film levels
//     \Printer.PrintMessages:Read        device messages and errors
//     \Printer.JobStatus:Read            per card job state
//     \Printer.SDK:Version               driver SDK version
//     \Printer.Print:StartJob:Set        claim a printer job id
//     \Printer.Print:EndJob:Set          signal that all data was sent
//     \Printer.Action:Set                cancel / resume / restart
//
// These strings come from the SDK header DXP01SDK.H; nothing here is invented.
//
// The class owns a COM apartment initialisation and releases the BidiSpl
// object on close, so callers never touch COM directly.
// ---------------------------------------------------------------------------
namespace occ {

class BidiClient
{
public:
    BidiClient();
    ~BidiClient();

    BidiClient(const BidiClient &) = delete;
    BidiClient &operator=(const BidiClient &) = delete;

    // Binds to `printerName`. Returns false when the spooler or the driver does
    // not provide a Bidi interface (for example a plain office printer).
    bool open(const QString &printerName, QString *error = nullptr);
    void close();
    bool isOpen() const;

    // BIDI_ACTION_GET with the given schema. `xml` receives the reply.
    bool query(const QString &schema, QString *xml, QString *error = nullptr,
               const QString &inputXml = QString());

    // BIDI_ACTION_SET with XML payload (StartJob / EndJob / Action).
    bool set(const QString &schema, const QString &inputXml, QString *error = nullptr,
             QString *outputXml = nullptr);

    // SendRecv with no payload (used by EndJob).
    bool send(const QString &schema, QString *error = nullptr);

    // True when a BidiSpl round trip succeeded - the practical test for
    // "is this printer driven by the XPS Card Printer driver".
    bool looksLikeCardPrinter(QString *sdkVersion = nullptr, QString *error = nullptr);

    QString lastError() const { return m_lastError; }
    QString lastTechnicalDetail() const { return m_lastTechnical; }

    // BidiSpl schemas, exactly as defined by the SDK header.
    static QString schemaStartJob();
    static QString schemaEndJob();
    static QString schemaPrinterOptions();
    static QString schemaPrinterOptions3();
    static QString schemaSupplies();
    static QString schemaPrintMessages();
    static QString schemaJobStatus();
    static QString schemaSdkVersion();
    static QString schemaPrinterAction();
    static QString schemaHopperStatus();

    // XML payload builders (mirror the SDK's STARTJOB_XML / PRINTER_ACTION_XML).
    static QString startJobXml(const QString &hopperId, const QString &cardEjectSide);
    static QString jobStatusXml(int printerJobId);
    static QString printerActionXml(int action, int printerJobId, int errorCode);

    // Action ids from the SDK header.
    enum Action { Cancel = 100, Resume = 101, Restart = 102 };

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QString m_lastError;
    QString m_lastTechnical;
};

} // namespace occ
