#pragma once

#include "printing/ICardPrinter.h"
#include "printing/PrinterTypes.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace occ {

// ---------------------------------------------------------------------------
// PrinterManager - discovery and lifetime of printer backends.
//
// Which backend a queue uses is decided by ASKING the driver: the spooler's
// BidiSpl interface is queried for the XPS Card Printer schemas. A queue that
// answers is an XPS Card Printer; anything else is an ordinary Windows queue.
// Detection never relies on the printer's name.
//
// The simulator is always available, so the rest of the application stays
// usable with no hardware attached.
// ---------------------------------------------------------------------------
class PrinterManager : public QObject
{
    Q_OBJECT

public:
    explicit PrinterManager(QObject *parent = nullptr);
    ~PrinterManager() override;

    // Enumerates the installed queues and, for each, asks the driver which
    // backend applies. Caches the result and emits printersChanged().
    QVector<PrinterIdentity> refresh();

    // The printer list produced by the last refresh().
    const QVector<PrinterIdentity> &printers() const { return m_printers; }

    // Backend for a Windows printer name. Returns nullptr and sets lastError()
    // when the name is not one of the installed queues.
    CardPrinterPtr printer(const QString &name);

    // The always-available simulator backend (may be used with no hardware).
    CardPrinterPtr simulator();

    // The real detection query. `sdkVersion` receives the driver's reported SDK
    // version when it is available.
    static bool isEntrustCardPrinter(const QString &printerName,
                                     QString *sdkVersion = nullptr);

    // Status for a set of printer names. Unknown names yield a
    // PrinterState::NotAvailable status rather than an error.
    QVector<PrinterStatus> statusesFor(const QStringList &names);

    QString lastError() const { return m_lastError; }

signals:
    void printersChanged();

private:
    QVector<PrinterIdentity>      m_printers;
    QHash<QString, CardPrinterPtr> m_backends;
    CardPrinterPtr                m_simulator;
    QString                      m_lastError;
};

} // namespace occ
