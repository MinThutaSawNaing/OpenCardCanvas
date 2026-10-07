#pragma once

#include "codes/QrEncoder.h"
#include "core/CardObject.h"

#include <QColor>
#include <QString>

namespace occ {

// ---------------------------------------------------------------------------
// QrObject - a real QR code generated from the object's own data.
//
// The matrix comes from QrEncoder (ISO/IEC 18004), so what is drawn is an
// actually scannable symbol, not a decorative picture. Encoding is cached and
// only redone when the payload or the error correction level changes.
//
// The data may contain {{placeholders}}, which the personalization engine
// expands per record, so one QR object supports batch printing.
// ---------------------------------------------------------------------------
class QrObject : public CardObject
{
public:
    QrObject();

    QString data() const { return m_data; }
    void setData(const QString &data);

    QrErrorCorrection errorCorrection() const { return m_ecc; }
    void setErrorCorrection(QrErrorCorrection ecc);

    QrContentKind contentKind() const { return m_kind; }
    void setContentKind(QrContentKind kind) { m_kind = kind; }

    QColor foregroundColor() const { return m_foreground; }
    void setForegroundColor(const QColor &color) { m_foreground = color; }

    QColor backgroundColor() const { return m_background; }
    void setBackgroundColor(const QColor &color) { m_background = color; }

    // Quiet zone in modules. The standard requires 4; more is allowed.
    int quietZoneModules() const { return m_quietZoneModules; }
    void setQuietZoneModules(int modules);

    // --- validation ---------------------------------------------------------
    // Empty string when the payload can be encoded at the selected level.
    QString validationError() const;
    bool isValid() const { return validationError().isEmpty(); }

    // The encoded symbol for the current payload, or an invalid result.
    // `placeholders` may be null; when supplied, the payload is expanded first.
    QrEncoder::Result encode(const QHash<QString, QString> *placeholders = nullptr) const;

    QString resolvedData(const RenderContext &ctx) const;

protected:
    void paintObject(QPainter &painter, const RenderContext &ctx) const override;
    QJsonObject propertiesToJson() const override;
    bool propertiesFromJson(const QJsonObject &json, QString *error) override;
    QString defaultName() const override;
    CardObjectPtr cloneImpl() const override;

private:
    QString           m_data = QStringLiteral("{{employee_id}}");
    QrErrorCorrection m_ecc = QrErrorCorrection::Medium;
    QrContentKind     m_kind = QrContentKind::PlainText;
    QColor            m_foreground = QColor(Qt::black);
    QColor            m_background = QColor(Qt::white);
    int               m_quietZoneModules = 4;

    // Render cache: encoding is a few hundred microseconds, but it runs on
    // every repaint during a drag, so caching keeps dragging smooth.
    mutable QString           m_cacheKey;
    mutable QrEncoder::Result m_cache;
};

} // namespace occ
