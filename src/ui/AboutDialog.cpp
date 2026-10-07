#include "ui/AboutDialog.h"

#include "occ/Version.h"
#include "ui/IconFactory.h"
#include "utils/AppPaths.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace occ {

namespace {

// Build stamp: the date and time this translation unit was compiled, which is
// the only honest "build date" a compiled binary has.
QString buildStamp()
{
    return QString::fromLatin1(__DATE__) + QLatin1Char(' ') + QString::fromLatin1(__TIME__);
}

const char *kLicenceText = QT_TRANSLATE_NOOP(
    "occ::AboutDialog",
    "Entrust/Datacard printer support uses the documented XPS Card Printer Driver "
    "interfaces (Windows print spooler BidiSpl and GDI/XPS printing). Entrust and Datacard "
    "are trademarks of their respective owners. Driver software is not redistributed with "
    "this application and must be installed separately.");

struct Component
{
    const char *name;
    const char *description;
};

const Component kThirdParty[] = {
    { "Qt 6",
      QT_TRANSLATE_NOOP("occ::AboutDialog",
                        "Application framework (Core, Gui, Widgets, PrintSupport, Svg, "
                        "Network, Sql). Used under the GNU Lesser General Public Licence "
                        "version 3.") },
    { "QR Code encoder",
      QT_TRANSLATE_NOOP("occ::AboutDialog",
                        "Written for OpenCardCanvas from ISO/IEC 18004: versions 1-40, error "
                        "correction levels L/M/Q/H, Reed-Solomon over GF(256).") },
    { "Barcode encoders",
      QT_TRANSLATE_NOOP("occ::AboutDialog",
                        "Code 128, Code 39, EAN-13, EAN-8 and ITF-14, written for "
                        "OpenCardCanvas from the published symbology specifications.") },
    { "Windows print spooler",
      QT_TRANSLATE_NOOP("occ::AboutDialog",
                        "BidiSpl (bidispl.h) for printer status, capabilities and supplies; "
                        "GDI and XPS printing for card output.") },
    { "Icons",
      QT_TRANSLATE_NOOP("occ::AboutDialog",
                        "Drawn as vector paths by OpenCardCanvas itself. No icon font and no "
                        "third-party icon set is used or redistributed.") },
};

} // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About OpenCardCanvas"));
    setWindowIcon(IconFactory::icon(QStringLiteral("logo")));
    resize(620, 480);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *header = new QHBoxLayout();
    auto *logo = new QLabel(this);
    logo->setPixmap(IconFactory::pixmap(QStringLiteral("logo"), 64));
    logo->setFixedSize(64, 64);
    logo->setScaledContents(true);
    header->addWidget(logo);

    auto *titleBox = new QVBoxLayout();
    auto *title = new QLabel(tr("OpenCardCanvas %1").arg(QStringLiteral(OCC_VERSION_STRING)),
                             this);
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.4);
    titleFont.setBold(true);
    title->setFont(titleFont);
    titleBox->addWidget(title);

    auto *subtitle = new QLabel(tr("Design, personalize, preview and print ID cards."), this);
    subtitle->setWordWrap(true);
    titleBox->addWidget(subtitle);

    auto *facts = new QLabel(tr("Version %1  \u00B7  built %2\nQt %3  \u00B7  %4")
                                 .arg(QStringLiteral(OCC_VERSION_STRING),
                                      buildStamp(),
                                      QString::fromLatin1(qVersion()),
                                      QSysInfo::prettyProductName()),
                             this);
    facts->setWordWrap(true);
    titleBox->addWidget(facts);
    header->addLayout(titleBox, 1);
    layout->addLayout(header);

    auto *tabs = new QTabWidget(this);

    auto *licencePage = new QTextBrowser(tabs);
    licencePage->setOpenExternalLinks(true);
    licencePage->setHtml(
        tr("<p><b>Licence</b></p>"
           "<p>OpenCardCanvas is provided as-is. Build instructions are in "
           "docs/BUILD.md and the project file format is documented in "
           "docs/PROJECT_FORMAT.md.</p>"
           "<p><b>Printer support</b></p>"
           "<p>%1</p>"
           "<p>Card printers are driven through the Windows print spooler. No vendor driver, "
           "DLL or binary is copied, embedded or redistributed by this application; the XPS "
           "Card Printer Driver must be installed by the user or by their IT department "
           "before a card printer can be used.</p>"
           "<p>Printer status, capabilities and supply levels are read from the driver "
           "through the documented BidiSpl schemas. Where a device does not report a value, "
           "OpenCardCanvas shows \"Not available\" rather than a guess.</p>")
            .arg(tr(kLicenceText)));
    tabs->addTab(licencePage, tr("Licence and attribution"));

    QString components = tr("<p>OpenCardCanvas is built on the following components:</p>");
    for (const Component &component : kThirdParty) {
        components += QStringLiteral("<p><b>%1</b><br/>%2</p>")
                          .arg(QString::fromLatin1(component.name),
                               tr(component.description));
    }
    auto *componentsPage = new QTextBrowser(tabs);
    componentsPage->setOpenExternalLinks(true);
    componentsPage->setHtml(components);
    tabs->addTab(componentsPage, tr("Third-party components"));

    auto *pathsPage = new QTextBrowser(tabs);
    pathsPage->setHtml(tr("<p><b>Locations</b></p><ul>"
                          "<li>Settings: %1</li>"
                          "<li>Logs: %2</li>"
                          "<li>Recovery: %3</li>"
                          "<li>Templates: %4</li></ul>")
                           .arg(AppPaths::configFilePath().toHtmlEscaped(),
                                AppPaths::logsDir().toHtmlEscaped(),
                                AppPaths::autosaveDir().toHtmlEscaped(),
                                AppPaths::templatesDir().toHtmlEscaped()));
    tabs->addTab(pathsPage, tr("Locations"));

    layout->addWidget(tabs, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

} // namespace occ
