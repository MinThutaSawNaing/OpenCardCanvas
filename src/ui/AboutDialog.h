#pragma once

#include <QDialog>

// ---------------------------------------------------------------------------
// AboutDialog - version, build and attribution.
//
// The printer attribution is not decoration: OpenCardCanvas talks to Entrust /
// Datacard XPS card printers through the interfaces their driver documents
// (the spooler's BidiSpl channel and GDI/XPS printing). Saying so plainly, and
// saying that no driver software is shipped with this application, is the
// difference between a tool an IT department can approve and one it cannot.
// ---------------------------------------------------------------------------
namespace occ {

class AboutDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};

} // namespace occ
