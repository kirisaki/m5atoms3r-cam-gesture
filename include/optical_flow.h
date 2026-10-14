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
    };

    Result estimate(const uint8_t *previous, const uint8_t *current, bool axisY) const {
        using namespace GestureConfig::Flow;
        Result result;
        int vx[MaxPatches], vy[MaxPatches];
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
                int axisCost[2 * SearchRadius + 1];
                for (auto &value : axisCost) value = 100000;
                for (int dy = -SearchRadius; dy <= SearchRadius; ++dy) {
                    for (int dx = -SearchRadius; dx <= SearchRadius; ++dx) {
                        const int px = x - dx, py = y - dy;
                        if (px < MinX + PatchRadius || px >= Width - PatchRadius || py < PatchRadius || py >= Height - PatchRadius) continue;
                        const int error = cost(previous, current, px, py, x, y);
                        const int axis = (axisY ? dy : dx) + SearchRadius;
                        axisCost[axis] = std::min(axisCost[axis], error);
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
                if (alternative <= best + UniquenessMargin) continue;
                vx[result.moving] = bestX;
                vy[result.moving] = bestY;
                ++result.moving;
            }
        }
        return summarize(vx, vy, result.moving, result.textured, axisY);
    }

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
