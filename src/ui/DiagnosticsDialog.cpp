#include "ui/DiagnosticsDialog.h"

#include "core/CardDocument.h"
#include "core/CardSide.h"
#include "occ/Version.h"
#include "project/AssetStore.h"
#include "project/ProjectValidator.h"
#include "ui/IconFactory.h"
#include "utils/AppPaths.h"
#include "utils/Logger.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QSysInfo>
#include <QTextBrowser>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace occ {

namespace {

QString severityColor(Issue::Severity severity)
{
    switch (severity) {
    case Issue::Severity::Error:   return QStringLiteral("#c02626");
    case Issue::Severity::Warning: return QStringLiteral("#b06a00");
    case Issue::Severity::Info:    return QStringLiteral("#2a6f2a");
    }
    return QStringLiteral("#404040");
}

} // namespace

DiagnosticsDialog::DiagnosticsDialog(const CardDocument *document, QWidget *parent)
    : QDialog(parent),
      m_document(document)
{
    setWindowTitle(tr("Diagnostics"));
    setWindowIcon(IconFactory::icon(QStringLiteral("log")));
    resize(760, 600);
    buildUi();
    refresh();
}

void DiagnosticsDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    m_report = new QTextBrowser(this);
    m_report->setOpenExternalLinks(true);
    layout->addWidget(m_report, 1);

    auto *buttons = new QHBoxLayout();
    m_openLogs = new QPushButton(tr("Open log folder"), this);
    m_openProject = new QPushButton(tr("Open project folder"), this);
    m_copy = new QPushButton(tr("Copy report to clipboard"), this);
    m_refresh = new QPushButton(IconFactory::icon(QStringLiteral("refresh")), tr("Refresh"),
                                this);
    buttons->addWidget(m_openLogs);
    buttons->addWidget(m_openProject);
    buttons->addStretch(1);
    buttons->addWidget(m_copy);
    buttons->addWidget(m_refresh);
    layout->addLayout(buttons);

    auto *closeRow = new QHBoxLayout();
    auto *close = new QPushButton(tr("Close"), this);
    closeRow->addStretch(1);
    closeRow->addWidget(close);
    layout->addLayout(closeRow);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_refresh, &QPushButton::clicked, this, &DiagnosticsDialog::refresh);
    connect(m_openLogs, &QPushButton::clicked, this, [this] {
        const QString dir = AppPaths::logsDir();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            QTextBrowser *browser = m_report;
            browser->setHtml(browser->toHtml()
                             + tr("<p><b>The log folder could not be opened. It is:</b><br/>%1</p>")
                                   .arg(dir.toHtmlEscaped()));
        }
    });
    connect(m_openProject, &QPushButton::clicked, this, [this] {
        QString dir = AppPaths::defaultExportsDir();
        if (m_document && !m_document->filePath().isEmpty())
            dir = QFileInfo(m_document->filePath()).absolutePath();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
            m_report->setHtml(m_report->toHtml()
                              + tr("<p><b>The folder could not be opened. It is:</b><br/>%1</p>")
                                    .arg(dir.toHtmlEscaped()));
        }
    });
    connect(m_copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_report->toPlainText());
        m_copy->setText(tr("Copied"));
    });
}

void DiagnosticsDialog::setDocument(const CardDocument *document)
{
    m_document = document;
    refresh();
}

QString DiagnosticsDialog::logTail(int lines) const
{
    const QString path = Logger::logFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return tr("No log file was found at %1").arg(QDir::toNativeSeparators(path));

    // The whole file is read and the tail kept: rotation bounds the size, and
    // this avoids any seek arithmetic that could land inside a UTF-8 sequence.
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    QStringList all;
    while (!stream.atEnd())
        all << stream.readLine();
    file.close();

    const int from = qMax(0, int(all.size()) - lines);
    return all.mid(from).join(QLatin1Char('\n'));
}

QString DiagnosticsDialog::buildReport() const
{
    QString html;
    html += QStringLiteral("<h2>%1</h2>").arg(tr("OpenCardCanvas diagnostics"));

    html += QStringLiteral("<p><b>%1</b><br/>").arg(tr("Application"));
    html += tr("Version %1 (project format %2)<br/>")
                .arg(QStringLiteral(OCC_VERSION_STRING))
                .arg(OCC_PROJECT_FORMAT_VERSION);
    html += tr("Qt %1<br/>").arg(QString::fromLatin1(qVersion()));
    html += tr("OS: %1<br/>").arg(QSysInfo::prettyProductName().toHtmlEscaped());
    html += tr("Kernel: %1 %2<br/>").arg(QSysInfo::kernelType().toHtmlEscaped(),
                                         QSysInfo::kernelVersion().toHtmlEscaped());
    html += tr("CPU: %1").arg(QSysInfo::currentCpuArchitecture().toHtmlEscaped());
    html += QStringLiteral("</p>");

    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        html += QStringLiteral("<p><b>%1</b><br/>").arg(tr("Display"));
        html += tr("Screen: %1 x %2 pixels, %3 x %4 mm<br/>")
                    .arg(screen->size().width())
                    .arg(screen->size().height())
                    .arg(QString::number(screen->physicalSize().width(), 'f', 1),
                         QString::number(screen->physicalSize().height(), 'f', 1));
        html += tr("Logical DPI: %1 x %2<br/>")
                    .arg(QString::number(screen->logicalDotsPerInchX(), 'f', 1),
                         QString::number(screen->logicalDotsPerInchY(), 'f', 1));
        html += tr("Device pixel ratio: %1<br/>")
                    .arg(QString::number(screen->devicePixelRatio(), 'f', 2));
        html += tr("Application scaling: %1 %")
                    .arg(QString::number(qApp ? qApp->devicePixelRatio() * 100.0 : 100.0, 'f', 0));
        html += QStringLiteral("</p>");
    }

    html += QStringLiteral("<p><b>%1</b><ul>").arg(tr("Locations"));
    html += QStringLiteral("<li>%1 %2</li>").arg(tr("Settings:"),
                                                 AppPaths::configFilePath().toHtmlEscaped());
    html += QStringLiteral("<li>%1 %2</li>").arg(tr("Logs:"),
                                                 Logger::logDirectory().toHtmlEscaped());
    html += QStringLiteral("<li>%1 %2</li>").arg(tr("Recovery:"),
                                                 AppPaths::autosaveDir().toHtmlEscaped());
    html += QStringLiteral("<li>%1 %2</li>").arg(tr("Templates:"),
                                                 AppPaths::templatesDir().toHtmlEscaped());
    html += QStringLiteral("<li>%1 %2</li>").arg(tr("Exports:"),
                                                 AppPaths::defaultExportsDir().toHtmlEscaped());
    html += QStringLiteral("</ul></p>");

    if (!m_document) {
        html += tr("<p>No document is open.</p>");
        html += QStringLiteral("<p><b>%1</b></p><pre>%2</pre>")
                    .arg(tr("Log (last 200 lines)"), logTail(200).toHtmlEscaped());
        return html;
    }

    int frontCount = 0;
    int backCount = 0;
    const CardSideId sides[] = { CardSideId::Front, CardSideId::Back };
    for (CardSideId sideId : sides) {
        const int count = m_document->side(sideId).count();
        if (sideId == CardSideId::Front)
            frontCount = count;
        else
            backCount = count;
    }

    html += QStringLiteral("<p><b>%1</b><br/>").arg(tr("Current document"));
    html += tr("File: %1<br/>")
                .arg(m_document->filePath().isEmpty()
                         ? tr("(not saved yet)")
                         : m_document->filePath().toHtmlEscaped());
    html += tr("Card: %1 x %2 mm at %3 dpi, bleed %4 mm<br/>")
                .arg(QString::number(m_document->geometry().widthMm(), 'f', 2),
                     QString::number(m_document->geometry().heightMm(), 'f', 2))
                .arg(m_document->geometry().renderDpi())
                .arg(QString::number(m_document->geometry().bleedMm(), 'f', 2));
    html += tr("Objects: %1 on the front, %2 on the back (%3 total)<br/>")
                .arg(frontCount)
                .arg(backCount)
                .arg(frontCount + backCount);
    html += tr("Images: %1<br/>").arg(m_document->assets() ? m_document->assets()->count() : 0);
    html += tr("Placeholders: %1<br/>")
                .arg(m_document->placeholders().isEmpty()
                         ? tr("none")
                         : m_document->placeholders().join(QStringLiteral(", ")).toHtmlEscaped());
    html += tr("Unsaved changes: %1").arg(m_document->isDirty() ? tr("yes") : tr("no"));
    html += QStringLiteral("</p>");

    const Report validation = ProjectValidator::validate(*m_document);
    html += QStringLiteral("<p><b>%1</b> - %2</p>")
                .arg(tr("Design audit"), validation.summary().toHtmlEscaped());
    if (validation.issues.isEmpty()) {
        html += tr("<p>No problems were found. This design should print as intended.</p>");
    } else {
        html += QStringLiteral("<ul>");
        for (const Issue &issue : validation.issues) {
            html += QStringLiteral("<li><span style=\"color:%1\"><b>%2</b></span> %3</li>")
                        .arg(severityColor(issue.severity),
                             ProjectValidator::severityText(issue.severity).toHtmlEscaped(),
                             issue.message.toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
    }

    html += QStringLiteral("<p><b>%1</b></p><pre>%2</pre>")
                .arg(tr("Log (last 200 lines)"), logTail(200).toHtmlEscaped());
    return html;
}

void DiagnosticsDialog::refresh()
{
    if (!m_report)
        return;
    m_report->setHtml(buildReport());
    if (m_copy)
        m_copy->setText(tr("Copy report to clipboard"));
}

} // namespace occ
