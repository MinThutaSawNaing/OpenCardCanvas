// ---------------------------------------------------------------------------
// PrinterManager - implementation.
// ---------------------------------------------------------------------------
#include "printing/PrinterManager.h"

#include "printing/EntrustXpsPrinter.h"
#include "printing/SimulatorPrinter.h"
#include "printing/WindowsPrinterBackend.h"
#include "utils/BidiClient.h"
#include "utils/GdiPrintSurface.h"

#include <QDateTime>
#include <QStringList>

#include <memory>

namespace occ {

namespace {

PrinterConnection connectionFromPort(const QString &port, bool isNetwork)
{
    if (isNetwork)
        return PrinterConnection::Network;
    if (port.startsWith(QLatin1String("USB"), Qt::CaseInsensitive))
        return PrinterConnection::Usb;
    if (port.startsWith(QLatin1String("COM"), Qt::CaseInsensitive))
        return PrinterConnection::Serial;
    if (port.startsWith(QLatin1String("LPT"), Qt::CaseInsensitive))
        return PrinterConnection::Parallel;
    if (port.startsWith(QLatin1String("IP_"), Qt::CaseInsensitive)
        || port.contains(QLatin1Char('.')))
        return PrinterConnection::Network;
    if (port.startsWith(QLatin1String("PORTPROMPT"), Qt::CaseInsensitive)
        || port.startsWith(QLatin1String("FILE"), Qt::CaseInsensitive))
        return PrinterConnection::Virtual;
    return PrinterConnection::Unknown;
}

} // namespace

PrinterManager::PrinterManager(QObject *parent)
    : QObject(parent)
{
}

PrinterManager::~PrinterManager() = default;

// --------------------------------------------------------------------------
// isEntrustCardPrinter - a real query, never a name match.
// --------------------------------------------------------------------------
bool PrinterManager::isEntrustCardPrinter(const QString &printerName,
                                          QString *sdkVersion)
{
    if (sdkVersion)
        sdkVersion->clear();

    BidiClient client;
    QString openError;
    if (!client.open(printerName, &openError))
        return false;

    QString version;
    if (!client.looksLikeCardPrinter(&version))
        return false;

    if (sdkVersion)
        *sdkVersion = version;
    return true;
}

// --------------------------------------------------------------------------
// refresh
// --------------------------------------------------------------------------
QVector<PrinterIdentity> PrinterManager::refresh()
{
    m_lastError.clear();
    m_backends.clear();

    QVector<PrinterIdentity> discovered;

    for (const GdiPrintSurface::PrinterInfo &info : GdiPrintSurface::enumeratePrinters()) {
        PrinterIdentity identity;
        identity.name = info.name;
        identity.driverName = info.driverName;
        identity.port = info.port;
        identity.connection = connectionFromPort(info.port, info.isNetwork);

        QString sdkVersion;
        if (isEntrustCardPrinter(info.name, &sdkVersion)) {
            identity.backend = PrinterBackendKind::EntrustXps;
            if (!sdkVersion.isEmpty())
                identity.firmwareVersion = sdkVersion;

            // Fill in the model, serial and capabilities from the driver.
            BidiClient client;
            if (client.open(info.name)) {
                QString xml;
                if (client.query(BidiClient::schemaPrinterOptions(), &xml)) {
                    PrinterStatus st;
                    QString parseError;
                    EntrustXpsPrinter::parsePrinterOptionsXml(xml, &identity, &st,
                                                              &parseError);
                }
            }
        } else {
            identity.backend = PrinterBackendKind::Windows;
        }

        discovered.append(identity);
    }

    m_printers = discovered;
    emit printersChanged();
    return m_printers;
}

// --------------------------------------------------------------------------
// printer
// --------------------------------------------------------------------------
CardPrinterPtr PrinterManager::printer(const QString &name)
{
    m_lastError.clear();

    if (auto cached = m_backends.constFind(name); cached != m_backends.constEnd())
        return cached.value();

    if (m_printers.isEmpty())
        refresh();

    PrinterBackendKind kind = PrinterBackendKind::Windows;
    bool known = false;
    for (const PrinterIdentity &identity : m_printers) {
        if (identity.name == name) {
            known = true;
            kind = identity.backend;
            break;
        }
    }

    if (!known) {
        m_lastError =
            QStringLiteral("The printer \"%1\" is not installed for the current user.")
                .arg(name);
        return nullptr;
    }

    CardPrinterPtr created;
    if (kind == PrinterBackendKind::EntrustXps)
        created = std::make_shared<EntrustXpsPrinter>(name);
    else
        created = std::make_shared<WindowsPrinterBackend>(name);

    m_backends.insert(name, created);
    return created;
}

// --------------------------------------------------------------------------
// simulator
// --------------------------------------------------------------------------
CardPrinterPtr PrinterManager::simulator()
{
    if (!m_simulator)
        m_simulator = std::make_shared<SimulatorPrinter>();
    return m_simulator;
}

// --------------------------------------------------------------------------
// statusesFor
// --------------------------------------------------------------------------
QVector<PrinterStatus> PrinterManager::statusesFor(const QStringList &names)
{
    QVector<PrinterStatus> statuses;
    statuses.reserve(names.size());

    for (const QString &name : names) {
        const CardPrinterPtr backend = printer(name);
        if (!backend) {
            PrinterStatus status;
            status.state = PrinterState::NotAvailable;
            status.detail = m_lastError;
            status.updatedAt = QDateTime::currentDateTime();
            statuses.append(status);
            continue;
        }
        statuses.append(backend->status());
    }
    return statuses;
}

} // namespace occ
