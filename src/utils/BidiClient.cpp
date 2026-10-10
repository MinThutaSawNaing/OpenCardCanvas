// ---------------------------------------------------------------------------
// BidiClient - implementation of the print spooler BidiSpl COM channel.
//
// The request/response sequence implemented here mirrors the XPS Card Printer
// Driver SDK samples (samples/cpp/common/util.cpp):
//
//   CoInitializeEx -> CoCreateInstance(BidiSpl) -> IBidiSpl::BindDevice
//   per operation:    CoCreateInstance(BidiRequest)
//                     IBidiRequest::SetSchema(<schema>)
//                     IBidiSpl::SendRecv(BIDI_ACTION_GET|SET, request)
//                     IBidiRequest::GetResult()
//                     IBidiRequest::GetOutputData()
//   done:             CoUninitialize
//
// There is no import library for the BidiSpl type library in this SDK, so the
// coclasses are instantiated through __uuidof(BidiSpl) / __uuidof(BidiRequest);
// the class UUIDs come from the platform header <bidispl.h>.
// ---------------------------------------------------------------------------
#include "utils/BidiClient.h"

#include <windows.h>
#include <bidispl.h>
#include <winspool.h>
#include <objbase.h>
#include <wrl/client.h>

#include <QByteArray>
#include <QString>

using Microsoft::WRL::ComPtr;

namespace occ {

namespace {

// Human readable text for a Win32 / HRESULT status code (used for the log).
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

// "0xhhhhhhhh <system text>" for technical detail strings.
QString describeHresult(HRESULT hr)
{
    QString s = QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
    const QString msg = win32Message(static_cast<DWORD>(hr));
    if (!msg.isEmpty())
        s += QStringLiteral(" ") + msg;
    return s;
}

// The spooler returns the schema payload as raw UTF-16LE bytes (the SDK samples
// feed them straight into a CString/BSTR). Convert to a QString, tolerating a
// byte-order mark that some driver builds prepend.
QString utf16FromBlob(const BYTE *data, ULONG sizeBytes)
{
    if (!data || sizeBytes == 0)
        return QString();
    const int charCount = int(sizeBytes / sizeof(char16_t));
    if (charCount <= 0)
        return QString();
    QByteArray raw(reinterpret_cast<const char *>(data), int(sizeBytes));
    QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData()),
                                      charCount);
    if (!text.isEmpty() && text.at(0) == QChar(0xFEFF))
        text.remove(0, 1);
    return text;
}

// RAII wrapper for the CoTaskMem memory returned by IBidiRequest::GetOutputData.
class ComMem
{
public:
    ComMem() = default;
    ~ComMem()
    {
        if (m_schema)
            ::CoTaskMemFree(m_schema);
        if (m_data)
            ::CoTaskMemFree(m_data);
    }
    ComMem(const ComMem &) = delete;
    ComMem &operator=(const ComMem &) = delete;

    LPWSTR *schemaSlot() { return &m_schema; }
    BYTE **dataSlot() { return &m_data; }
    const BYTE *data() const { return m_data; }

private:
    LPWSTR m_schema = nullptr;
    BYTE  *m_data = nullptr;
};

} // namespace

// --------------------------------------------------------------------------
// BidiClient::Impl
// --------------------------------------------------------------------------
struct BidiClient::Impl
{
    ComPtr<IBidiSpl> spl;
    bool    comInitialized = false;   // true when *we* must call CoUninitialize
    QString deviceName;
};

namespace {
// Defined at the end of this file; shared by set() and send().
bool runSetRequest(IBidiSpl *spl, const QString &schema, IBidiRequest *request,
                   QString *userError, QString *technical);
} // namespace

BidiClient::BidiClient()
    : m_impl(std::make_unique<Impl>())
{
}

BidiClient::~BidiClient()
{
    close();
}

// --------------------------------------------------------------------------
// open / close
// --------------------------------------------------------------------------
bool BidiClient::open(const QString &printerName, QString *error)
{
    close();
    m_lastError.clear();
    m_lastTechnical.clear();

    const QString name = printerName.trimmed();
    if (name.isEmpty()) {
        m_lastError = QStringLiteral("No printer name was provided.");
        m_lastTechnical = QStringLiteral("BidiClient::open(): empty printer name");
        if (error)
            *error = m_lastError;
        return false;
    }

    // The spooler Bidi COM server needs COM. Qt on Windows has usually already
    // initialised the apartment, in which case CoInitializeEx returns S_FALSE
    // (same model) or RPC_E_CHANGED_MODE (different model). Both are fine - we
    // only call CoUninitialize when we were the ones who succeeded.
    const HRESULT hrInit = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hrInit)) {
        m_impl->comInitialized = true;
    } else if (hrInit == RPC_E_CHANGED_MODE) {
        m_impl->comInitialized = false;
    } else {
        m_lastError = QStringLiteral(
            "The printer communication service (COM) could not be started, so the "
            "card printer driver cannot be queried.");
        m_lastTechnical = QStringLiteral("CoInitializeEx: %1").arg(describeHresult(hrInit));
        if (error)
            *error = m_lastError;
        return false;
    }

    ComPtr<IBidiSpl> spl;
    HRESULT hr = ::CoCreateInstance(__uuidof(BidiSpl), nullptr, CLSCTX_ALL,
                                    __uuidof(IBidiSpl),
                                    reinterpret_cast<void **>(spl.GetAddressOf()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral(
            "The print spooler's status interface (BidiSpl) is not available on this "
            "system.");
        m_lastTechnical = QStringLiteral("CoCreateInstance(BidiSpl): %1")
                              .arg(describeHresult(hr));
        if (m_impl->comInitialized) {
            ::CoUninitialize();
            m_impl->comInitialized = false;
        }
        if (error)
            *error = m_lastError;
        return false;
    }

    hr = spl->BindDevice(reinterpret_cast<LPCWSTR>(name.utf16()), BIDI_ACCESS_USER);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral(
                          "The status interface could not attach to the print queue "
                          "\"%1\".")
                          .arg(name);
        m_lastTechnical = QStringLiteral("IBidiSpl::BindDevice(\"%1\"): %2")
                              .arg(name, describeHresult(hr));
        spl.Reset();
        if (m_impl->comInitialized) {
            ::CoUninitialize();
            m_impl->comInitialized = false;
        }
        if (error)
            *error = m_lastError;
        return false;
    }

    m_impl->spl = std::move(spl);
    m_impl->deviceName = name;
    return true;
}

void BidiClient::close()
{
    if (!m_impl)
        return;
    m_impl->spl.Reset();
    m_impl->deviceName.clear();
    if (m_impl->comInitialized) {
        ::CoUninitialize();
        m_impl->comInitialized = false;
    }
}

bool BidiClient::isOpen() const
{
    return m_impl && m_impl->spl != nullptr;
}

// --------------------------------------------------------------------------
// query - BIDI_ACTION_GET
// --------------------------------------------------------------------------
bool BidiClient::query(const QString &schema, QString *xml, QString *error,
                       const QString &inputXml)
{
    if (xml)
        xml->clear();
    m_lastError.clear();
    m_lastTechnical.clear();

    if (!isOpen()) {
        m_lastError = QStringLiteral("Not connected to a printer.");
        m_lastTechnical = QStringLiteral("BidiClient::query(): open() has not been called");
        if (error)
            *error = m_lastError;
        return false;
    }

    ComPtr<IBidiRequest> request;
    HRESULT hr = ::CoCreateInstance(__uuidof(BidiRequest), nullptr, CLSCTX_ALL,
                                    __uuidof(IBidiRequest),
                                    reinterpret_cast<void **>(request.GetAddressOf()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("Could not create a status request.");
        m_lastTechnical = QStringLiteral("CoCreateInstance(BidiRequest): %1")
                              .arg(describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    const QString schemaCopy(schema);
    hr = request->SetSchema(reinterpret_cast<LPCWSTR>(schemaCopy.utf16()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer driver rejected the status request.");
        m_lastTechnical = QStringLiteral("IBidiRequest::SetSchema(\"%1\"): %2")
                              .arg(schema, describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    if (!inputXml.isEmpty()) {
        hr = request->SetInputData(BIDI_BLOB,
                                  reinterpret_cast<const BYTE *>(inputXml.utf16()),
                                  UINT(inputXml.size() * sizeof(char16_t)));
        if (FAILED(hr)) {
            m_lastError = QStringLiteral("The printer status request could not be filled in.");
            m_lastTechnical = QStringLiteral("IBidiRequest::SetInputData: %1").arg(describeHresult(hr));
            if (error)
                *error = m_lastError;
            return false;
        }
    }
    hr = m_impl->spl->SendRecv(BIDI_ACTION_GET, request.Get());
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer did not answer the status request.");
        m_lastTechnical = QStringLiteral("IBidiSpl::SendRecv(Get, \"%1\"): %2")
                              .arg(schema, describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    HRESULT deviceResult = S_OK;
    hr = request->GetResult(&deviceResult);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer did not answer the status request.");
        m_lastTechnical = QStringLiteral("IBidiRequest::GetResult(): %1")
                              .arg(describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }
    if (FAILED(deviceResult)) {
        // The driver reported that it does not support this schema. That is a
        // normal outcome for optional schemas, so the caller can degrade.
        m_lastError = QStringLiteral(
            "The print queue does not support this status request.");
        m_lastTechnical = QStringLiteral("SendRecv(Get, \"%1\") result: %2")
                              .arg(schema, describeHresult(deviceResult));
        if (error)
            *error = m_lastError;
        return false;
    }

    ComMem mem;
    DWORD dataType = 0;
    ULONG dataSize = 0;
    hr = request->GetOutputData(0, mem.schemaSlot(), &dataType, mem.dataSlot(), &dataSize);
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer returned no readable status data.");
        m_lastTechnical = QStringLiteral("IBidiRequest::GetOutputData(): %1")
                              .arg(describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    if (xml)
        *xml = utf16FromBlob(mem.data(), dataSize);
    return true;
}

// --------------------------------------------------------------------------
// set - BIDI_ACTION_SET with an XML payload (StartJob / Action)
// --------------------------------------------------------------------------
bool BidiClient::set(const QString &schema, const QString &inputXml, QString *error,
                     QString *outputXml)
{
    if (outputXml)
        outputXml->clear();
    m_lastError.clear();
    m_lastTechnical.clear();

    if (!isOpen()) {
        m_lastError = QStringLiteral("Not connected to a printer.");
        m_lastTechnical = QStringLiteral("BidiClient::set(): open() has not been called");
        if (error)
            *error = m_lastError;
        return false;
    }

    ComPtr<IBidiRequest> request;
    HRESULT hr = ::CoCreateInstance(__uuidof(BidiRequest), nullptr, CLSCTX_ALL,
                                    __uuidof(IBidiRequest),
                                    reinterpret_cast<void **>(request.GetAddressOf()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("Could not create a printer request.");
        m_lastTechnical = QStringLiteral("CoCreateInstance(BidiRequest): %1")
                              .arg(describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    const QString schemaCopy(schema);
    hr = request->SetSchema(reinterpret_cast<LPCWSTR>(schemaCopy.utf16()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer driver rejected the request.");
        m_lastTechnical = QStringLiteral("IBidiRequest::SetSchema(\"%1\"): %2")
                              .arg(schema, describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    // The payload is UTF-16 without a trailing terminator, exactly as the SDK
    // samples hand it to IBidiRequest::SetInputData(BIDI_BLOB, ...).
    const char16_t *chars = reinterpret_cast<const char16_t *>(inputXml.utf16());
    const QByteArray payload(reinterpret_cast<const char *>(chars),
                             inputXml.size() * int(sizeof(char16_t)));
    hr = request->SetInputData(BIDI_BLOB,
                               reinterpret_cast<const BYTE *>(payload.constData()),
                               UINT(payload.size()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer request could not be filled in.");
        m_lastTechnical = QStringLiteral("IBidiRequest::SetInputData(): %1")
                              .arg(describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    QString userError;
    QString technical;
    const bool ok = runSetRequest(m_impl->spl.Get(), schema, request.Get(),
                                  &userError, &technical);
    m_lastError = userError;
    m_lastTechnical = technical;
    if (!ok && error)
        *error = m_lastError;
    // Some driver versions return the claimed PrinterJobID in StartJob's
    // response, others expose it through PrintMessages. Output is optional:
    // absence of a response must not turn an accepted SET into a failure.
    if (ok && outputXml) {
        ComMem mem;
        DWORD dataType = 0;
        ULONG dataSize = 0;
        if (SUCCEEDED(request->GetOutputData(0, mem.schemaSlot(), &dataType,
                                             mem.dataSlot(), &dataSize)))
            *outputXml = utf16FromBlob(mem.data(), dataSize);
    }
    return ok;
}

// --------------------------------------------------------------------------
// send - BIDI_ACTION_SET with no payload (used by EndJob)
// --------------------------------------------------------------------------
bool BidiClient::send(const QString &schema, QString *error)
{
    m_lastError.clear();
    m_lastTechnical.clear();

    if (!isOpen()) {
        m_lastError = QStringLiteral("Not connected to a printer.");
        m_lastTechnical = QStringLiteral("BidiClient::send(): open() has not been called");
        if (error)
            *error = m_lastError;
        return false;
    }

    ComPtr<IBidiRequest> request;
    HRESULT hr = ::CoCreateInstance(__uuidof(BidiRequest), nullptr, CLSCTX_ALL,
                                    __uuidof(IBidiRequest),
                                    reinterpret_cast<void **>(request.GetAddressOf()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("Could not create a printer request.");
        m_lastTechnical = QStringLiteral("CoCreateInstance(BidiRequest): %1")
                              .arg(describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    const QString schemaCopy(schema);
    hr = request->SetSchema(reinterpret_cast<LPCWSTR>(schemaCopy.utf16()));
    if (FAILED(hr)) {
        m_lastError = QStringLiteral("The printer driver rejected the request.");
        m_lastTechnical = QStringLiteral("IBidiRequest::SetSchema(\"%1\"): %2")
                              .arg(schema, describeHresult(hr));
        if (error)
            *error = m_lastError;
        return false;
    }

    QString userError;
    QString technical;
    const bool ok = runSetRequest(m_impl->spl.Get(), schema, request.Get(),
                                  &userError, &technical);
    m_lastError = userError;
    m_lastTechnical = technical;
    if (!ok && error)
        *error = m_lastError;
    return ok;
}

// --------------------------------------------------------------------------
// runSetRequest - shared implementation of the "set" operations. The frozen
// header declares no such member, so it is a file-local free function that
// receives the already-built request and reports text through out parameters.
// --------------------------------------------------------------------------
namespace {
bool runSetRequest(IBidiSpl *spl, const QString &schema, IBidiRequest *request,
                   QString *userError, QString *technical)
{
    HRESULT hr = spl->SendRecv(BIDI_ACTION_SET, request);
    if (FAILED(hr)) {
        if (userError)
            *userError = QStringLiteral("The printer did not accept the request.");
        if (technical)
            *technical = QStringLiteral("IBidiSpl::SendRecv(Set, \"%1\"): %2")
                             .arg(schema, describeHresult(hr));
        return false;
    }

    HRESULT deviceResult = S_OK;
    hr = request->GetResult(&deviceResult);
    if (FAILED(hr)) {
        if (userError)
            *userError = QStringLiteral("The printer did not accept the request.");
        if (technical)
            *technical = QStringLiteral("IBidiRequest::GetResult(): %1")
                             .arg(describeHresult(hr));
        return false;
    }
    if (FAILED(deviceResult)) {
        if (userError)
            *userError = QStringLiteral("The printer reported a problem with the request.");
        if (technical)
            *technical = QStringLiteral("SendRecv(Set, \"%1\") result: %2")
                             .arg(schema, describeHresult(deviceResult));
        return false;
    }
    return true;
}
} // namespace

// --------------------------------------------------------------------------
// looksLikeCardPrinter - the practical "is this an XPS Card Printer?" test.
// --------------------------------------------------------------------------
bool BidiClient::looksLikeCardPrinter(QString *sdkVersion, QString *error)
{
    if (sdkVersion)
        sdkVersion->clear();
    m_lastError.clear();
    m_lastTechnical.clear();

    if (!isOpen()) {
        m_lastError = QStringLiteral("Not connected to a printer.");
        m_lastTechnical = QStringLiteral("BidiClient::looksLikeCardPrinter(): not open");
        if (error)
            *error = m_lastError;
        return false;
    }

    // The XPS Card Printer driver answers \Printer.SDK:Version. A plain office
    // queue either does not understand the schema or returns an error result.
    QString xml;
    QString detail;
    if (query(schemaSdkVersion(), &xml, &detail)) {
        if (sdkVersion)
            *sdkVersion = xml.trimmed();
        return true;
    }
    const QString sdkDetail = m_lastTechnical;

    // Fall back to the capabilities schema used by the SDK print sample; its
    // reply is always rooted at <PrinterInfo2>.
    if (query(schemaPrinterOptions(), &xml, &detail)
        && xml.contains(QLatin1String("PrinterInfo2"), Qt::CaseInsensitive)) {
        return true;
    }

    m_lastError = QStringLiteral(
        "This print queue is not driven by the XPS Card Printer driver.");
    m_lastTechnical = QStringLiteral("SDK version query: %1; options query: %2")
                          .arg(sdkDetail, m_lastTechnical);
    if (error)
        *error = m_lastError;
    return false;
}

// --------------------------------------------------------------------------
// Schema strings - copied verbatim from the XPS Card Printer Driver SDK header
// DXP01SDK.H (namespace dxp01sdk). Nothing here is invented.
// --------------------------------------------------------------------------
QString BidiClient::schemaStartJob()       { return QStringLiteral("\\Printer.Print:StartJob:Set"); }
QString BidiClient::schemaEndJob()         { return QStringLiteral("\\Printer.Print:EndJob:Set"); }
QString BidiClient::schemaPrinterOptions() { return QStringLiteral("\\Printer.PrinterOptions2:Read"); }
QString BidiClient::schemaPrinterOptions3(){ return QStringLiteral("\\Printer.PrinterOptions3:Read"); }
QString BidiClient::schemaSupplies()       { return QStringLiteral("\\Printer.SuppliesStatus3:Read"); }
QString BidiClient::schemaPrintMessages()  { return QStringLiteral("\\Printer.PrintMessages:Read"); }
QString BidiClient::schemaJobStatus()      { return QStringLiteral("\\Printer.JobStatus:Read"); }
QString BidiClient::schemaSdkVersion()     { return QStringLiteral("\\Printer.SDK:Version"); }
QString BidiClient::schemaPrinterAction()  { return QStringLiteral("\\Printer.Action:Set"); }
QString BidiClient::schemaHopperStatus()   { return QStringLiteral("\\Printer.Hopper:Status:Get"); }

// --------------------------------------------------------------------------
// XML payload builders - the exact templates from DXP01SDK.H
// (STARTJOB_XML / JOB_STATUS_XML / PRINTER_ACTION_XML).
// --------------------------------------------------------------------------
QString BidiClient::startJobXml(const QString &hopperId, const QString &cardEjectSide)
{
    // InputHopperSelection defaults to hopper 1 and CardEjectSide to "Default",
    // matching the defaults in the SDK print sample's StartJob().
    const QString hopper = hopperId.trimmed().isEmpty() ? QStringLiteral("1") : hopperId;
    const QString eject  = cardEjectSide.trimmed().isEmpty()
                               ? QStringLiteral("Default")
                               : cardEjectSide;
    return QStringLiteral(
               "<?xml version=\"1.0\"?>"
               "<StartJob>"
               "<InputHopperSelection>%1</InputHopperSelection>"
               // CheckPrintRibbonSupplies / CheckEmbossSupplies are deprecated by
               // the SDK; supplies are read through \Printer.SuppliesStatus3:Read.
               "<CheckPrintRibbonSupplies>0</CheckPrintRibbonSupplies>"
               "<CheckEmbossSupplies>0</CheckEmbossSupplies>"
               "<CardEjectSide>%2</CardEjectSide>"
               "<OutputHopperLocation></OutputHopperLocation>"
               "</StartJob>")
        .arg(hopper, eject);
}

QString BidiClient::jobStatusXml(int printerJobId)
{
    return QStringLiteral("<?xml version=\"1.0\"?>"
                          "<JobStatus>"
                          "<PrinterJobID>%1</PrinterJobID>"
                          "</JobStatus>")
        .arg(printerJobId);
}

QString BidiClient::printerActionXml(int action, int printerJobId, int errorCode)
{
    // NOTE: supplying the error code is what clears that error on the printer.
    return QStringLiteral("<?xml version=\"1.0\"?>"
                          "<PrinterAction>"
                          "<Action>%1</Action>"
                          "<PrinterJobID>%2</PrinterJobID>"
                          "<ErrorCode>%3</ErrorCode>"
                          "</PrinterAction>")
        .arg(action)
        .arg(printerJobId)
        .arg(errorCode);
}

} // namespace occ
