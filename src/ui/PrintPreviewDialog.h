#pragma once

#include "core/CardTypes.h"

#include <QDialog>
#include <QImage>
#include <QString>
#include <QVector>

class QComboBox;
class QLabel;
class QScrollArea;
class QSlider;
class QSpinBox;

// ---------------------------------------------------------------------------
// PrintPreviewDialog - what is about to be printed, rendered by the same code
// that prints it.
//
// The preview is produced by CardRenderer::renderSide at print resolution, not
// by a separate "preview" drawing path, and not by scaling the design canvas.
// That is the whole point: a preview that is drawn by different code than the
// output is a decoration, and it can disagree with the card in the printer.
//
// It shows the card size, the printer and its driver, the copies, the duplex
// setting and which records are queued, and it is explicit that personalised
// values are substituted at print time.
// ---------------------------------------------------------------------------
namespace occ {

class CardDocument;
class PrinterManager;

class PrintPreviewDialog : public QDialog
{
    Q_OBJECT
public:
    struct Request
    {
        QString      printerName;      // empty means the simulator
        int          copies = 1;
        bool         duplex = false;
        bool         frontEnabled = true;
        bool         backEnabled = false;
        QVector<int> records;          // empty = print the design as designed
    };

    PrintPreviewDialog(const CardDocument &document, const Request &request,
                       PrinterManager *printerManager, QWidget *parent = nullptr);

    Request request() const;

signals:
    // The user confirmed; the window owns the actual printing so progress and
    // failures are reported in one place.
    void printConfirmed(const Request &request);

private:
    void buildUi();
    void renderPreview();
    void updateImages();
    void updateSummary();
    void setZoom(int percent);
    void fitToWindow();
    QImage renderSide(CardSideId side) const;

    const CardDocument *m_document = nullptr;
    PrinterManager     *m_printers = nullptr;
    Request             m_request;
    QImage              m_front;
    QImage              m_back;

    QScrollArea *m_scroll = nullptr;
    QLabel      *m_frontLabel = nullptr;
    QLabel      *m_backLabel = nullptr;
    QSlider     *m_zoom = nullptr;
    QLabel      *m_zoomLabel = nullptr;
    QSpinBox    *m_copies = nullptr;
    QComboBox   *m_printerChoice = nullptr;
    QLabel      *m_frontSideLabel = nullptr;
    QLabel      *m_backSideLabel = nullptr;
    QLabel      *m_summary = nullptr;
};

} // namespace occ
