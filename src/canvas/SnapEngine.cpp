#include "canvas/SnapEngine.h"

#include "core/Units.h"

#include <QtMath>

#include <cmath>

namespace occ {

namespace {

// One thing a dragged edge can line up with (internal to this file; the public
// SnapEngine::Candidate is a different, reporting-only type).
struct SnapCandidate
{
    double   value = 0.0;
    bool     fromCard = false;
    bool     fromGuide = false;
    bool     fromGrid = false;
    ObjectId objectId;
};

// The winning adjustment along one axis.
struct AxisSnap
{
    bool   snapped = false;
    double delta = 0.0;     // how much the proposed value must move
    double lineMm = 0.0;    // the value to draw as an alignment line
    bool   fromCard = false;
    bool   fromGuide = false;
    bool   fromGrid = false;
};

void addObjectsOnSide(const CardSide &side, const ObjectId &exclude,
                      QVector<SnapCandidate> *x, QVector<SnapCandidate> *y)
{
    for (CardObject *object : side.objects()) {
        if (!object || object->id() == exclude)
            continue;
        // Hidden objects still snap: they are part of the layout, and a user who
        // hid one still expects a neighbour to line up with it.
        for (double value : SnapEngine::objectCandidateValuesX(*object)) {
            SnapCandidate candidate;
            candidate.value = value;
            candidate.objectId = object->id();
            x->append(candidate);
        }
        for (double value : SnapEngine::objectCandidateValuesY(*object)) {
            SnapCandidate candidate;
            candidate.value = value;
            candidate.objectId = object->id();
            y->append(candidate);
        }
    }
}

void addGuides(const QVector<GuideModel::Guide> &guides, QVector<SnapCandidate> *x,
               QVector<SnapCandidate> *y)
{
    for (const GuideModel::Guide &guide : guides) {
        SnapCandidate candidate;
        candidate.value = guide.posMm;
        candidate.fromGuide = true;
        (guide.horizontal ? y : x)->append(candidate);
    }
}

void addCard(const CardGeometry &geom, QVector<SnapCandidate> *x, QVector<SnapCandidate> *y)
{
    for (double value : SnapEngine::cardCandidateValuesX(geom)) {
        SnapCandidate candidate;
        candidate.value = value;
        candidate.fromCard = true;
        x->append(candidate);
    }
    for (double value : SnapEngine::cardCandidateValuesY(geom)) {
        SnapCandidate candidate;
        candidate.value = value;
        candidate.fromCard = true;
        y->append(candidate);
    }
}

AxisSnap snapAxis(const QVector<double> &targets, const QVector<SnapCandidate> &candidates,
                  const SnapEngine::Options &options, double tolerance)
{
    AxisSnap best;
    if (tolerance <= 0.0 || candidates.isEmpty() || targets.isEmpty())
        return best;

    double bestDistance = tolerance + 1.0;
    for (const double target : targets) {
        for (const SnapCandidate &candidate : candidates) {
            const double distance = qAbs(target - candidate.value);
            if (distance > tolerance || distance >= bestDistance)
                continue;
            // Strictly closer wins, so a candidate added earlier (the card, then
            // guides, then objects) keeps a tie.
            bestDistance = distance;
            best.snapped = true;
            best.delta = candidate.value - target;
            best.lineMm = candidate.value;
            best.fromCard = candidate.fromCard;
            best.fromGuide = candidate.fromGuide;
            best.fromGrid = candidate.fromGrid;
        }
    }
    if (best.snapped)
        return best;

    // The grid is a fallback: it only applies when nothing more meaningful is in
    // range, so a grid line can never beat an object edge.
    if (options.toGrid && options.gridSpacingMm > 0.0) {
        double nearestDistance = tolerance + 1.0;
        for (const double target : targets) {
            const double nearest =
                qRound(target / options.gridSpacingMm) * options.gridSpacingMm;
            const double distance = qAbs(nearest - target);
            if (distance > tolerance || distance >= nearestDistance)
                continue;
            nearestDistance = distance;
            best.snapped = true;
            best.delta = nearest - target;
            best.lineMm = nearest;
            best.fromGrid = true;
        }
    }
    return best;
}

// SnapEngine member definitions start here.
} // namespace

double SnapEngine::toleranceMm(double tolerancePx, double pxPerMm)
{
    if (pxPerMm <= 0.0)
        return 0.0;
    return qMax(0.0, tolerancePx) / pxPerMm;
}

QVector<double> SnapEngine::cardCandidateValuesX(const CardGeometry &geom)
{
    const QRectF bounds = geom.boundsMm();
    return { bounds.left(), bounds.center().x(), bounds.right() };
}

QVector<double> SnapEngine::cardCandidateValuesY(const CardGeometry &geom)
{
    const QRectF bounds = geom.boundsMm();
    return { bounds.top(), bounds.center().y(), bounds.bottom() };
}

QVector<double> SnapEngine::objectCandidateValuesX(const CardObject &obj)
{
    const QRectF box = obj.boundingRectMm();
    return { box.left(), box.center().x(), box.right() };
}

QVector<double> SnapEngine::objectCandidateValuesY(const CardObject &obj)
{
    const QRectF box = obj.boundingRectMm();
    return { box.top(), box.center().y(), box.bottom() };
}

SnapEngine::Result SnapEngine::snapMove(const CardObject &moving,
                                       const QPointF &proposedTopLeftMm,
                                       const CardSide &side, const CardGeometry &geom,
                                       const QVector<GuideModel::Guide> &guides,
                                       const Options &options, double pxPerMm)
{
    Result result;
    result.adjustedMm = proposedTopLeftMm;

    const double tolerance = toleranceMm(options.tolerancePx, pxPerMm);
    if (tolerance <= 0.0)
        return result;

    QVector<SnapCandidate> xCandidates;
    QVector<SnapCandidate> yCandidates;
    if (options.toCard)
        addCard(geom, &xCandidates, &yCandidates);
    if (options.toGuides)
        addGuides(guides, &xCandidates, &yCandidates);
    if (options.toObjects)
        addObjectsOnSide(side, moving.id(), &xCandidates, &yCandidates);

    // Both edges and the centre of the moving object are targets, which is what
    // makes centring and edge-to-edge alignment both work.
    const double width = moving.widthMm();
    const double height = moving.heightMm();
    const QVector<double> xTargets = { proposedTopLeftMm.x(),
                                       proposedTopLeftMm.x() + width / 2.0,
                                       proposedTopLeftMm.x() + width };
    const QVector<double> yTargets = { proposedTopLeftMm.y(),
                                       proposedTopLeftMm.y() + height / 2.0,
                                       proposedTopLeftMm.y() + height };

    const AxisSnap x = snapAxis(xTargets, xCandidates, options, tolerance);
    const AxisSnap y = snapAxis(yTargets, yCandidates, options, tolerance);

    if (x.snapped) {
        result.adjustedMm.setX(result.adjustedMm.x() + x.delta);
        result.snappedX = true;
        result.guideXmm = x.lineMm;
        result.fromCardX = x.fromCard;
        result.fromGridX = x.fromGrid;
    }
    if (y.snapped) {
        result.adjustedMm.setY(result.adjustedMm.y() + y.delta);
        result.snappedY = true;
        result.guideYmm = y.lineMm;
        result.fromCardY = y.fromCard;
        result.fromGridY = y.fromGrid;
    }
    return result;
}

SnapEngine::Result SnapEngine::snapResize(const CardObject &moving, bool horizontalEdge,
                                         bool leadingEdge, double proposedMm,
                                         const CardSide &side, const CardGeometry &geom,
                                         const QVector<GuideModel::Guide> &guides,
                                         const Options &options, double pxPerMm)
{
    Result result;
    result.adjustedMm = QPointF(proposedMm, proposedMm);

    const double tolerance = toleranceMm(options.tolerancePx, pxPerMm);
    if (tolerance <= 0.0)
        return result;

    QVector<SnapCandidate> xCandidates;
    QVector<SnapCandidate> yCandidates;
    if (options.toCard)
        addCard(geom, &xCandidates, &yCandidates);
    if (options.toGuides)
        addGuides(guides, &xCandidates, &yCandidates);
    if (options.toObjects)
        addObjectsOnSide(side, moving.id(), &xCandidates, &yCandidates);

    // While resizing, the edge being dragged is the target, and the opposite edge
    // and the centre of the same object are also candidates, so an edge can be
    // lined up with the object's own centre or with its other side.
    QVector<SnapCandidate> &candidates = horizontalEdge ? yCandidates : xCandidates;
    const QRectF box = moving.boundingRectMm();
    if (horizontalEdge) {
        SnapCandidate own;
        own.value = box.center().y();
        own.objectId = moving.id();
        candidates.prepend(own);
        SnapCandidate opposite;
        opposite.value = leadingEdge ? box.bottom() : box.top();
        opposite.objectId = moving.id();
        candidates.prepend(opposite);
    } else {
        SnapCandidate own;
        own.value = box.center().x();
        own.objectId = moving.id();
        candidates.prepend(own);
        SnapCandidate opposite;
        opposite.value = leadingEdge ? box.right() : box.left();
        opposite.objectId = moving.id();
        candidates.prepend(opposite);
    }

    const QVector<double> targets = { proposedMm };
    const AxisSnap axis = snapAxis(targets, candidates, options, tolerance);
    if (!axis.snapped)
        return result;

    const double snapped = proposedMm + axis.delta;
    if (horizontalEdge) {
        result.adjustedMm = QPointF(proposedMm, snapped);
        result.snappedY = true;
        result.guideYmm = axis.lineMm;
        result.fromCardY = axis.fromCard;
        result.fromGridY = axis.fromGrid;
    } else {
        result.adjustedMm = QPointF(snapped, proposedMm);
        result.snappedX = true;
        result.guideXmm = axis.lineMm;
        result.fromCardX = axis.fromCard;
        result.fromGridX = axis.fromGrid;
    }
    return result;
}

} // namespace occ
