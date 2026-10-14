#pragma once
#include "gesture_config.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// Sparse block-matching optical flow, with zero-mean SAD for exposure tolerance.
// Fixed buffers, no heap allocation, no PC/OpenCV dependency.
class OpticalFlow {
public:
    static constexpr int Width = GestureConfig::Image::Width;
    static constexpr int Height = GestureConfig::Image::Height;
    static constexpr int PatchRadius = GestureConfig::Flow::PatchRadius;
    static constexpr int PatchStep = GestureConfig::Flow::PatchStep;
    static constexpr int SearchRadius = GestureConfig::Flow::SearchRadius;
    static constexpr int PatchPixels = (2 * PatchRadius + 1) * (2 * PatchRadius + 1);
    static constexpr int PatchColumns = (Width - 2 * PatchRadius + PatchStep - 1) / PatchStep;
    static constexpr int PatchRows = (Height - 2 * PatchRadius + PatchStep - 1) / PatchStep;
    static constexpr int MaxPatches = PatchColumns * PatchRows;
    static_assert(PatchRadius > 0 && PatchStep > 0 && SearchRadius > 0,
                  "Patch and search dimensions must be positive");
    static_assert(Width > 2 * PatchRadius && Height > 2 * PatchRadius,
                  "A patch must fit inside the image");
    static_assert(Width - GestureConfig::Flow::MinX > 2 * PatchRadius,
                  "A patch must fit inside the active region");
    struct Result {
        float x = 0, y = 0;
        int moving = 0, textured = 0, inliers = 0;
        bool global = false;
        float centerX = 0, centerY = 0;
        int edgeVotes = 0; // Inliers near an ROI edge, moving outward along the dominant raw axis.
    };

    Result estimate(const uint8_t *previous, const uint8_t *current, bool axisY) const {
        return estimateImpl<false>(previous, current, axisY);
    }
    Result estimate2D(const uint8_t *previous, const uint8_t *current) const {
        return estimateImpl<true>(previous, current, false);
    }
private:
    template<bool TwoDimensional>
    Result estimateImpl(const uint8_t *previous, const uint8_t *current, bool axisY) const {
        using namespace GestureConfig::Flow;
        Result result;
        int vx[MaxPatches], vy[MaxPatches];
        uint8_t pxs[TwoDimensional ? MaxPatches : 1], pys[TwoDimensional ? MaxPatches : 1];
        for (int y = PatchRadius; y < Height - PatchRadius; y += PatchStep) {
            for (int x = PatchRadius; x < Width - PatchRadius; x += PatchStep) {
                if (x - PatchRadius < MinX) continue;
                int total = 0;
                for (int j = -PatchRadius; j <= PatchRadius; ++j)
                    for (int i = -PatchRadius; i <= PatchRadius; ++i) total += current[(y+j)*Width+x+i];
                const int mean = total / PatchPixels;
                int texture = 0;
                for (int j = -PatchRadius; j <= PatchRadius; ++j)
                    for (int i = -PatchRadius; i <= PatchRadius; ++i)
                        texture += std::abs(current[(y+j)*Width+x+i] - mean);
                if (texture < MinTexture) continue;
                ++result.textured;
                const int zero = cost(previous, current, x, y, x, y);
                if (zero < MinStationaryCost) continue;
                int best = zero, bestX = 0, bestY = 0;
                int vectorCost[TwoDimensional ? (2 * SearchRadius + 1) * (2 * SearchRadius + 1) : 1];
                if (TwoDimensional) for (auto &value : vectorCost) value = 100000;
                int axisCost[2 * SearchRadius + 1];
                for (auto &value : axisCost) value = 100000;
                for (int dy = -SearchRadius; dy <= SearchRadius; ++dy) {
                    for (int dx = -SearchRadius; dx <= SearchRadius; ++dx) {
                        const int px = x - dx, py = y - dy;
                        if (px < MinX + PatchRadius || px >= Width - PatchRadius || py < PatchRadius || py >= Height - PatchRadius) continue;
                        const int error = cost(previous, current, px, py, x, y);
                        const int axis = (axisY ? dy : dx) + SearchRadius;
                        axisCost[axis] = std::min(axisCost[axis], error);
                        if (TwoDimensional) vectorCost[(dy + SearchRadius) * (2 * SearchRadius + 1) + dx + SearchRadius] = error;
                        if (error < best || (error == best && dx*dx+dy*dy < bestX*bestX+bestY*bestY)) {
                            best = error; bestX = dx; bestY = dy;
                        }
                    }
                }
                if ((bestX == 0 && bestY == 0) || best > MaxMatchCost ||
                    best * ImprovementDenominator > zero * ImprovementNumerator) continue;
                // Repeated stripes can match many offsets: require a unique
                // displacement along the gesture axis, not across a straight edge.
                int alternative = 100000;
                const int bestAxis = axisY ? bestY : bestX;
                for (int i = 0; i < 2 * SearchRadius + 1; ++i)
                    if (std::abs(i-SearchRadius-bestAxis) >= AlternativeDistance) alternative = std::min(alternative, axisCost[i]);
                if (TwoDimensional) {
                    alternative = 100000;
                    for (int dy = -SearchRadius; dy <= SearchRadius; ++dy)
                        for (int dx = -SearchRadius; dx <= SearchRadius; ++dx)
                            if (std::abs(dx - bestX) >= AlternativeDistance || std::abs(dy - bestY) >= AlternativeDistance)
                                alternative = std::min(alternative, vectorCost[(dy + SearchRadius) * (2 * SearchRadius + 1) + dx + SearchRadius]);
                }
                if (alternative <= best + UniquenessMargin) continue;
                if (TwoDimensional) { pxs[result.moving] = x; pys[result.moving] = y; }
                vx[result.moving] = bestX;
                vy[result.moving] = bestY;
                ++result.moving;
            }
        }
        if (TwoDimensional) return summarize2D(vx, vy, pxs, pys, result.moving, result.textured);
        return summarize(vx, vy, result.moving, result.textured, axisY);
    }

public:
    // Reduce patch vectors to one reliable motion. Shared by image estimation
    // and vector-level regression tests; input count must fit MaxPatches.
    static Result summarize(const int *vx, const int *vy, int moving, int textured, bool axisY) {
        using namespace GestureConfig::Flow;
        Result result;
        if (moving < 0 || moving > MaxPatches) return result;
        result.moving = moving;
        result.textured = textured;
        if (result.moving < MinMovingPatches) return result;
        int positive = 0, negative = 0;
        for (int i = 0; i < result.moving; ++i) {
            const int axis = axisY ? vy[i] : vx[i];
            if (axis > 0) ++positive;
            if (axis < 0) ++negative;
        }
        result.global = result.moving >= GlobalMinPatches && result.moving * 100 > result.textured * GlobalPercent;
        if (std::max(positive, negative) < MinDirectionVotes ||
            std::max(positive, negative) * 100 < result.moving * MinDirectionCoveragePercent ||
            std::max(positive, negative) < DirectionVoteRatio * std::min(positive, negative)) return result;
        const int sign = positive > negative ? 1 : -1;
        int sorted[MaxPatches], count = 0;
        for (int i = 0; i < result.moving; ++i) {
            const int axis = axisY ? vy[i] : vx[i];
            if (axis * sign > 0) sorted[count++] = axis;
        }
        std::sort(sorted, sorted+count);
        int median = sorted[count/2];
        // Mixed speeds can leave the median without enough support. Only in
        // that case, try a faster cluster with an absolute and relative minimum.
        int medianCount = 0;
        for (int i = 0; i < count; ++i)
            if (std::abs(sorted[i] - median) <= InlierDistance) ++medianCount;
        int fastCount = 0;
        bool fastCluster = false;
        for (int candidate = FastClusterMinAxisMotion;
             medianCount * InlierVoteRatio < positive + negative && candidate <= SearchRadius;
             ++candidate) {
            int candidateCount = 0;
            for (int i = 0; i < count; ++i)
                if (std::abs(sorted[i] - sign * candidate) <= InlierDistance) ++candidateCount;
            if (candidateCount >= FastClusterMinInliers &&
                candidateCount * FastClusterVoteRatio >= positive + negative && candidateCount > fastCount) {
                fastCount = candidateCount;
                median = sign * candidate;
                fastCluster = true;
            }
        }
        float sumX = 0, sumY = 0;
        for (int i = 0; i < result.moving; ++i) {
            const int axis = axisY ? vy[i] : vx[i];
            if (axis * sign > 0 && std::abs(axis-median) <= InlierDistance) {
                ++result.inliers;
                sumX += vx[i]; sumY += vy[i];
            }
        }
        if (result.inliers >= MinInliers && result.inliers * (fastCluster ? FastClusterVoteRatio : InlierVoteRatio) >= positive + negative) {
            result.x = sumX / result.inliers;
            result.y = sumY / result.inliers;
        }
        return result;
    }

    // A joint (dx,dy) cluster avoids independently combining unrelated x/y motions.
    static Result summarize2D(const int *vx, const int *vy, const uint8_t *px, const uint8_t *py,
                              int moving, int textured) {
        using namespace GestureConfig::Flow;
        Result result;
        if (moving < 0 || moving > MaxPatches) return result;
        result.moving = moving; result.textured = textured;
        result.global = moving >= GlobalMinPatches && moving * 100 > textured * GlobalPercent;
        auto supports = [&](int candidate, int sample) {
            // Keep the main displacement precise while allowing a little
            // lateral spread from a turning/deforming hand.
            const bool horizontal = std::abs(vx[candidate]) >= std::abs(vy[candidate]);
            return std::abs(vx[sample] - vx[candidate]) <= (horizontal ? InlierDistance : 2) &&
                   std::abs(vy[sample] - vy[candidate]) <= (horizontal ? 2 : InlierDistance);
        };
        int best = -1, support = 0;
        for (int i = 0; i < moving; ++i) {
            int count = 0;
            for (int j = 0; j < moving; ++j)
                if (supports(i, j)) ++count;
            if (count > support) { support = count; best = i; }
        }
        bool axisFallback = false, fallbackY = false;
        int fallbackMotion = 0;
        if (support < MinInliers || support * 100 < moving * MinDirectionCoveragePercent) {
            // Turning hands have spread in the perpendicular component. Reuse
            // the existing direction-vote/median filter only when it yields an
            // unambiguous dominant axis; never combine the two estimates.
            const auto horizontal = summarize(vx, vy, moving, textured, false);
            const auto vertical = summarize(vx, vy, moving, textured, true);
            const bool hx = std::fabs(horizontal.x) >= 1 && std::fabs(horizontal.x) >= 1.5f * std::fabs(horizontal.y);
            const bool vyAxis = std::fabs(vertical.y) >= 1 && std::fabs(vertical.y) >= 1.5f * std::fabs(vertical.x);
            if (!hx && !vyAxis) return result;
            if (hx && vyAxis && horizontal.inliers < 2 * vertical.inliers && vertical.inliers < 2 * horizontal.inliers) return result;
            fallbackY = vyAxis && (!hx || vertical.inliers > horizontal.inliers);
            fallbackMotion = static_cast<int>(std::round(fallbackY ? vertical.y : horizontal.x));
            axisFallback = true;
        }
        auto selected = [&](int i) {
            const int axis = fallbackY ? vy[i] : vx[i];
            return axisFallback ? axis * fallbackMotion > 0 && std::abs(axis - fallbackMotion) <= InlierDistance : supports(best, i);
        };
        for (int i = 0; i < moving; ++i) {
            if (!selected(i)) continue;
            ++result.inliers;
            result.x += vx[i]; result.y += vy[i];
            result.centerX += px[i]; result.centerY += py[i];
        }
        if (result.inliers < MinInliers) { result.inliers = 0; result.x = result.y = 0; return result; }
        result.x /= result.inliers; result.y /= result.inliers;
        if (axisFallback && (result.inliers * FastClusterVoteRatio < moving ||
            (fallbackY ? std::fabs(result.y) < 1.5f * std::fabs(result.x) : std::fabs(result.x) < 1.5f * std::fabs(result.y)))) {
            result.inliers = 0; result.x = result.y = 0; return result;
        }
        result.centerX /= result.inliers; result.centerY /= result.inliers;
        constexpr int edgeBand = PatchRadius + 4;
        for (int i = 0; i < moving; ++i) {
            if (!selected(i)) continue;
            const bool outward = std::fabs(result.x) >= std::fabs(result.y)
                ? (result.x > 0 ? px[i] >= Width - 1 - edgeBand : px[i] <= MinX + edgeBand)
                : (result.y > 0 ? py[i] >= Height - 1 - edgeBand : py[i] <= edgeBand);
            if (outward) ++result.edgeVotes;
        }
        return result;
    }

private:
    static int cost(const uint8_t *a, const uint8_t *b, int ax, int ay, int bx, int by) {
        int delta = 0;
        for (int j = -PatchRadius; j <= PatchRadius; ++j)
            for (int i = -PatchRadius; i <= PatchRadius; ++i)
                delta += b[(by+j)*Width+bx+i] - a[(ay+j)*Width+ax+i];
        delta /= PatchPixels;
        int error = 0;
        for (int j = -PatchRadius; j <= PatchRadius; ++j)
            for (int i = -PatchRadius; i <= PatchRadius; ++i)
                error += std::abs(b[(by+j)*Width+bx+i] - a[(ay+j)*Width+ax+i] - delta);
        return error;
    }
};
