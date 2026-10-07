#pragma once

#include <QDialog>
#include <QString>

class QPushButton;
class QTextBrowser;

// ---------------------------------------------------------------------------
// DiagnosticsDialog - everything a support conversation needs, in one window.
//
// The point of this dialog is that the user never has to be asked "what version
// are you running, what does the log say, and how big is your card?". All of it
// is assembled here, and "Copy report to clipboard" turns it into a single
// paste-able message.
//
// The design audit from ProjectValidator is shown with severity colouring, so
// a problem that would print badly is visible before a card is consumed.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;

class DiagnosticsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit DiagnosticsDialog(const CardDocument *document, QWidget *parent = nullptr);

    void setDocument(const CardDocument *document);

private:
    void buildUi();
    void refresh();
    QString buildReport() const;
    QString logTail(int lines) const;

    const CardDocument *m_document = nullptr;
    QTextBrowser *m_report = nullptr;
    QPushButton  *m_openLogs = nullptr;
    QPushButton  *m_openProject = nullptr;
    QPushButton  *m_copy = nullptr;
    QPushButton  *m_refresh = nullptr;
};

} // namespace occ
