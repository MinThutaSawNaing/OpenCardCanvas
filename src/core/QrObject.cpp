#include "core/QrObject.h"

#include "personalization/TemplateEngine.h"
#include "rendering/RenderContext.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPainter>
#include <QtMath>

namespace occ {

QrObject::QrObject() : CardObject(ObjectType::QrCode, ObjectId::createUuid())
{
    setRectMm(QRectF(60.0, 8.0, 20.0, 20.0));
    m_name = defaultName();
}

void QrObject::setData(const QString &data)
{
    m_data = data;
    m_cacheKey.clear();
}

void QrObject::setErrorCorrection(QrErrorCorrection ecc)
{
    m_ecc = ecc;
    m_cacheKey.clear();
}

void QrObject::setQuietZoneModules(int modules)
{
    m_quietZoneModules = qBound(0, modules, 16);
}

QString QrObject::validationError() const
{
    if (m_data.trimmed().isEmpty())
        return QCoreApplication::translate("QrObject", "The QR code has no data.");
    if (!QrEncoder::canEncode(m_data, m_ecc)) {
        return QCoreApplication::translate(
            "QrObject",
            "The data is too long for a QR code at error correction level %1. "
            "Shorten the text or lower the error correction level.")
            .arg(names::qrEcc(m_ecc));
    }
    return QString();
}

QrEncoder::Result QrObject::encode(const QHash<QString, QString> *placeholders) const
{
    const QString payload = placeholders ? TemplateEngine::expand(m_data, *placeholders) : m_data;
    const QString key = payload + QLatin1Char('\x1f') + names::qrEcc(m_ecc);
    if (key == m_cacheKey && m_cache.ok)
        return m_cache;
    QrEncoder::Result result = QrEncoder::encode(payload, m_ecc);
    if (result.ok) {
        m_cacheKey = key;
        m_cache = result;
    }
    return result;
}

QString QrObject::resolvedData(const RenderContext &ctx) const
{
    return ctx.resolveText(m_data, id());
}

void QrObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    const double wMm = widthMm();
    const double hMm = heightMm();
    if (wMm <= 0.0 || hMm <= 0.0)
        return;

    const QString payload = resolvedData(ctx);

    // Draws an honest "cannot generate" marker rather than a symbol that would
    // not scan.
    const auto paintInvalid = [&] {
        painter.save();
        QPen pen(QColor(200, 40, 40, 220));
        pen.setWidthF(0.15);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 245, 245, 150));
        painter.drawRect(QRectF(0.0, 0.0, wMm, hMm));
        painter.restore();
    };

    if (payload.trimmed().isEmpty()) {
        paintInvalid();
        ctx.addWarning(QCoreApplication::translate(
                           "QrObject", "A QR code has no data and cannot be generated."),
                       id());
        return;
    }

    const QrEncoder::Result result = QrEncoder::encode(payload, m_ecc);
    if (!result.ok) {
        ctx.addWarning(result.error, id());
        paintInvalid();
        return;
    }

    // Fit the symbol inside the object box, centred, keeping modules square.
    const double totalModules = double(result.size) + 2.0 * double(m_quietZoneModules);
    if (totalModules <= 0.0)
        return;
    const double moduleMm = qMin(wMm, hMm) / totalModules;
    const double drawSize = moduleMm * totalModules;
    const double originX = (wMm - drawSize) / 2.0;
    const double originY = (hMm - drawSize) / 2.0;

    painter.save();
    // QR modules must stay crisp: no antialiasing and no smoothing.
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (m_background.alpha() > 0)
        painter.fillRect(QRectF(originX, originY, drawSize, drawSize), m_background);

    if (m_foreground.alpha() > 0) {
        const QBrush brush(m_foreground);
        const double baseX = originX + moduleMm * double(m_quietZoneModules);
        const double baseY = originY + moduleMm * double(m_quietZoneModules);
        for (int row = 0; row < result.size; ++row) {
            int col = 0;
            while (col < result.size) {
                if (!result.isDark(col, row)) {
                    ++col;
                    continue;
                }
                // Merge horizontal runs so far fewer primitives are emitted.
                int runEnd = col;
                while (runEnd + 1 < result.size && result.isDark(runEnd + 1, row))
                    ++runEnd;
                painter.fillRect(QRectF(baseX + col * moduleMm, baseY + row * moduleMm,
                                        double(runEnd - col + 1) * moduleMm, moduleMm),
                                 brush);
                col = runEnd + 1;
            }
        }
    }
    painter.restore();
}

QJsonObject QrObject::propertiesToJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("data"), m_data);
    o.insert(QStringLiteral("ecc"), names::qrEcc(m_ecc));
    o.insert(QStringLiteral("contentKind"), names::qrContentKind(m_kind));
    o.insert(QStringLiteral("foreground"), m_foreground.name(QColor::HexArgb));
    o.insert(QStringLiteral("background"), m_background.name(QColor::HexArgb));
    o.insert(QStringLiteral("quietZoneModules"), m_quietZoneModules);
    return o;
}

bool QrObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    Q_UNUSED(error);
    m_data = json.value(QStringLiteral("data")).toString(m_data);

    QrErrorCorrection ecc = m_ecc;
    if (names::qrEccFromString(json.value(QStringLiteral("ecc")).toString(), &ecc))
        m_ecc = ecc;

    QrContentKind kind = m_kind;
    if (names::qrContentKindFromString(json.value(QStringLiteral("contentKind")).toString(), &kind))
        m_kind = kind;

    const QColor fg(json.value(QStringLiteral("foreground")).toString());
    if (fg.isValid())
        m_foreground = fg;
    const QColor bg(json.value(QStringLiteral("background")).toString());
    if (bg.isValid())
        m_background = bg;

    setQuietZoneModules(json.value(QStringLiteral("quietZoneModules")).toInt(m_quietZoneModules));
    m_cacheKey.clear();
    return true;
}

QString QrObject::defaultName() const
{
    return QCoreApplication::translate("QrObject", "QR Code");
}

CardObjectPtr QrObject::cloneImpl() const
{
    auto copy = std::make_unique<QrObject>();
    copy->m_data = m_data;
    copy->m_ecc = m_ecc;
    copy->m_kind = m_kind;
    copy->m_foreground = m_foreground;
    copy->m_background = m_background;
    copy->m_quietZoneModules = m_quietZoneModules;
    return copy;
}

} // namespace occ
