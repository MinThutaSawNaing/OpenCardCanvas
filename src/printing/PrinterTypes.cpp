// ---------------------------------------------------------------------------
// PrinterTypes.cpp - out-of-line helpers for the shared printer data types.
//
// The enumerations in PrinterTypes.h need stable, human readable names in three
// places: the UI (status labels), the log file, and the .occard/preset files.
// Keeping the mapping here means there is exactly one definition of each name.
// ---------------------------------------------------------------------------
#include "printing/PrinterTypes.h"

namespace occ {

// --------------------------------------------------------------------------
// PrinterState
// --------------------------------------------------------------------------
QString printerStateToString(PrinterState state)
{
    switch (state) {
    case PrinterState::NotQueried:   return QStringLiteral("Not queried");
    case PrinterState::Ready:        return QStringLiteral("Ready");
    case PrinterState::Printing:     return QStringLiteral("Printing");
    case PrinterState::Busy:         return QStringLiteral("Busy");
    case PrinterState::Offline:      return QStringLiteral("Offline");
    case PrinterState::Warning:      return QStringLiteral("Warning");
    case PrinterState::Error:        return QStringLiteral("Error");
    case PrinterState::NotAvailable: return QStringLiteral("Not available");
    }
    return QStringLiteral("Unknown");
}

PrinterState printerStateFromString(const QString &text)
{
    const QString s = text.trimmed().toLower();
    if (s == QLatin1String("ready"))        return PrinterState::Ready;
    if (s == QLatin1String("printing"))     return PrinterState::Printing;
    if (s == QLatin1String("busy"))         return PrinterState::Busy;
    if (s == QLatin1String("offline"))      return PrinterState::Offline;
    if (s == QLatin1String("warning"))      return PrinterState::Warning;
    if (s == QLatin1String("error"))        return PrinterState::Error;
    if (s == QLatin1String("not available"))return PrinterState::NotAvailable;
    if (s == QLatin1String("not queried"))  return PrinterState::NotQueried;
    return PrinterState::NotQueried;
}

// --------------------------------------------------------------------------
// JobState - the strings on the right are the exact values the XPS Card
// Printer driver reports (DXP01SDK.H: JOB_ACTIVE, JOB_SUCCEEDED, ...).
// --------------------------------------------------------------------------
QString jobStateToString(JobState state)
{
    switch (state) {
    case JobState::NotAvailable:        return QStringLiteral("Not available");
    case JobState::Active:              return QStringLiteral("Printing card");
    case JobState::Succeeded:           return QStringLiteral("Card printed");
    case JobState::Failed:              return QStringLiteral("Card failed");
    case JobState::Cancelled:           return QStringLiteral("Cancelled");
    case JobState::CardReadyToRetrieve: return QStringLiteral("Card ready to retrieve");
    case JobState::CardNotRetrieved:    return QStringLiteral("Card not retrieved");
    case JobState::Unknown:             return QStringLiteral("Unknown");
    }
    return QStringLiteral("Unknown");
}

JobState jobStateFromString(const QString &text)
{
    const QString s = text.trimmed();
    if (s.isEmpty())                            return JobState::NotAvailable;
    if (s.compare(QLatin1String("NotAvailable"), Qt::CaseInsensitive) == 0)
        return JobState::NotAvailable;
    if (s.compare(QLatin1String("JobActive"), Qt::CaseInsensitive) == 0)
        return JobState::Active;
    if (s.compare(QLatin1String("JobSucceeded"), Qt::CaseInsensitive) == 0)
        return JobState::Succeeded;
    if (s.compare(QLatin1String("JobFailed"), Qt::CaseInsensitive) == 0)
        return JobState::Failed;
    if (s.compare(QLatin1String("JobCancelled"), Qt::CaseInsensitive) == 0)
        return JobState::Cancelled;
    if (s.compare(QLatin1String("CardReadyToRetrieve"), Qt::CaseInsensitive) == 0)
        return JobState::CardReadyToRetrieve;
    if (s.compare(QLatin1String("CardNotRetrieved"), Qt::CaseInsensitive) == 0)
        return JobState::CardNotRetrieved;
    return JobState::Unknown;
}

// --------------------------------------------------------------------------
// PrinterBackendKind
// --------------------------------------------------------------------------
QString backendKindToString(PrinterBackendKind kind)
{
    switch (kind) {
    case PrinterBackendKind::EntrustXps: return QStringLiteral("XPS Card Printer driver");
    case PrinterBackendKind::Windows:    return QStringLiteral("Windows printer driver");
    case PrinterBackendKind::Simulator:  return QStringLiteral("Simulator (no hardware)");
    }
    return QStringLiteral("Unknown");
}

// --------------------------------------------------------------------------
// PrinterConnection - derived from the queue's port name.
// --------------------------------------------------------------------------
QString connectionToString(PrinterConnection connection)
{
    switch (connection) {
    case PrinterConnection::Usb:      return QStringLiteral("USB");
    case PrinterConnection::Network:  return QStringLiteral("Network");
    case PrinterConnection::Serial:   return QStringLiteral("Serial");
    case PrinterConnection::Parallel: return QStringLiteral("Parallel");
    case PrinterConnection::Virtual:  return QStringLiteral("Virtual");
    case PrinterConnection::Unknown:  break;
    }
    return QStringLiteral("Unknown");
}

// --------------------------------------------------------------------------
// TopcoatPreset - matches the SDK print sample's -t / -u command line values
// (all | except | chip | mag2 | mag3 | magJIS).
// --------------------------------------------------------------------------
QString topcoatPresetToString(TopcoatPreset preset)
{
    switch (preset) {
    case TopcoatPreset::DriverDefault: return QStringLiteral("default");
    case TopcoatPreset::All:           return QStringLiteral("all");
    case TopcoatPreset::Except:        return QStringLiteral("except");
    case TopcoatPreset::Iso7816:       return QStringLiteral("chip");
    case TopcoatPreset::Iso2Track:     return QStringLiteral("mag2");
    case TopcoatPreset::Iso3Track:     return QStringLiteral("mag3");
    case TopcoatPreset::Jis:           return QStringLiteral("magJIS");
    }
    return QStringLiteral("default");
}

} // namespace occ
