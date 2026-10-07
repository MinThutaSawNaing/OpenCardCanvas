#include "core/BarcodeObject.h"

#include "personalization/TemplateEngine.h"
#include "rendering/RenderContext.h"
#include "rendering/TextLayout.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QPainter>
#include <QtMath>

namespace occ {

namespace {

// Usual floor for reliable card printing: roughly 0.0075 inch per narrow bar.
constexpr double kMinimumModuleMm = 0.19;
constexpr double kComfortableModuleMm = 0.25;

QColor colorFromJson(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isString())
        return fallback;
    const QColor c(value.toString());
    return c.isValid() ? c : fallback;
}

} // namespace

BarcodeObject::BarcodeObject() : CardObject(ObjectType::Barcode, ObjectId::createUuid())
{
    setRectMm(QRectF(5.0, 40.0, 45.0, 10.0));
    m_name = defaultName();
}

void BarcodeObject::setData(const QString &data)
{
    m_data = data;
}

void BarcodeObject::setSymbology(BarcodeSymbology symbology)
{
    m_symbology = symbology;
    m_name = defaultName();
}

void BarcodeObject::setHumanTextSizePt(double pt)
{
    if (!std::isfinite(pt))
        return;
    m_humanTextSizePt = qBound(2.0, pt, 72.0);
}

void BarcodeObject::setQuietZoneModules(int modules)
{
    m_quietZoneModules = qBound(0, modules, 40);
}

double BarcodeObject::minimumReliableModuleMm()
{
    return kMinimumModuleMm;
}

QString BarcodeObject::validationError() const
{
    if (m_data.trimmed().isEmpty())
        return QCoreApplication::translate("BarcodeObject", "The barcode has no data.");
    QString error;
    BarcodeEncoder::validate(m_data, m_symbology, &error);
    return error;
}

BarcodeEncoder::Result BarcodeObject::encode() const
{
    return BarcodeEncoder::encode(m_data, m_symbology, false);
}

BarcodeEncoder::Result BarcodeObject::encodeWith(const QHash<QString, QString> &values) const
{
    const QString payload = TemplateEngine::expand(m_data, values);
    return BarcodeEncoder::encode(payload, m_symbology, false);
}

bool BarcodeObject::moduleWidthIsRisky(double *moduleMmOut) const
{
    const BarcodeEncoder::Result result = encode();
    if (!result.ok || result.moduleCount() <= 0) {
        if (moduleMmOut)
            *moduleMmOut = 0.0;
        return false;
    }
    const int total = result.moduleCount() + 2 * m_quietZoneModules;
    const double moduleMm = widthMm() / double(total);
    if (moduleMmOut)
        *moduleMmOut = moduleMm;
    return moduleMm < kMinimumModuleMm;
}

void BarcodeObject::paintObject(QPainter &painter, const RenderContext &ctx) const
{
    const double wMm = widthMm();
    const double hMm = heightMm();
    if (wMm <= 0.0 || hMm <= 0.0)
        return;

    const QString payload = ctx.resolveText(m_data, id());

    const auto paintInvalid = [&](const QString &message) {
        painter.save();
        QPen pen(QColor(200, 40, 40, 220));
        pen.setWidthF(0.15);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 245, 245, 150));
        painter.drawRect(QRectF(0.0, 0.0, wMm, hMm));
        painter.restore();
        if (!message.isEmpty())
            ctx.addWarning(message, id());
    };

    if (payload.trimmed().isEmpty()) {
        paintInvalid(QCoreApplication::translate(
            "BarcodeObject", "A barcode has no data and cannot be generated."));
        return;
    }

    const BarcodeEncoder::Result result = BarcodeEncoder::encode(payload, m_symbology, false);
    if (!result.ok) {
        paintInvalid(result.error);
        return;
    }

    const int totalModules = result.moduleCount() + 2 * m_quietZoneModules;
    if (totalModules <= 0)
        return;
    const double moduleMm = wMm / double(totalModules);

    // Human readable text occupies the lower part of the object.
    double textAreaMm = 0.0;
    if (m_showHumanText && BarcodeEncoder::supportsHumanText(m_symbology)) {
        textAreaMm = qMin(hMm * 0.45,
                          TextLayout::measureHeightMm(m_humanTextSizePt, 100.0) * 1.25);
    }
    const double barAreaMm = qMax(0.0, hMm - textAreaMm);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    if (m_background.alpha() > 0)
        painter.fillRect(QRectF(0.0, 0.0, wMm, hMm), m_background);

    // Bars: merge runs of dark modules into single rectangles.
    if (m_foreground.alpha() > 0) {
        const QBrush brush(m_foreground);
        const int count = result.moduleCount();
        int i = 0;
        while (i < count) {
            if (!result.isDark(i)) {
                ++i;
                continue;
            }
            int runEnd = i;
            while (runEnd + 1 < count && result.isDark(runEnd + 1))
                ++runEnd;
            painter.fillRect(QRectF(moduleMm * double(m_quietZoneModules + i), 0.0,
                                    moduleMm * double(runEnd - i + 1), barAreaMm),
                             brush);
            i = runEnd + 1;
        }
    }

    if (textAreaMm > 0.0 && m_foreground.alpha() > 0) {
        const QString normalized = BarcodeEncoder::normalize(payload, m_symbology);
        const QString human = BarcodeEncoder::humanReadableText(normalized, m_symbology);
        if (!human.isEmpty()) {
            double usedPt = m_humanTextSizePt;
            // Shrink the caption if it would otherwise overflow the barcode.
            int guard = 0;
            while (TextLayout::measureWidthMm(human, usedPt, QStringLiteral("Arial"),
                                              false, false) > wMm
                   && usedPt > 2.0 && guard++ < 40) {
                usedPt -= qMax(0.1, usedPt * 0.06);
            }
            const QPainterPath path =
                TextLayout::outline(human, usedPt, QStringLiteral("Arial"), false, false);
            const QRectF bounds = path.boundingRect();
            const double textX = (wMm - bounds.width()) / 2.0 - bounds.left();
            const double textY = barAreaMm + (textAreaMm + bounds.height()) / 2.0 - bounds.bottom();
            painter.fillPath(path.translated(textX, textY), m_foreground);
        }
    }

    painter.restore();

    double actualModuleMm = 0.0;
    if (moduleWidthIsRisky(&actualModuleMm)) {
        ctx.addWarning(QCoreApplication::translate(
                           "BarcodeObject",
                           "The barcode is too narrow to print reliably: the narrowest bar "
                           "would be %1 mm, but %2 mm is recommended. Make the barcode wider "
                           "or shorten the data.")
                           .arg(actualModuleMm, 0, 'f', 3)
                           .arg(kMinimumModuleMm, 0, 'f', 2),
                       id());
    } else if (moduleMm < kComfortableModuleMm) {
        ctx.addWarning(QCoreApplication::translate(
                           "BarcodeObject",
                           "The barcode is narrow (%1 mm per module). It should still scan, "
                           "but a wider barcode is safer.")
                           .arg(moduleMm, 0, 'f', 3),
                       id());
    }
}

QJsonObject BarcodeObject::propertiesToJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("data"), m_data);
    o.insert(QStringLiteral("symbology"), names::barcodeSymbology(m_symbology));
    o.insert(QStringLiteral("showHumanText"), m_showHumanText);
    o.insert(QStringLiteral("humanTextSizePt"), m_humanTextSizePt);
    o.insert(QStringLiteral("foreground"), m_foreground.name(QColor::HexArgb));
    o.insert(QStringLiteral("background"), m_background.name(QColor::HexArgb));
    o.insert(QStringLiteral("quietZoneModules"), m_quietZoneModules);
    return o;
}

bool BarcodeObject::propertiesFromJson(const QJsonObject &json, QString *error)
{
    Q_UNUSED(error);
    m_data = json.value(QStringLiteral("data")).toString(m_data);

    BarcodeSymbology sym = m_symbology;
    if (names::barcodeSymbologyFromString(json.value(QStringLiteral("symbology")).toString(), &sym))
        m_symbology = sym;

    m_showHumanText = json.value(QStringLiteral("showHumanText")).toBool(m_showHumanText);
    setHumanTextSizePt(json.value(QStringLiteral("humanTextSizePt")).toDouble(m_humanTextSizePt));
    m_foreground = colorFromJson(json.value(QStringLiteral("foreground")), m_foreground);
    m_background = colorFromJson(json.value(QStringLiteral("background")), m_background);
    setQuietZoneModules(json.value(QStringLiteral("quietZoneModules")).toInt(m_quietZoneModules));
    return true;
}

QString BarcodeObject::defaultName() const
{
    return QCoreApplication::translate("BarcodeObject", "Barcode");
}

CardObjectPtr BarcodeObject::cloneImpl() const
{
    auto copy = std::make_unique<BarcodeObject>();
    copy->m_data = m_data;
    copy->m_symbology = m_symbology;
    copy->m_showHumanText = m_showHumanText;
    copy->m_humanTextSizePt = m_humanTextSizePt;
    copy->m_foreground = m_foreground;
    copy->m_background = m_background;
    copy->m_quietZoneModules = m_quietZoneModules;
    return copy;
}

} // namespace occ
