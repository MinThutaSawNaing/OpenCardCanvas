// ---------------------------------------------------------------------------
// SimulatorPrinter - implementation.
//
// The simulator writes the rendered card sides as PNG files plus a small JSON
// job record, and reports progress through the job callback. It never opens a
// printer, never sends data to a device and never claims a card was produced.
// ---------------------------------------------------------------------------
#include "printing/SimulatorPrinter.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

namespace occ {

SimulatorPrinter::SimulatorPrinter(const QString &name)
    : m_name(name.isEmpty() ? QStringLiteral("OpenCardCanvas Simulator") : name)
{
}

SimulatorPrinter::~SimulatorPrinter() = default;

PrinterIdentity SimulatorPrinter::identity()
{
    PrinterIdentity id;
    id.name = m_name;
    id.manufacturer = QStringLiteral("OpenCardCanvas");
    id.model = QStringLiteral("Simulator (no hardware)");
    id.driverName = QStringLiteral("Simulator");
    id.connection = PrinterConnection::Virtual;
    id.backend = PrinterBackendKind::Simulator;
    id.duplexSupported = true;
    id.duplexKnown = true;
    id.hasPrintHead = true;
    id.printHeadKnown = true;
    id.hasLaminator = m_simulatedLaminator;
    id.laminatorKnown = true;
    id.colorResolution = QStringLiteral("300x300");
    id.monochromeResolution = QStringLiteral("300x300");
    return id;
}

bool SimulatorPrinter::connect()
{
    m_connected = true;
    m_lastError.clear();
    return true;
}

void SimulatorPrinter::disconnect()
{
    m_connected = false;
}

QString SimulatorPrinter::outputDirectory() const
{
    if (!m_outputDirectory.isEmpty())
        return m_outputDirectory;
    return QDir(QDir::tempPath())
        .filePath(QStringLiteral("OpenCardCanvas/simulator"));
}

QString SimulatorPrinter::resolvedOutputDirectory(const PrintJob &job) const
{
    if (!job.simulatorOutputDirectory.trimmed().isEmpty())
        return job.simulatorOutputDirectory;
    return outputDirectory();
}

// --------------------------------------------------------------------------
// status - a plausibly shaped status built purely from the simulated flags.
// --------------------------------------------------------------------------
PrinterStatus SimulatorPrinter::status()
{
    PrinterStatus st;
    st.updatedAt = QDateTime::currentDateTime();
    st.iBidiAvailable = false;
    st.stateText = QStringLiteral("Simulator");

    if (m_simulatedBusy)
        st.state = PrinterState::Busy;
    else if (m_simulatedReady)
        st.state = PrinterState::Ready;
    else
        st.state = PrinterState::NotAvailable;

    st.detail = QStringLiteral(
        "Simulator: no printer hardware is involved. The values shown are "
        "OpenCardCanvas's own simulated settings, not readings from a device.");

    st.printRibbon.available = true;
    st.printRibbon.type = QStringLiteral("Simulated ribbon");
    st.printRibbon.percentRemaining = m_simulatedRibbonPercent;

    st.topcoatRibbon.available = true;
    st.topcoatRibbon.type = QStringLiteral("Simulated top coat");
    st.topcoatRibbon.percentRemaining = m_simulatedTopcoatPercent;

    st.laminatorL1.available = m_simulatedLaminator;
    st.laminatorL1.percentRemaining = m_simulatedLaminator ? 100 : -1;

    if (!m_connected)
        st.messages << QStringLiteral("Simulator is not connected.");

    return st;
}

// --------------------------------------------------------------------------
// printJob - writes the card sides and a job record. This is a simulation, not
// a print: the result is always marked simulated and says so in the detail.
// --------------------------------------------------------------------------
PrintResult SimulatorPrinter::printJob(const PrintJob &job)
{
    PrintResult result;
    result.simulated = true;
    result.ok = false;
    m_lastError.clear();

    if (!m_connected)
        connect();

    if (!job.frontEnabled && !job.backEnabled) {
        result.error = QStringLiteral("There is nothing to simulate: both card sides are "
                                      "disabled.");
        m_lastError = result.error;
        return result;
    }
    if (job.frontEnabled && job.frontImage.isNull()) {
        result.error = QStringLiteral("The front of the card has no image to simulate.");
        m_lastError = result.error;
        return result;
    }
    if (job.backEnabled && job.backImage.isNull()) {
        result.error = QStringLiteral("The back of the card has no image to simulate.");
        m_lastError = result.error;
        return result;
    }

    const QString directory = resolvedOutputDirectory(job);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        result.error = QStringLiteral("The simulator could not create its output folder "
                                      "\"%1\".")
                           .arg(directory);
        m_lastError = result.error;
        return result;
    }

    result.printerJobId = m_nextJobId++;
    const QDir dir(directory);
    const QString frontPath = dir.filePath(QStringLiteral("card-front.png"));
    const QString backPath = dir.filePath(QStringLiteral("card-back.png"));
    const QString recordPath =
        dir.filePath(QStringLiteral("job-%1.json").arg(result.printerJobId));

    auto report = [&job, &result](JobState state, const QString &text) {
        if (job.progress)
            job.progress(result.printerJobId, state, text);
    };

    report(JobState::Active, QStringLiteral("Simulating the front of the card"));

    QString savedFront;
    QString savedBack;
    if (job.frontEnabled) {
        if (!job.frontImage.save(frontPath, "PNG")) {
            result.error =
                QStringLiteral("The simulator could not write \"%1\".").arg(frontPath);
            m_lastError = result.error;
            return result;
        }
        savedFront = frontPath;
    }
    if (job.backEnabled) {
        report(JobState::Active, QStringLiteral("Simulating the back of the card"));
        if (!job.backImage.save(backPath, "PNG")) {
            result.error =
                QStringLiteral("The simulator could not write \"%1\".").arg(backPath);
            m_lastError = result.error;
            return result;
        }
        savedBack = backPath;
    }

    QJsonObject record;
    record[QStringLiteral("printerName")] = m_name;
    record[QStringLiteral("simulated")] = true;
    record[QStringLiteral("printerJobId")] = result.printerJobId;
    record[QStringLiteral("documentName")] = job.documentName;
    record[QStringLiteral("copies")] = job.copies;
    record[QStringLiteral("duplex")] = job.duplex;
    record[QStringLiteral("frontEnabled")] = job.frontEnabled;
    record[QStringLiteral("backEnabled")] = job.backEnabled;
    record[QStringLiteral("frontImage")] = savedFront;
    record[QStringLiteral("backImage")] = savedBack;
    record[QStringLiteral("frontDpi")] = job.frontDpi;
    record[QStringLiteral("backDpi")] = job.backDpi;
    record[QStringLiteral("frontTopcoat")] = int(job.frontTopcoat);
    record[QStringLiteral("backTopcoat")] = int(job.backTopcoat);
    record[QStringLiteral("createdAt")] =
        QDateTime::currentDateTime().toString(Qt::ISODate);
    record[QStringLiteral("note")] =
        QStringLiteral("Simulated job - no card was printed.");

    QFile file(recordPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.error = QStringLiteral("The simulator could not write its job record "
                                      "\"%1\".")
                           .arg(recordPath);
        m_lastError = result.error;
        return result;
    }
    file.write(QJsonDocument(record).toJson(QJsonDocument::Indented));
    file.close();

    report(JobState::Succeeded, QStringLiteral("Simulation complete"));

    result.ok = true;
    result.completed = true;
    result.finalJobState = JobState::Succeeded;
    result.simulated = true;
    result.error.clear();
    result.errorCode = 0;
    result.errorString.clear();
    result.technicalDetail =
        QStringLiteral("SIMULATED ONLY - nothing was printed. The card image(s) and a job "
                       "record were written to \"%1\".")
            .arg(directory);
    return result;
}

bool SimulatorPrinter::print(const PrintJob &job)
{
    return printJob(job).ok;
}

// --------------------------------------------------------------------------
// cancelJob - there is no device to cancel, so this simply succeeds.
// --------------------------------------------------------------------------
bool SimulatorPrinter::cancelJob(int printerJobId)
{
    Q_UNUSED(printerJobId);
    m_lastError.clear();
    return true;
}

// --------------------------------------------------------------------------
// diagnosticsReport - states plainly that nothing is printed.
// --------------------------------------------------------------------------
QString SimulatorPrinter::diagnosticsReport()
{
    QStringList lines;
    lines << QStringLiteral("Simulator backend - NO HARDWARE IS INVOLVED");
    lines << QStringLiteral("This backend renders the card and writes a job record. It "
                            "never sends data to a printer and never produces a card.");
    lines << QStringLiteral("Name                    : %1").arg(m_name);
    lines << QStringLiteral("Connected               : %1")
                 .arg(m_connected ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QStringLiteral("Simulated ready         : %1")
                 .arg(m_simulatedReady ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QStringLiteral("Simulated busy          : %1")
                 .arg(m_simulatedBusy ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QStringLiteral("Simulated print ribbon  : %1%").arg(m_simulatedRibbonPercent);
    lines << QStringLiteral("Simulated top coat      : %1%").arg(m_simulatedTopcoatPercent);
    lines << QStringLiteral("Simulated laminator     : %1")
                 .arg(m_simulatedLaminator ? QStringLiteral("Installed")
                                           : QStringLiteral("Not installed"));
    lines << QStringLiteral("Output folder           : %1").arg(outputDirectory());
    return lines.join(QLatin1Char('\n'));
}

} // namespace occ
