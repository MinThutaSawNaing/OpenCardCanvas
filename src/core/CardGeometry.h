#pragma once

#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVector>

// ---------------------------------------------------------------------------
// CardGeometry - the physical description of the card being designed.
//
// Everything is stored in millimetres; the render resolution is a separate
// concern so that the same design can be exported at 300 dpi, printed at
// 600 dpi or displayed on screen without the design changing.
// ---------------------------------------------------------------------------
namespace occ {

struct CardPreset
{
    QString id;          // stable identifier, written to the project file
    QString name;        // localised display name
    double  widthMm = 85.60;
    double  heightMm = 53.98;
    bool    isCustom = false;
};

inline constexpr double kMinCardMm = 10.0;
inline constexpr double kMaxCardMm = 500.0;
inline constexpr int    kMinDpi    = 72;
inline constexpr int    kMaxDpi    = 1200;

class CardGeometry
{
public:
    CardGeometry();

    // --- presets ------------------------------------------------------------
    static QVector<CardPreset> presets();
    static CardPreset preset(const QString &id);
    // Returns the preset whose dimensions match, or the custom preset.
    static QString matchingPresetId(double widthMm, double heightMm);

    // --- dimensions ---------------------------------------------------------
    double widthMm() const { return m_widthMm; }
    double heightMm() const { return m_heightMm; }
    void setWidthMm(double mm);
    void setHeightMm(double mm);
    void setSize(double widthMm, double heightMm);

    // Landscape means width >= height. Turning a card portrait swaps the two
    // dimensions, which is how card printers describe orientation.
    bool isLandscape() const { return m_widthMm >= m_heightMm; }
    void setOrientation(bool landscape);

    // --- output -------------------------------------------------------------
    int renderDpi() const { return m_renderDpi; }
    void setRenderDpi(int dpi);

    // Bleed extends the printable area beyond the trim edge. Objects are not
    // clipped to the trim edge when bleed is in use.
    double bleedMm() const { return m_bleedMm; }
    void setBleedMm(double mm);

    // --- preset identity ----------------------------------------------------
    QString presetId() const { return m_presetId; }
    void setPresetId(const QString &id) { m_presetId = id; }

    // --- derived ------------------------------------------------------------
    double aspectRatio() const;
    QSize pixelSize() const;                         // at renderDpi()
    QSize pixelSize(int dpi) const;
    // Trim box, origin at (0,0).
    QRectF boundsMm() const;
    // Trim box expanded by the bleed on all four sides.
    QRectF bleedBoundsMm() const;
    // Print area including bleed, in pixels at renderDpi().
    QSizeF printableSizePx() const;

    bool isValid(QString *error = nullptr) const;

    // --- persistence --------------------------------------------------------
    QJsonObject toJson() const;
    // Returns false and fills `error` when the stored geometry is unusable.
    bool fromJson(const QJsonObject &json, QString *error);

    static CardGeometry defaultGeometry();
    static CardGeometry isoId1();
    static CardGeometry isoId1Portrait();

private:
    double  m_widthMm  = 85.60;
    double  m_heightMm = 53.98;
    int     m_renderDpi = 300;
    double  m_bleedMm = 0.0;
    QString m_presetId = QStringLiteral("iso-id1");
};

} // namespace occ
