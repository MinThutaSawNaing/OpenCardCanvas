// ---------------------------------------------------------------------------
// EntrustXpsPrinter - XPS Card Printer Driver SDK backend.
//
// Everything here follows the shipped SDK samples:
//   * capabilities/status/supplies/options  -> util.cpp's Get*/Parse* functions
//     over the spooler BidiSpl schemas in DXP01SDK.H.
//   * printing                              -> print.cpp: StartDoc / StartPage /
//     StretchDIBits / escapes via TextOut, with StartJob before spooling and
//     EndJob after WaitUntilJobSpooled, then PollForJobCompletion.
// ---------------------------------------------------------------------------
#include "printing/EntrustXpsPrinter.h"

#include "printing/PrinterTypes.h"
#include "utils/GdiPrintSurface.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QXmlStreamReader>
#include <QThread>

#include <utility>

namespace occ {

// Defined in PrinterTypes.cpp; declared here so the backends share one mapping.
QString jobStateToString(JobState state);
JobState jobStateFromString(const QString &text);

namespace {

// --------------------------------------------------------------------------
// A tiny, shape tolerant reader for the two XML layouts the driver can return:
//
//   flat    :  <PrinterInfo2><PrinterStatus>Ready</PrinterStatus>...</PrinterInfo2>
//              (what the SDK samples parse with XPath "PrinterInfo2/PrinterStatus")
//   property:  <Property name="PrinterStatus"><Value>Ready</Value></Property>
//              (the generic BidiSpl envelope)
//
// Both end up in one lower-cased key -> value map, so lookups are uniform. This
// is deliberately not XPath: malformed input must be rejected without throwing.
// --------------------------------------------------------------------------
using PropMap = QHash<QString, QString>;

bool collectProps(const QString &xml, PropMap *props, QString *error)
{
    props->clear();

    struct Node
    {
        QString name;
        QString text;
        QString propName;
    };
    QVector<Node> stack;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();

        if (reader.isStartElement()) {
            Node node;
            node.name = reader.name().toString();
            if (node.name.compare(QLatin1String("Property"), Qt::CaseInsensitive) == 0) {
                node.propName = reader.attributes().value(QLatin1String("name")).toString();
            }
            stack.append(node);
            continue;
        }

        if (reader.isCharacters()) {
            if (!stack.isEmpty() && !reader.isWhitespace())
                stack.last().text += reader.text().toString();
            continue;
        }

        if (reader.isEndElement()) {
            if (stack.isEmpty())
                continue;
            const Node node = stack.takeLast();
            const QString text = node.text.trimmed();
            const QString key = node.name.toLower();
            if (!key.isEmpty())
                props->insert(key, text);

            if (node.name.compare(QLatin1String("Value"), Qt::CaseInsensitive) == 0
                && !stack.isEmpty()) {
                const Node &parent = stack.last();
                if (parent.name.compare(QLatin1String("Property"), Qt::CaseInsensitive) == 0
                    && !parent.propName.isEmpty()) {
                    props->insert(parent.propName.toLower(), text);
                }
            }
            if (node.name.compare(QLatin1String("Property"), Qt::CaseInsensitive) == 0
                && !node.propName.isEmpty() && !text.isEmpty()) {
                props->insert(node.propName.toLower(), text);
            }
        }
    }

    if (reader.hasError()) {
        if (error)
            *error = QStringLiteral(
                "The printer returned status data that is not valid XML (%1).")
                         .arg(reader.errorString());
        return false;
    }
    if (props->isEmpty()) {
        if (error)
            *error = QStringLiteral("The printer returned no status data.");
        return false;
    }
    return true;
}

QString propValue(const PropMap &props, const QString &key)
{
    return props.value(key.toLower());
}

bool propHas(const PropMap &props, const QString &key)
{
    return props.contains(key.toLower());
}

// Returns `fallback` when the property is absent or not a number.
int propInt(const PropMap &props, const QString &key, int fallback = -1)
{
    if (!props.contains(key.toLower()))
        return fallback;
    const QString text = props.value(key.toLower()).trimmed();
    if (text.isEmpty())
        return fallback;
    bool ok = false;
    const int value = text.toInt(&ok);
    return ok ? value : fallback;
}

// A capability flag: present-and-not-"None" means supported; absent means the
// device did not report it (so `known` stays false).
void applyFlag(const PropMap &props, const QString &key, bool *value, bool *known)
{
    if (!propHas(props, key)) {
        *known = false;
        return;
    }
    const QString text = propValue(props, key).trimmed();
    *known = true;
    *value = !text.isEmpty() && text.compare(QLatin1String("None"), Qt::CaseInsensitive) != 0;
}

} // namespace

// --------------------------------------------------------------------------
// Construction
// --------------------------------------------------------------------------
EntrustXpsPrinter::EntrustXpsPrinter(const QString &printerName)
    : m_printerName(printerName)
{
    m_identity.name = printerName;
    m_identity.backend = PrinterBackendKind::EntrustXps;
}

EntrustXpsPrinter::~EntrustXpsPrinter()
{
    disconnect();
}

// --------------------------------------------------------------------------
// connect / disconnect
// --------------------------------------------------------------------------
bool EntrustXpsPrinter::connect()
{
    m_lastError.clear();
    m_lastTechnical.clear();
    m_connected = false;
    m_bidiAvailable = false;

    if (m_printerName.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("No card printer was selected.");
        m_lastTechnical = QStringLiteral("EntrustXpsPrinter::connect(): empty printer name");
        return false;
    }

    QString error;
    if (!m_bidi.open(m_printerName, &error)) {
        m_lastError = QStringLiteral(
                          "The card printer \"%1\" could not be reached through the print "
                          "spooler. Check that the XPS Card Printer driver is installed for "
                          "this queue and that the printer is switched on.")
                          .arg(m_printerName);
        m_lastTechnical = QStringLiteral("BidiClient::open(\"%1\"): %2")
                              .arg(m_printerName, m_bidi.lastTechnicalDetail().isEmpty()
                                                       ? error
                                                       : m_bidi.lastTechnicalDetail());
        return false;
    }

    // Record the driver's capabilities up front; the queue is connected even if
    // some optional schemas are unsupported.
    m_connected = true;

    QString version;
    if (m_bidi.looksLikeCardPrinter(&version)) {
        m_bidiAvailable = true;
        m_sdkVersion = version;
    } else {
        // Bidi works but the driver did not answer a card-printer schema. Keep the
        // connection but say so plainly rather than pretending.
        m_bidiAvailable = false;
        m_lastTechnical = QStringLiteral("Card printer schemas unavailable: %1")
                              .arg(m_bidi.lastTechnicalDetail());
    }

    // Best effort identity refresh (never fatal).
    if (m_bidiAvailable) {
        QString xml;
        if (m_bidi.query(BidiClient::schemaPrinterOptions(), &xml, nullptr)) {
            QString parseError;
            parsePrinterOptionsXml(xml, &m_identity, nullptr, &parseError);
        }
        // The driver name/port come from the spooler, which always knows them.
        for (const GdiPrintSurface::PrinterInfo &info : GdiPrintSurface::enumeratePrinters()) {
            if (info.name == m_printerName) {
                m_identity.driverName = info.driverName;
                m_identity.port = info.port;
                if (info.port.startsWith(QLatin1String("USB"), Qt::CaseInsensitive))
                    m_identity.connection = PrinterConnection::Usb;
                else if (info.port.startsWith(QLatin1String("IP_"), Qt::CaseInsensitive)
                         || info.port.contains(QLatin1Char('.')))
                    m_identity.connection = PrinterConnection::Network;
                else if (info.port.startsWith(QLatin1String("COM"), Qt::CaseInsensitive))
                    m_identity.connection = PrinterConnection::Serial;
                else if (info.port.startsWith(QLatin1String("LPT"), Qt::CaseInsensitive))
                    m_identity.connection = PrinterConnection::Parallel;
                break;
            }
        }
    }

    return true;
}

void EntrustXpsPrinter::disconnect()
{
    m_bidi.close();
    m_connected = false;
    m_bidiAvailable = false;
}

bool EntrustXpsPrinter::ensureConnected()
{
    if (m_connected && m_bidi.isOpen())
        return true;
    return connect();
}

PrinterIdentity EntrustXpsPrinter::identity()
{
    if (!m_connected)
        ensureConnected();
    if (m_identity.name.isEmpty())
        m_identity.name = m_printerName;
    m_identity.backend = PrinterBackendKind::EntrustXps;
    return m_identity;
}

// --------------------------------------------------------------------------
// parsePrinterOptionsXml - the \Printer.PrinterOptions2:Read document
// (<PrinterInfo2> in the SDK sample). Fields the device omits keep their
// "not available" defaults and their *Known flags stay false.
// --------------------------------------------------------------------------
bool EntrustXpsPrinter::parsePrinterOptionsXml(const QString &xml, PrinterIdentity *id,
                                               PrinterStatus *st, QString *error)
{
    if (error)
        error->clear();

    PropMap props;
    QString parseError;
    if (!collectProps(xml, &props, &parseError)) {
        if (error)
            *error = parseError;
        return false;
    }

    const bool looksLikeOptions =
        propHas(props, "PrinterModel") || propHas(props, "PrinterStatus")
        || propHas(props, "PrintHead") || props.contains(QStringLiteral("printerinfo2"));
    if (!looksLikeOptions) {
        if (error)
            *error = QStringLiteral(
                "The data is not an XPS Card Printer options document.");
        return false;
    }

    if (id) {
        id->backend = PrinterBackendKind::EntrustXps;
        if (propHas(props, "PrinterModel"))
            id->model = propValue(props, "PrinterModel");
        if (propHas(props, "PrinterSerialNumber"))
            id->serialNumber = propValue(props, "PrinterSerialNumber");
        if (propHas(props, "PrinterVersion"))
            id->firmwareVersion = propValue(props, "PrinterVersion");
        if (propHas(props, "ColorPrintResolution"))
            id->colorResolution = propValue(props, "ColorPrintResolution");
        if (propHas(props, "MonochromePrintResolution"))
            id->monochromeResolution = propValue(props, "MonochromePrintResolution");

        applyFlag(props, "OptionDuplex", &id->duplexSupported, &id->duplexKnown);
        applyFlag(props, "PrintHead", &id->hasPrintHead, &id->printHeadKnown);
        applyFlag(props, "Laminator", &id->hasLaminator, &id->laminatorKnown);
        applyFlag(props, "ModuleEmbosser", &id->hasEmbosser, &id->embosserKnown);
        applyFlag(props, "OptionSmartcard", &id->hasSmartcard, &id->smartcardKnown);
        applyFlag(props, "OptionPrinterBarcodeReader", &id->hasBarcodeReader,
                  &id->barcodeReaderKnown);

        if (propHas(props, "OptionMagstripe")) {
            id->magstripeKnown = true;
            const QString stripe = propValue(props, "OptionMagstripe");
            id->hasMagstripe = stripe.contains(QLatin1String("ISO"), Qt::CaseInsensitive)
                               || stripe.compare(QLatin1String("JIS"),
                                                 Qt::CaseInsensitive) == 0;
        }

        if (propHas(props, "ConnectionPortType")) {
            const QString port = propValue(props, "ConnectionPortType").toLower();
            if (port.contains(QLatin1String("usb")))
                id->connection = PrinterConnection::Usb;
            else if (port.contains(QLatin1String("network")))
                id->connection = PrinterConnection::Network;
            else if (port.contains(QLatin1String("serial")))
                id->connection = PrinterConnection::Serial;
            else if (port.contains(QLatin1String("parallel")))
                id->connection = PrinterConnection::Parallel;
        }
    }

    if (st) {
        const QString statusText = propValue(props, "PrinterStatus");
        st->stateText = statusText;
        st->iBidiAvailable = true;
        if (statusText.compare(QLatin1String("Ready"), Qt::CaseInsensitive) == 0)
            st->state = PrinterState::Ready;
        else if (statusText.compare(QLatin1String("Busy"), Qt::CaseInsensitive) == 0)
            st->state = PrinterState::Busy;
        else if (!statusText.isEmpty())
            st->state = PrinterState::NotAvailable;   // reported, but not a known state
    }

    return true;
}

// --------------------------------------------------------------------------
// parseSuppliesXml - the \Printer.SuppliesStatus3:Read document
// (<PrinterSupplies3>). Element names are taken from the SDK's ParseSuppliesXML.
// --------------------------------------------------------------------------
bool EntrustXpsPrinter::parseSuppliesXml(const QString &xml, PrinterStatus *st,
                                         QString *error)
{
    if (error)
        error->clear();

    PropMap props;
    QString parseError;
    if (!collectProps(xml, &props, &parseError)) {
        if (error)
            *error = parseError;
        return false;
    }

    const bool looksLikeSupplies =
        propHas(props, "PrinterStatus") || propHas(props, "PrintRibbon")
        || propHas(props, "RibbonRemaining") || propHas(props, "PrintRibbonRemaining")
        || props.contains(QStringLiteral("printersupplies3"));
    if (!looksLikeSupplies) {
        if (error)
            *error = QStringLiteral("The data is not an XPS Card Printer supplies document.");
        return false;
    }

    if (!st)
        return true;

    const QString installed = QStringLiteral("Installed");
    auto isInstalled = [&installed](const QString &text) {
        return text.compare(installed, Qt::CaseInsensitive) == 0;
    };

    // Print ribbon.
    {
        RibbonSupply &ribbon = st->printRibbon;
        const int remaining = propHas(props, "RibbonRemaining")
                                  ? propInt(props, "RibbonRemaining", -1)
                                  : propInt(props, "PrintRibbonRemaining", -1);
        ribbon.available = isInstalled(propValue(props, "PrintRibbon")) || remaining >= 0;
        ribbon.type = propValue(props, "PrintRibbonType");
        ribbon.serialNumber = propValue(props, "RibbonSerialNumber");
        ribbon.lotCode = propValue(props, "RibbonLotCode");
        ribbon.partNumber = propInt(props, "RibbonPartNumber", 0);
        ribbon.percentRemaining = remaining;
    }

    // Top coat ribbon (reported by the embosser section of the document).
    {
        RibbonSupply &ribbon = st->topcoatRibbon;
        const int remaining = propInt(props, "TopperRibbonRemaining", -1);
        ribbon.available = isInstalled(propValue(props, "TopperRibbon")) || remaining >= 0;
        ribbon.type = propValue(props, "TopperRibbonType");
        ribbon.serialNumber = propValue(props, "TopperRibbonSerialNumber");
        ribbon.lotCode = propValue(props, "TopperRibbonLotCode");
        ribbon.partNumber = propInt(props, "TopperRibbonPartNumber", 0);
        ribbon.percentRemaining = remaining;
    }

    // Retransfer film (retransfer printers only).
    {
        RibbonSupply &film = st->retransferFilm;
        const int remaining = propInt(props, "RetransferFilmRemaining", -1);
        film.available = isInstalled(propValue(props, "RetransferFilm")) || remaining >= 0;
        film.type = QStringLiteral("Retransfer film");
        film.serialNumber = propValue(props, "RetransferFilmSerialNumber");
        film.lotCode = propValue(props, "RetransferFilmLotCode");
        film.partNumber = propInt(props, "RetransferFilmPartNumber", 0);
        film.percentRemaining = remaining;
    }

    // Laminate L1 / L2.
    {
        LaminatorSupply &l1 = st->laminatorL1;
        const QString code = propValue(props, "L1Laminate");
        l1.available = !code.isEmpty()
                       && code.compare(QLatin1String("None"), Qt::CaseInsensitive) != 0;
        l1.percentRemaining = propInt(props, "L1LaminateRemaining", -1);
        l1.lotCode = propValue(props, "L1LaminateLotCode");
        l1.serialNumber = propValue(props, "L1LaminateSerialNumber");
    }
    {
        LaminatorSupply &l2 = st->laminatorL2;
        const QString code = propValue(props, "L2Laminate");
        l2.available = !code.isEmpty()
                       && code.compare(QLatin1String("None"), Qt::CaseInsensitive) != 0;
        l2.percentRemaining = propInt(props, "L2LaminateRemaining", -1);
        l2.lotCode = propValue(props, "L2LaminateLotCode");
        l2.serialNumber = propValue(props, "L2LaminateSerialNumber");
    }

    return true;
}

// --------------------------------------------------------------------------
// parsePrinterStatusXml - the \Printer.PrintMessages:Read document
// (<PrinterStatus>): ClientID / PrinterJobID / ErrorCode / ErrorSeverity /
// ErrorString. A non-zero error code is not interpreted here; status() decides
// what it means for the overall state.
// --------------------------------------------------------------------------
bool EntrustXpsPrinter::parsePrinterStatusXml(const QString &xml, PrinterStatus *st,
                                              QString *error)
{
    if (error)
        error->clear();

    PropMap props;
    QString parseError;
    if (!collectProps(xml, &props, &parseError)) {
        if (error)
            *error = parseError;
        return false;
    }

    const bool looksLikeStatus = propHas(props, "ErrorCode") || propHas(props, "PrinterJobID")
                                 || propHas(props, "ErrorSeverity")
                                 || props.contains(QStringLiteral("printerstatus"));
    if (!looksLikeStatus) {
        if (error)
            *error = QStringLiteral("The data is not an XPS Card Printer status document.");
        return false;
    }

    if (!st)
        return true;

    st->errorCode = propInt(props, "ErrorCode", 0);
    st->severity = propInt(props, "ErrorSeverity", 0);
    st->errorString = propValue(props, "ErrorString");
    st->iBidiAvailable = true;

    const QString clientId = propValue(props, "ClientID");
    const int printerJobId = propInt(props, "PrinterJobID", 0);
    if (!clientId.isEmpty())
        st->messages << QStringLiteral("ClientID %1").arg(clientId);
    if (printerJobId > 0)
        st->messages << QStringLiteral("PrinterJobID %1").arg(printerJobId);

    return true;
}

// --------------------------------------------------------------------------
// parseJobStatusXml - the \Printer.JobStatus:Read document (<JobStatus>).
// --------------------------------------------------------------------------
bool EntrustXpsPrinter::parseJobStatusXml(const QString &xml, JobState *state,
                                          int *windowsJobId, QString *error)
{
    if (error)
        error->clear();
    if (state)
        *state = JobState::NotAvailable;
    if (windowsJobId)
        *windowsJobId = 0;

    PropMap props;
    QString parseError;
    if (!collectProps(xml, &props, &parseError)) {
        if (error)
            *error = parseError;
        return false;
    }

    const bool looksLikeJobStatus = propHas(props, "JobState")
                                    || props.contains(QStringLiteral("jobstatus"));
    if (!looksLikeJobStatus) {
        if (error)
            *error = QStringLiteral("The data is not an XPS Card Printer job status document.");
        return false;
    }

    if (state)
        *state = jobStateFromString(propValue(props, "JobState"));
    if (windowsJobId)
        *windowsJobId = propInt(props, "WindowsJobID", 0);
    return true;
}

// --------------------------------------------------------------------------
// topcoatFromString - the SDK print sample's -t / -u values.
// --------------------------------------------------------------------------
TopcoatPreset EntrustXpsPrinter::topcoatFromString(const QString &text)
{
    const QString s = text.trimmed().toLower();
    if (s.isEmpty() || s == QLatin1String("default") || s == QLatin1String("driverdefault"))
        return TopcoatPreset::DriverDefault;
    if (s == QLatin1String("all"))
        return TopcoatPreset::All;
    if (s == QLatin1String("except"))
        return TopcoatPreset::Except;
    if (s == QLatin1String("chip") || s == QLatin1String("iso7816")
        || s == QLatin1String("iso_7816"))
        return TopcoatPreset::Iso7816;
    if (s == QLatin1String("mag2") || s == QLatin1String("iso2"))
        return TopcoatPreset::Iso2Track;
    if (s == QLatin1String("mag3") || s == QLatin1String("iso3"))
        return TopcoatPreset::Iso3Track;
    if (s == QLatin1String("magjis") || s == QLatin1String("jis"))
        return TopcoatPreset::Jis;
    return TopcoatPreset::DriverDefault;
}

// --------------------------------------------------------------------------
// topcoatEscapes
//
// Command syntax and the two commands ("~TA%" top coat add, "~PB%" print
// blocking) are exactly the SDK print sample's
// WriteCustomTopcoatBlockingEscapesFront/Back(); the sample writes them through
// TextOut on the print DC. Coordinates are millimetres on a landscape ID-1 card
// (85.6 x 54.0 mm, origin top-left).
//
// The per-preset region geometry follows the standard ISO/IEC 7811 magnetic
// stripe zones and the ISO/IEC 7816 contact area. As with the SDK sample's
// "custom" escapes, the exact zones must be confirmed on real hardware before
// production use.
// --------------------------------------------------------------------------
QString EntrustXpsPrinter::topcoatEscapes(TopcoatPreset preset, bool frontSide)
{
    Q_UNUSED(frontSide);
    if (preset == TopcoatPreset::DriverDefault)
        return QString();

    const QString wholeCard = QStringLiteral("0 0 85.6 54");
    const QString iso7816   = QStringLiteral("10.25 10.25 19.87 19.87");
    const QString iso2Track = QStringLiteral("0 5.54 85.6 5.58");
    const QString iso3Track = QStringLiteral("0 5.54 85.6 8.63");
    const QString jisTrack  = QStringLiteral("0 6.30 85.6 2.79");

    QString blocking;
    switch (preset) {
    case TopcoatPreset::All:           blocking.clear();      break;
    case TopcoatPreset::Except:        blocking = iso7816;    break;
    case TopcoatPreset::Iso7816:       blocking = iso7816;    break;
    case TopcoatPreset::Iso2Track:     blocking = iso2Track;  break;
    case TopcoatPreset::Iso3Track:     blocking = iso3Track;  break;
    case TopcoatPreset::Jis:           blocking = jisTrack;   break;
    case TopcoatPreset::DriverDefault: return QString();
    }

    QString escape = QStringLiteral("~TA%%1?").arg(wholeCard);
    if (!blocking.isEmpty())
        escape += QStringLiteral("~PB%%1;").arg(blocking);
    return escape;
}

namespace {

// <HopperStatus><HopperInformation ... Status="Full"/></HopperStatus>
// `hopperIndex` is 1-based, matching the SDK's GetHopperIndex().
QString hopperStatusValue(const QString &xml, int hopperIndex)
{
    QXmlStreamReader reader(xml);
    int index = 0;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()
            && reader.name().compare(QLatin1String("HopperInformation"),
                                     Qt::CaseInsensitive) == 0) {
            const QString status =
                reader.attributes().value(QLatin1String("Status")).toString();
            ++index;
            if (index == hopperIndex)
                return status;
        }
    }
    return QString();
}

} // namespace

// --------------------------------------------------------------------------
// status - reads the driver's schemas in the documented order and degrades
// gracefully: a schema the device does not answer simply leaves its fields at
// "not available".
// --------------------------------------------------------------------------
PrinterStatus EntrustXpsPrinter::status()
{
    PrinterStatus result;
    result.updatedAt = QDateTime::currentDateTime();

    if (!ensureConnected()) {
        result.state = PrinterState::NotAvailable;
        result.iBidiAvailable = false;
        result.detail = QStringLiteral("Not available: %1").arg(m_lastError);
        return result;
    }

    if (!m_bidiAvailable) {
        result.state = PrinterState::NotAvailable;
        result.iBidiAvailable = false;
        result.detail = QStringLiteral(
            "The XPS Card Printer driver was not detected on this print queue: the "
            "print spooler does not expose the card printer's status interface "
            "(BidiSpl). Install or repair the XPS Card Printer Driver for this queue.");
        return result;
    }

    result.iBidiAvailable = true;
    QString xml;

    // 1. Capabilities, model, serial, status text.
    if (m_bidi.query(BidiClient::schemaPrinterOptions(), &xml, nullptr)) {
        QString parseError;
        parsePrinterOptionsXml(xml, &m_identity, &result, &parseError);
    }

    // 2. Colour mode.
    {
        PropMap props;
        if (m_bidi.query(BidiClient::schemaPrinterOptions3(), &xml, nullptr)
            && collectProps(xml, &props, nullptr)) {
            m_colorMode = propValue(props, "PrinterColorMode");
        }
    }

    // 3. Supplies: print ribbon, top coat, retransfer film, laminate.
    if (m_bidi.query(BidiClient::schemaSupplies(), &xml, nullptr))
        parseSuppliesXml(xml, &result, nullptr);

    // 4. Device messages and the current error code / severity.
    if (m_bidi.query(BidiClient::schemaPrintMessages(), &xml, nullptr))
        parsePrinterStatusXml(xml, &result, nullptr);

    // 5. SDK (driver) version.
    {
        PropMap props;
        if (m_bidi.query(BidiClient::schemaSdkVersion(), &xml, nullptr)) {
            if (collectProps(xml, &props, nullptr)) {
                const QString version = propValue(props, "SDKVersion");
                m_sdkVersion = version.isEmpty() ? xml.trimmed() : version;
            } else {
                m_sdkVersion = xml.trimmed();
            }
        }
        result.sdkVersion = m_sdkVersion;
    }

    // 6. Input hopper status (optional; some models do not implement it).
    if (m_bidi.query(BidiClient::schemaHopperStatus(), &xml, nullptr)) {
        const QString hopper = hopperStatusValue(xml, 1);
        if (!hopper.isEmpty()) {
            result.hopperStatusAvailable = true;
            result.inputHopperStatus = hopper;
        }
    }

    // ---- interpret, without guessing -------------------------------------
    if (result.errorCode != 0 && result.severity <= 3) {
        result.state = PrinterState::Error;
        result.detail = QStringLiteral("The printer reported error %1: %2")
                            .arg(result.errorCode)
                            .arg(result.errorString.isEmpty()
                                     ? QStringLiteral("(no description)")
                                     : result.errorString);
    } else if (result.state == PrinterState::Ready) {
        result.detail = QStringLiteral("The printer is ready.");
    } else if (result.state == PrinterState::Busy) {
        result.detail = QStringLiteral("The printer is busy.");
    } else if (result.state == PrinterState::NotQueried) {
        result.state = PrinterState::NotAvailable;
        result.detail = QStringLiteral(
            "The printer driver did not report a status for this queue.");
    } else {
        result.detail = QStringLiteral("The printer reported: %1")
                            .arg(result.stateText.isEmpty()
                                     ? QStringLiteral("no status")
                                     : result.stateText);
    }

    result.updatedAt = QDateTime::currentDateTime();
    return result;
}

int EntrustXpsPrinter::printerJobIdFromStatusXml(const QString &xml) const
{
    PropMap props;
    if (!collectProps(xml, &props, nullptr))
        return 0;
    return propInt(props, "PrinterJobID", 0);
}

// --------------------------------------------------------------------------
// pollForCompletion - util::PollForJobCompletion() from the SDK sample: query
// \Printer.JobStatus:Read every two seconds until a terminal state, reporting
// progress on the way.
//
// Note: the SDK additionally supplies the claimed PrinterJobID as request input
// (JOB_STATUS_XML); the frozen BidiClient exposes a schema GET only, so the poll
// queries by schema. Confirm against hardware.
// --------------------------------------------------------------------------
void EntrustXpsPrinter::pollForCompletion(const PrintJob &job, PrintResult &result)
{
    const int timeoutSeconds =
        job.completionTimeoutSeconds > 0 ? job.completionTimeoutSeconds : 180;

    QElapsedTimer timer;
    timer.start();

    JobState state = JobState::NotAvailable;
    for (;;) {
        QString xml;
        if (m_bidi.query(BidiClient::schemaJobStatus(), &xml, nullptr)) {
            if (!parseJobStatusXml(xml, &state, nullptr, nullptr))
                state = JobState::Unknown;
        } else {
            state = JobState::Unknown;
        }

        if (job.progress)
            job.progress(result.printerJobId, state, jobStateToString(state));

        if (state == JobState::Succeeded || state == JobState::Failed
            || state == JobState::Cancelled || state == JobState::CardNotRetrieved
            || state == JobState::NotAvailable)
            break;

        if (timer.elapsed() > qint64(timeoutSeconds) * 1000) {
            state = JobState::Unknown;
            result.error = QStringLiteral(
                               "The printer did not confirm that the card finished "
                               "within %1 seconds.")
                               .arg(timeoutSeconds);
            result.technicalDetail = QStringLiteral(
                "Polling \\Printer.JobStatus:Read timed out for printer job %1")
                                         .arg(result.printerJobId);
            break;
        }

        QThread::msleep(2000);
    }

    result.finalJobState = state;
    result.completed = (state == JobState::Succeeded);
}

// --------------------------------------------------------------------------
// printJob - the real card printing path (mirrors the SDK "print" sample:
// StartJob, then spool, then EndJob, then poll).
// --------------------------------------------------------------------------
PrintResult EntrustXpsPrinter::printJob(const PrintJob &job)
{
    PrintResult result;
    result.simulated = false;
    m_lastError.clear();
    m_lastTechnical.clear();

    auto finish = [this, &result]() -> PrintResult & {
        m_lastResult = result;
        m_lastError = result.error;
        if (!result.technicalDetail.isEmpty())
            m_lastTechnical = result.technicalDetail;
        return result;
    };

    if (!ensureConnected()) {
        result.error = QStringLiteral("Cannot print: %1").arg(m_lastError);
        result.technicalDetail = m_lastTechnical;
        return finish();
    }
    if (!m_bidiAvailable) {
        result.error = QStringLiteral(
            "Cannot print: the XPS Card Printer driver is not available on this queue.");
        result.technicalDetail = m_lastTechnical;
        return finish();
    }
    if (!job.frontEnabled && !job.backEnabled) {
        result.error =
            QStringLiteral("There is nothing to print: both card sides are disabled.");
        return finish();
    }
    if (job.frontEnabled && job.frontImage.isNull()) {
        result.error = QStringLiteral("The front of the card has no image.");
        return finish();
    }
    if (job.backEnabled && job.backImage.isNull()) {
        result.error = QStringLiteral("The back of the card has no image.");
        return finish();
    }
    if (job.backEnabled && !job.duplex) {
        result.error = QStringLiteral(
            "The back of the card was requested but the job is not two-sided.");
        return finish();
    }

    // 1. Claim a printer job id BEFORE any data is spooled (SDK print sample).
    {
        const QString payload = BidiClient::startJobXml(job.hopperId, job.cardEjectSide);
        QString setError;
        if (!m_bidi.set(BidiClient::schemaStartJob(), payload, &setError)) {
            result.error =
                QStringLiteral("The printer would not start the card job: %1").arg(setError);
            result.technicalDetail = m_bidi.lastTechnicalDetail();
            return finish();
        }
    }
    {
        QString xml;
        if (m_bidi.query(BidiClient::schemaPrintMessages(), &xml, nullptr))
            result.printerJobId = printerJobIdFromStatusXml(xml);
    }

    // 2. Spool the card image(s) through the GDI print path.
    GdiPrintSurface surface;
    GdiPrintSurface::Options options;
    // Taken from the document, not assumed: a portrait card must print upright.
    options.landscape = job.landscape;
    options.duplex = job.duplex;
    options.resolutionDpi = job.frontDpi > 0 ? job.frontDpi : 300;
    options.documentName = job.documentName.isEmpty() ? QStringLiteral("OpenCardCanvas card")
                                                      : job.documentName;

    QString error;
    QString technical;
    if (!surface.beginDocument(m_printerName, options, &error, &technical)) {
        result.error = error;
        result.technicalDetail = technical;
        if (result.printerJobId > 0)
            cancelJob(result.printerJobId);
        return finish();
    }

    auto abortWith = [&](const QString &message, const QString &detail) -> PrintResult & {
        result.error = message;
        if (!detail.isEmpty())
            result.technicalDetail = detail;
        surface.abort();
        if (result.printerJobId > 0)
            cancelJob(result.printerJobId);
        return finish();
    };

    if (job.frontEnabled) {
        if (!surface.drawPage(job.frontImage, job.frontDpi, &error, &technical))
            return abortWith(error, technical);
        const QString escape = topcoatEscapes(job.frontTopcoat, true);
        if (!escape.isEmpty() && !surface.sendEscape(escape, &error))
            return abortWith(error, QString());
    }
    if (job.backEnabled) {
        if (!surface.resetDevice(true, &error))
            return abortWith(error, QString());
        if (!surface.drawPage(job.backImage, job.backDpi, &error, &technical))
            return abortWith(error, technical);
        const QString escape = topcoatEscapes(job.backTopcoat, false);
        if (!escape.isEmpty() && !surface.sendEscape(escape, &error))
            return abortWith(error, QString());
    }

    unsigned long windowsJobId = 0;
    if (!surface.endDocument(&windowsJobId, &error, &technical))
        return abortWith(error, technical);
    result.windowsJobId = windowsJobId;

    // 3. Wait until the spooler holds all the data, then close the card job.
    (void)GdiPrintSurface::waitUntilJobSpooled(m_printerName, windowsJobId,
                                               job.completionTimeoutSeconds * 1000, nullptr);
    m_bidi.send(BidiClient::schemaEndJob(), nullptr);

    // 4. Poll for completion.
    if (job.waitForCompletion) {
        pollForCompletion(job, result);
    } else {
        result.finalJobState = JobState::Unknown;
        result.completed = false;
    }

    // Always try to capture the device's own error description.
    {
        QString xml;
        if (m_bidi.query(BidiClient::schemaPrintMessages(), &xml, nullptr)) {
            PrinterStatus deviceStatus;
            if (parsePrinterStatusXml(xml, &deviceStatus, nullptr)) {
                result.errorCode = deviceStatus.errorCode;
                result.errorString = deviceStatus.errorString;
            }
        }
    }

    // 5. Only the device may declare success.
    if (result.finalJobState == JobState::Succeeded) {
        result.ok = true;
        result.completed = true;
    } else {
        result.ok = false;
        if (!job.waitForCompletion) {
            result.error = QStringLiteral(
                "The card job was sent to the printer but completion was not verified "
                "(waiting for completion is disabled).");
        } else if (result.error.isEmpty()) {
            result.error = QStringLiteral("The card was not printed: %1.")
                               .arg(jobStateToString(result.finalJobState));
            if (result.errorCode != 0) {
                result.error += QStringLiteral(" The printer reported error %1: %2")
                                    .arg(result.errorCode)
                                    .arg(result.errorString.isEmpty()
                                             ? QStringLiteral("(no description)")
                                             : result.errorString);
            }
        }
    }

    return finish();
}

bool EntrustXpsPrinter::print(const PrintJob &job)
{
    return printJob(job).ok;
}

// --------------------------------------------------------------------------
// cancelJob - \Printer.Action:Set with the Cancel action (SDK
// util::CancelJob()); the error code supplied alongside clears that error.
// --------------------------------------------------------------------------
bool EntrustXpsPrinter::cancelJob(int printerJobId)
{
    if (!ensureConnected()) {
        m_lastError = QStringLiteral("Cannot cancel: %1").arg(m_lastError);
        return false;
    }
    if (!m_bidiAvailable) {
        m_lastError = QStringLiteral(
            "Cannot cancel: the XPS Card Printer driver is not available on this queue.");
        return false;
    }
    if (printerJobId <= 0) {
        m_lastError = QStringLiteral("There is no card job to cancel.");
        return false;
    }

    const QString payload =
        BidiClient::printerActionXml(BidiClient::Cancel, printerJobId, 0);
    QString error;
    if (!m_bidi.set(BidiClient::schemaPrinterAction(), payload, &error)) {
        m_lastError = QStringLiteral("The card job could not be cancelled: %1").arg(error);
        m_lastTechnical = m_bidi.lastTechnicalDetail();
        return false;
    }
    return true;
}

// --------------------------------------------------------------------------
// diagnosticsReport - a plain, multi line report for the Diagnostics dialog.
// Every value the device does not expose reads "Not available" instead of a
// guess.
// --------------------------------------------------------------------------
QString EntrustXpsPrinter::diagnosticsReport()
{
    const QString notAvailable = QStringLiteral("Not available");

    if (!m_connected)
        ensureConnected();

    const PrinterStatus st = m_connected && m_bidiAvailable ? status() : PrinterStatus();

    auto flagText = [&notAvailable](bool supported, bool known) {
        if (!known)
            return notAvailable;
        return supported ? QStringLiteral("Installed") : QStringLiteral("Not installed");
    };
    auto ribbonText = [&notAvailable](const RibbonSupply &ribbon) {
        if (!ribbon.available)
            return notAvailable;
        QString text = ribbon.percentRemaining >= 0
                           ? QStringLiteral("%1% remaining").arg(ribbon.percentRemaining)
                           : QStringLiteral("Installed (level not reported)");
        if (!ribbon.type.isEmpty())
            text += QStringLiteral(" - %1").arg(ribbon.type);
        return text;
    };
    auto laminateText = [&notAvailable](const LaminatorSupply &supply) {
        if (!supply.available)
            return notAvailable;
        return supply.percentRemaining >= 0
                   ? QStringLiteral("%1% remaining").arg(supply.percentRemaining)
                   : QStringLiteral("Installed (level not reported)");
    };

    QStringList lines;
    lines << QStringLiteral("XPS Card Printer driver backend");
    lines << QStringLiteral("Printer               : %1")
                 .arg(m_printerName.isEmpty() ? notAvailable : m_printerName);
    lines << QStringLiteral("Connected             : %1")
                 .arg(m_connected ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QStringLiteral("BidiSpl status channel: %1")
                 .arg(m_bidiAvailable ? QStringLiteral("Available")
                                      : QStringLiteral("NOT available - XPS Card Printer "
                                                       "driver not detected"));
    lines << QStringLiteral("Printer driver        : %1")
                 .arg(m_identity.driverName.isEmpty() ? notAvailable : m_identity.driverName);
    lines << QStringLiteral("Driver version        : %1")
                 .arg(m_identity.driverVersion.isEmpty() ? notAvailable
                                                         : m_identity.driverVersion);
    lines << QStringLiteral("Port                  : %1")
                 .arg(m_identity.port.isEmpty() ? notAvailable : m_identity.port);
    lines << QStringLiteral("SDK (driver) version  : %1")
                 .arg(st.sdkVersion.isEmpty() ? (m_sdkVersion.isEmpty() ? notAvailable
                                                                        : m_sdkVersion)
                                              : st.sdkVersion);
    lines << QStringLiteral("Printer model         : %1")
                 .arg(m_identity.model.isEmpty() ? notAvailable : m_identity.model);
    lines << QStringLiteral("Serial number         : %1")
                 .arg(m_identity.serialNumber.isEmpty() ? notAvailable
                                                        : m_identity.serialNumber);
    lines << QStringLiteral("Firmware version      : %1")
                 .arg(m_identity.firmwareVersion.isEmpty() ? notAvailable
                                                           : m_identity.firmwareVersion);
    lines << QStringLiteral("Colour mode           : %1")
                 .arg(m_colorMode.isEmpty() ? notAvailable : m_colorMode);
    lines << QStringLiteral("Colour resolution     : %1")
                 .arg(m_identity.colorResolution.isEmpty() ? notAvailable
                                                           : m_identity.colorResolution);
    lines << QStringLiteral("Monochrome resolution : %1")
                 .arg(m_identity.monochromeResolution.isEmpty()
                          ? notAvailable
                          : m_identity.monochromeResolution);
    lines << QStringLiteral("Duplex                : %1")
                 .arg(flagText(m_identity.duplexSupported, m_identity.duplexKnown));
    lines << QStringLiteral("Print head            : %1")
                 .arg(flagText(m_identity.hasPrintHead, m_identity.printHeadKnown));
    lines << QStringLiteral("Laminator             : %1")
                 .arg(flagText(m_identity.hasLaminator, m_identity.laminatorKnown));
    lines << QStringLiteral("Magstripe unit        : %1")
                 .arg(flagText(m_identity.hasMagstripe, m_identity.magstripeKnown));
    lines << QStringLiteral("Smart card unit       : %1")
                 .arg(flagText(m_identity.hasSmartcard, m_identity.smartcardKnown));
    lines << QStringLiteral("Embosser              : %1")
                 .arg(flagText(m_identity.hasEmbosser, m_identity.embosserKnown));
    lines << QStringLiteral("Barcode reader        : %1")
                 .arg(flagText(m_identity.hasBarcodeReader, m_identity.barcodeReaderKnown));
    lines << QStringLiteral("Print ribbon          : %1").arg(ribbonText(st.printRibbon));
    lines << QStringLiteral("Top coat ribbon       : %1").arg(ribbonText(st.topcoatRibbon));
    lines << QStringLiteral("Retransfer film       : %1").arg(ribbonText(st.retransferFilm));
    lines << QStringLiteral("Laminate L1           : %1").arg(laminateText(st.laminatorL1));
    lines << QStringLiteral("Laminate L2           : %1").arg(laminateText(st.laminatorL2));
    lines << QStringLiteral("Input hopper          : %1")
                 .arg(st.hopperStatusAvailable && !st.inputHopperStatus.isEmpty()
                          ? st.inputHopperStatus
                          : notAvailable);
    if (!m_lastError.isEmpty())
        lines << QStringLiteral("Last error            : %1").arg(m_lastError);
    if (!m_lastTechnical.isEmpty())
        lines << QStringLiteral("Technical detail      : %1").arg(m_lastTechnical);

    return lines.join(QLatin1Char('\n'));
}

} // namespace occ
