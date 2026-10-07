#pragma once

#include "canvas/GuideModel.h"
#include "core/CardGeometry.h"
#include "core/CardObject.h"
#include "core/CardSide.h"
#include "core/CardTypes.h"

#include <QPointF>
#include <QRectF>
#include <QVector>

// ---------------------------------------------------------------------------
// SnapEngine - where a dragged object should actually land.
//
// Snapping is the difference between a card that looks designed and a card that
// looks assembled: objects that line up exactly, edges that share a millimetre
// value, a photo centred to the micron rather than to the pixel.
//
// The engine is pure - it takes the moving object, a proposed position, the
// side, the geometry, the guides and the tolerance, and returns the adjusted
// position plus which lines (if any) the user should see drawn. No widget, no
// document, no side effects: the canvas simply draws the returned lines.
//
// Candidates are, in the order they win ties:
//   1. card edges and centre
//   2. guides
//   3. other objects' edges and centres
//   4. the grid
// The moving object is excluded from candidate 3 so it cannot snap to itself.
// ---------------------------------------------------------------------------
namespace occ {

class SnapEngine
{
public:
    struct Options
    {
        bool   toCard = true;
        bool   toGuides = true;
        bool   toObjects = true;
        bool   toGrid = false;
        double gridSpacingMm = 5.0;
        double tolerancePx = 8.0;
    };

    struct Result
    {
        QPointF adjustedMm;
        bool    snappedX = false;
        bool    snappedY = false;
        // Alignment lines to draw, in card millimetres. Only meaningful when the
        // matching snapped flag is set.
        double  guideXmm = 0.0;
        double  guideYmm = 0.0;
        bool    fromCardX = false;
        bool    fromCardY = false;
        bool    fromGridX = false;
        bool    fromGridY = false;
    };

    // Snaps a moving object's proposed top-left corner.
    static Result snapMove(const CardObject &moving, const QPointF &proposedTopLeftMm,
                           const CardSide &side, const CardGeometry &geom,
                           const QVector<GuideModel::Guide> &guides,
                           const Options &options, double pxPerMm);

    // Snaps a single edge while resizing. `proposedMm` is the proposed position
    // of that edge; `horizontalEdge` selects a Y (true) or an X (false) edge.
    // The object's other edges are used as additional candidates, so resizing
    // lines an edge up with the opposite one of another object.
    static Result snapResize(const CardObject &moving, bool horizontalEdge, bool leadingEdge,
                             double proposedMm, const CardSide &side,
                             const CardGeometry &geom,
                             const QVector<GuideModel::Guide> &guides,
                             const Options &options, double pxPerMm);

    // --- candidate introspection (also used by the tests and by the "what am I
    //     snapping to?" status hint) ------------------------------------------
    // A single snapping candidate on one axis.
    struct Candidate
    {
        double value = 0.0;
        bool   fromCard = false;
        bool   fromGuide = false;
        bool   fromGrid = false;
        ObjectId objectId;      // null for card, guide and grid candidates
    };

    static QVector<double> objectCandidateValuesX(const CardObject &obj);
    static QVector<double> objectCandidateValuesY(const CardObject &obj);
    static QVector<double> cardCandidateValuesX(const CardGeometry &geom);
    static QVector<double> cardCandidateValuesY(const CardGeometry &geom);

    // Distance in millimetres that `tolerancePx` represents at `pxPerMm`.
    static double toleranceMm(double tolerancePx, double pxPerMm);
};

} // namespace occ
