// ---------------------------------------------------------------------------
// Unit tests: the printer abstraction and the simulator backend.
//
// There is no card printer attached to the build machine, so these tests cover
// what can be verified here: that the abstraction behaves, that the simulator
// renders and records a job, and - most importantly - that the simulator can
// never be mistaken for a printer. The Entrust/Datacard hardware path is
// exercised only as far as "detection does not crash on this machine".
// ---------------------------------------------------------------------------
#include "printing/ICardPrinter.h"
#include "printing/PrinterManager.h"
#include "printing/SimulatorPrinter.h"
#include "utils/GdiPrintSurface.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest/QtTest>

using namespace occ;

class TestPrinter : public QObject
{
    Q_OBJECT
private slots:
    void simulatorIsAlwaysAvailable();
    void simulatorNeverClaimsRealHardware();
    void simulatorStatusSaysWhatItIs();
    void simulatorRecordsAJob();
    void simulatorReportsProgressStages();
    void simulatorDiagnosticsExplainThemselves();
    void managerRefreshDoesNotCrash();
    void printerEnumerationDoesNotCrash();
    void statusMaskDecodingIsSane();
};

void TestPrinter::simulatorIsAlwaysAvailable()
{
    PrinterManager manager;
    // The application must stay usable with no hardware attached, so this is
    // never allowed to be null.
    const CardPrinterPtr simulator = manager.simulator();
    QVERIFY(simulator != nullptr);
    QCOMPARE(simulator->backend(), PrinterBackendKind::Simulator);
    QVERIFY(!simulator->name().isEmpty());
}

void TestPrinter::simulatorNeverClaimsRealHardware()
{
    SimulatorPrinter simulator;
    // This single flag is what stops the UI from ever showing "printed" for a
    // job that went nowhere near a card.
    QVERIFY(!simulator.isRealHardware());
    QCOMPARE(simulator.backend(), PrinterBackendKind::Simulator);

    const PrinterIdentity identity = simulator.identity();
    QCOMPARE(identity.backend, PrinterBackendKind::Simulator);
    QVERIFY(!identity.name.isEmpty());
}

void TestPrinter::simulatorStatusSaysWhatItIs()
{
    SimulatorPrinter simulator;
    QVERIFY(simulator.connect());
    QVERIFY(simulator.isConnected());

    const PrinterStatus status = simulator.status();
    // Whatever state it reports, the text must make the simulation obvious.
    const QString combined = status.stateText + QLatin1Char(' ') + status.detail;
    QVERIFY2(combined.contains(QStringLiteral("simulat"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("simulator status did not say so: '%1'").arg(combined)));

    simulator.disconnect();
    QVERIFY(!simulator.isConnected());
}

void TestPrinter::simulatorRecordsAJob()
{
    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());

    QImage front(300, 190, QImage::Format_ARGB32_Premultiplied);
    front.fill(QColor(240, 240, 250));
    QImage back(300, 190, QImage::Format_ARGB32_Premultiplied);
    back.fill(QColor(200, 220, 240));

    PrintJob job;
    job.printerName = QStringLiteral("OpenCardCanvas Simulator");
    job.documentName = QStringLiteral("unit test card");
    job.copies = 1;
    job.frontEnabled = true;
    job.backEnabled = true;
    job.frontImage = front;
    job.backImage = back;
    job.simulatorOutputDirectory = outputDir.path();

    SimulatorPrinter simulator;
    QVERIFY(simulator.connect());
    const PrintResult result = simulator.printJob(job);

    // The job is reported as a successful *simulation*, and it says so.
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY2(result.simulated, "a simulator job must be marked as simulated");

    // It must have actually written the rendered sides somewhere, so a workflow
    // can be inspected end to end.
    const QDir dir(outputDir.path());
    const QStringList produced =
        dir.entryList(QStringList() << QStringLiteral("*.png"), QDir::Files);
    QVERIFY2(!produced.isEmpty(), "the simulator wrote no output files");
}

void TestPrinter::simulatorReportsProgressStages()
{
    QTemporaryDir outputDir;
    QVERIFY(outputDir.isValid());

    QImage front(200, 126, QImage::Format_ARGB32_Premultiplied);
    front.fill(Qt::white);

    PrintJob job;
    job.frontEnabled = true;
    job.backEnabled = false;
    job.frontImage = front;
    job.simulatorOutputDirectory = outputDir.path();

    int progressCalls = 0;
    job.progress = [&progressCalls](int, JobState, const QString &) { ++progressCalls; };

    SimulatorPrinter simulator;
    QVERIFY(simulator.connect());
    const PrintResult result = simulator.printJob(job);
    QVERIFY(result.ok);
    // A long running print must not be a black box, so progress is reported.
    QVERIFY2(progressCalls > 0, "the simulator reported no progress");
}

void TestPrinter::simulatorDiagnosticsExplainThemselves()
{
    SimulatorPrinter simulator;
    const QString report = simulator.diagnosticsReport();
    QVERIFY(!report.isEmpty());
    QVERIFY2(report.contains(QStringLiteral("simulat"), Qt::CaseInsensitive),
             "the diagnostics report must state that no hardware is involved");
}

void TestPrinter::managerRefreshDoesNotCrash()
{
    PrinterManager manager;
    // On this machine there may be no printers at all; that is a valid result
    // and must not crash or report a spurious error.
    const QVector<PrinterIdentity> printers = manager.refresh();
    for (const PrinterIdentity &printer : printers)
        QVERIFY(!printer.name.isEmpty());

    // Asking for an unknown printer must fail cleanly rather than assert.
    const CardPrinterPtr missing = manager.printer(QStringLiteral("no such printer 12345"));
    if (missing)
        QVERIFY(!missing->name().isEmpty());
}

void TestPrinter::printerEnumerationDoesNotCrash()
{
    // Enumerating through the Windows spooler is the real capability probe; it
    // must work - possibly returning an empty list - on any machine.
    const QVector<GdiPrintSurface::PrinterInfo> printers = GdiPrintSurface::enumeratePrinters();
    for (const GdiPrintSurface::PrinterInfo &printer : printers) {
        QVERIFY(!printer.name.isEmpty());
        QVERIFY(!printer.port.isNull());
    }
}

void TestPrinter::statusMaskDecodingIsSane()
{
    // A zero status means "nothing flagged", and the decoder must return
    // something a human can read rather than an empty list.
    const QStringList decoded = GdiPrintSurface::describePrinterStatus(0);
    QVERIFY(!decoded.isEmpty());

    const QString attributes = GdiPrintSurface::describePrinterAttributes(0);
    QVERIFY(!attributes.isNull());
}

QTEST_MAIN(TestPrinter)
#include "test_printer.moc"
