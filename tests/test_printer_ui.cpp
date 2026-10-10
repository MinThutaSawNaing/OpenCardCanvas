#include "core/CardDocument.h"
#include "ui/PrinterSettingsDialog.h"
#include "ui/PrintPreviewDialog.h"
#include "utils/Settings.h"

#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest/QtTest>

using namespace occ;

class TestPrinterUi : public QObject
{
    Q_OBJECT
private slots:
    void browsingDoesNotReplaceSavedPrinter()
    {
        auto &settings = AppSettings::instance();
        const QString old = settings.defaultPrinter();
        settings.setDefaultPrinter(QStringLiteral("Saved unavailable printer"));
        PrinterSettingsDialog dialog(nullptr, PrinterSettingsDialog::Mode::Settings);
        auto *table = dialog.findChild<QTableWidget *>();
        QVERIFY(table);
        table->setRowCount(1);
        auto *item = new QTableWidgetItem(QStringLiteral("Other printer"));
        item->setData(Qt::UserRole, QStringLiteral("Other printer"));
        table->setItem(0, 0, item);
        table->selectRow(0);
        QCOMPARE(settings.defaultPrinter(), QStringLiteral("Saved unavailable printer"));
        auto *save = dialog.findChild<QPushButton *>(QStringLiteral("savePrinter"));
        QVERIFY(save);
        save->click();
        QCOMPARE(settings.defaultPrinter(), QStringLiteral("Other printer"));
        settings.setDefaultPrinter(old);
        settings.sync();
    }

    void previewSelectionAndExplicitSave()
    {
        auto &settings = AppSettings::instance();
        const QString old = settings.defaultPrinter();
        settings.setDefaultPrinter(QStringLiteral("Saved printer"));
        CardDocument document;
        PrintPreviewDialog::Request request;
        request.printerName = QStringLiteral("Saved printer");
        PrintPreviewDialog dialog(document, request, nullptr);
        auto *choice = dialog.findChild<QComboBox *>(QStringLiteral("printerChoice"));
        QVERIFY(choice);
        QCOMPARE(dialog.request().printerName, request.printerName);
        choice->setCurrentIndex(0);
        QVERIFY(dialog.request().printerName.isEmpty());
        QCOMPARE(settings.defaultPrinter(), request.printerName);
        auto *save = dialog.findChild<QPushButton *>(QStringLiteral("savePrinter"));
        QVERIFY(save);
        save->click();
        QVERIFY(settings.defaultPrinter().isEmpty());
        settings.setDefaultPrinter(old);
        settings.sync();
    }
};

QTEST_MAIN(TestPrinterUi)
#include "test_printer_ui.moc"