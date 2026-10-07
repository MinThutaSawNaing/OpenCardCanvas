#pragma once

#include "printing/ICardPrinter.h"

#include <QString>

namespace occ {

// ---------------------------------------------------------------------------
// SimulatorPrinter - a backend that produces card images and a job record but
// NEVER prints a card.
//
// It exists so the editor, the preview and the whole printing workflow stay
// usable with no hardware attached. It is deliberately loud about what it is:
// isRealHardware() is false, every status text says "Simulator", and a print
// result is always marked simulated. Nothing in this class talks to a device,
// so it can never be mistaken for a real card being produced.
// ---------------------------------------------------------------------------
class SimulatorPrinter : public ICardPrinter
{
public:
    explicit SimulatorPrinter(
        const QString &name = QStringLiteral("OpenCardCanvas Simulator"));
    ~SimulatorPrinter() override;

    QString name() const override { return m_name; }
    PrinterBackendKind backend() const override { return PrinterBackendKind::Simulator; }
    PrinterIdentity identity() override;

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override { return m_connected; }

    PrinterStatus status() override;

    bool print(const PrintJob &job) override;
    PrintResult printJob(const PrintJob &job);

    QString lastError() const override { return m_lastError; }
    QString diagnosticsReport() override;
    bool isRealHardware() const override { return false; }
    bool cancelJob(int printerJobId = 0) override;

    // --- simulated configuration (drives the synthesised status) -----------
    void setSimulatedReady(bool ready) { m_simulatedReady = ready; }
    void setSimulatedBusy(bool busy) { m_simulatedBusy = busy; }
    void setSimulatedRibbonPercent(int percent) { m_simulatedRibbonPercent = percent; }
    void setSimulatedTopcoatPercent(int percent) { m_simulatedTopcoatPercent = percent; }
    void setSimulatedLaminator(bool installed) { m_simulatedLaminator = installed; }
    void setOutputDirectory(const QString &directory) { m_outputDirectory = directory; }

    // Where simulated card images are written when the job does not name a
    // directory (defaults to a folder under the system temporary folder).
    QString outputDirectory() const;

private:
    QString resolvedOutputDirectory(const PrintJob &job) const;

    QString m_name;
    bool    m_connected = false;
    bool    m_simulatedReady = true;
    bool    m_simulatedBusy = false;
    int     m_simulatedRibbonPercent = 100;
    int     m_simulatedTopcoatPercent = 100;
    bool    m_simulatedLaminator = false;
    QString m_outputDirectory;
    QString m_lastError;
    int     m_nextJobId = 1;
};

} // namespace occ
