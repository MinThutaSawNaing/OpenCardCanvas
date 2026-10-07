#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>

// ---------------------------------------------------------------------------
// PrinterTypes - data shared by every printer backend.
//
// Information that a backend cannot obtain is left explicitly "not available"
// rather than guessed: the UI shows "Not available" instead of inventing a
// status, which is the difference between a diagnostic tool and a fake one.
// ---------------------------------------------------------------------------
namespace occ {

enum class PrinterBackendKind { EntrustXps, Windows, Simulator };

enum class PrinterConnection { Unknown, Usb, Network, Serial, Parallel, Virtual };

// Normalised printer state. `Unknown` and `NotAvailable` are meaningful:
// Unknown = we could not query; NotAvailable = the device does not report it.
enum class PrinterState {
    NotQueried,
    Ready,
    Printing,
    Busy,
    Offline,
    Warning,
    Error,
    NotAvailable
};

enum class JobState {
    NotAvailable,
    Active,
    Succeeded,
    Failed,
    Cancelled,
    CardReadyToRetrieve,
    CardNotRetrieved,
    Unknown
};

struct PrinterIdentity
{
    QString              name;              // Windows printer name (unique)
    QString              manufacturer;
    QString              model;
    QString              driverName;
    QString              driverVersion;
    QString              port;
    QString              serialNumber;      // empty when the device does not report it
    QString              firmwareVersion;
    PrinterConnection    connection = PrinterConnection::Unknown;
    PrinterBackendKind   backend = PrinterBackendKind::Windows;

    // Capability flags. `known` distinguishes "the device says no" from
    // "we could not determine it".
    bool duplexSupported = false;
    bool duplexKnown = false;
    bool hasLaminator = false;   bool laminatorKnown = false;
    bool hasMagstripe = false;   bool magstripeKnown = false;
    bool hasSmartcard = false;   bool smartcardKnown = false;
    bool hasEmbosser = false;    bool embosserKnown = false;
    bool hasBarcodeReader = false; bool barcodeReaderKnown = false;
    bool hasPrintHead = false;   bool printHeadKnown = false;

    QString colorResolution;      // e.g. "300x300"
    QString monochromeResolution; // e.g. "600x600"
};

struct RibbonSupply
{
    bool    available = false;
    QString type;
    QString serialNumber;
    QString lotCode;
    int     percentRemaining = -1;   // -1 = not reported
    int     partNumber = 0;
};

struct LaminatorSupply
{
    bool available = false;
    int  percentRemaining = -1;
    QString lotCode;
    QString serialNumber;
};

struct PrinterStatus
{
    PrinterState state = PrinterState::NotQueried;
    QString      stateText;      // raw device text, e.g. "Ready", "Busy"
    QString      detail;         // user facing explanation
    int          errorCode = 0;
    int          severity = 0;   // 0 none, 1 alert .. 5 notice (SDK scale)
    QString      errorString;

    bool    iBidiAvailable = false;   // true when the device answered BidiSpl
    QString sdkVersion;

    RibbonSupply    printRibbon;
    RibbonSupply    topcoatRibbon;
    RibbonSupply    retransferFilm;
    LaminatorSupply laminatorL1;
    LaminatorSupply laminatorL2;

    bool        hopperStatusAvailable = false;
    QString     inputHopperStatus;    // "Full", "Low", "Empty", ...
    QStringList messages;

    QDateTime updatedAt;

    bool isReady() const
    { return state == PrinterState::Ready || state == PrinterState::Printing
             || state == PrinterState::Busy; }
};

// Top coat / print blocking presets defined by the XPS Card Printer driver.
enum class TopcoatPreset { DriverDefault, All, Except, Iso7816, Iso2Track, Iso3Track, Jis };

struct PrintJob
{
    QString printerName;
    QString documentName;
    int     copies = 1;

    bool frontEnabled = true;
    bool backEnabled = false;
    bool duplex = false;

    // Orientation of the CARD ON THE PAGE. Card printers are landscape devices,
    // but a portrait card (ISO ID-1 rotated, a badge, a luggage tag) must still
    // print upright, so this is taken from the document geometry rather than
    // assumed.
    bool landscape = true;

    // Card sides rendered at the printer's own resolution. The renderer
    // guarantees these match what the preview and the PNG export produce.
    QImage frontImage;
    QImage backImage;

    int     frontDpi = 300;
    int     backDpi = 300;

    // Hardware options
    QString hopperId;                  // empty = driver default
    QString cardEjectSide;             // "Front" / "Back" / empty = default
    TopcoatPreset frontTopcoat = TopcoatPreset::DriverDefault;
    TopcoatPreset backTopcoat = TopcoatPreset::DriverDefault;

    // Job tracking
    bool waitForCompletion = true;
    int  completionTimeoutSeconds = 180;
    // Emitted while polling, so a UI can show progress without blocking.
    // (Implemented with a callback rather than a signal so backends stay
    // independent of Qt's object model.)
    std::function<void(int printerJobId, JobState state, const QString &text)> progress;

    // Set by the simulator only: where to write the simulated card images.
    QString simulatorOutputDirectory;
};

struct PrintResult
{
    bool    ok = false;
    QString error;              // user facing, ready to display
    QString technicalDetail;    // goes to the log, shown in Diagnostics

    int           printerJobId = 0;      // 0 when no card job was started
    unsigned long windowsJobId = 0;

    bool    completed = false;
    JobState finalJobState = JobState::NotAvailable;
    int     errorCode = 0;
    QString errorString;

    bool simulated = false;      // true only for the simulator backend
};

} // namespace occ

Q_DECLARE_METATYPE(occ::PrinterState)
Q_DECLARE_METATYPE(occ::PrinterBackendKind)
