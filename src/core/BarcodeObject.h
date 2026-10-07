#pragma once

#include "codes/BarcodeEncoder.h"
#include "core/CardObject.h"

#include <QColor>
#include <QString>

namespace occ {

// ---------------------------------------------------------------------------
// BarcodeObject - a linear barcode generated from the object's own data.
//
// The bars come from BarcodeEncoder, so what is drawn really is the requested
// symbology. If the data is invalid for the symbology (a letter in an EAN-13,
// an odd digit count in ITF-14, a character outside Code 39's set) the object
// reports the problem instead of drawing an unscannable symbol.
//
// The data may contain {{placeholders}} so a single barcode object supports
// batch personalization.
// ---------------------------------------------------------------------------
class BarcodeObject : public CardObject
{
public:
    BarcodeObject();

    QString data() const { return m_data; }
    void setData(const QString &data);

    BarcodeSymbology symbology() const { return m_symbology; }
    void setSymbology(BarcodeSymbology symbology);

    bool showHumanText() const { return m_showHumanText; }
    void setShowHumanText(bool on) { m_showHumanText = on; }

    double humanTextSizePt() const { return m_humanTextSizePt; }
    void setHumanTextSizePt(double pt);

    QColor foregroundColor() const { return m_foreground; }
    void setForegroundColor(const QColor &color) { m_foreground = color; }

    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &color) { m_background = color; }

    // Quiet zone in modules on each side (10 is the usual recommendation for
    // Code 128 / Code 39).
    int quietZoneModules() const { return m_quietZoneModules; }
    void setQuietZoneModules(int modules);

    // --- validation ---------------------------------------------------------
    // Empty when the payload is valid for the selected symbology. Validation
    // uses the raw data (placeholders are checked after expansion at render
    // time, where a per-record problem is reported instead).
    QString validationError() const;
    bool isValid() const { return validationError().isEmpty(); }

    BarcodeEncoder::Result encode() const;
    BarcodeEncoder::Result encodeWith(const QHash<QString, QString> &values) const;

    // Narrowest module width that is still considered reliably readable at the
    // object's current size.
    static double minimumReliableModuleMm();

    // True when the object is too narrow to print the symbol reliably.
    bool moduleWidthIsRisky(double *moduleMm = nullptr) const;

protected:
    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

private:
    QString          m_data = QStringLiteral("{{employee_id}}");
    BarcodeSymbology m_symbology = BarcodeSymbology::Code128;
    bool             m_showHumanText = true;
    double           m_humanTextSizePt = 6.0;
    QColor           m_foreground = QColor(Qt::black);
    QColor           m_background = QColor(Qt::white);
    int              m_quietZoneModules = 10;
};

} // namespace occ
