#pragma once
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "render3d/camera.h"
#include "render3d/zone_renderer.h"

namespace sk {

// M119: the Frame Rate setting's in-between frames.
//
// The game keeps ticking at the engine's fixed 25Hz (engine/game_clock.h) --
// every AI step, every physics step, every script timer is tuned to that
// 40ms step, and running any of it faster would change the game rather than
// how smoothly it is shown (docs/RENDER_LOOP.md). So a higher frame rate
// draws *more pictures of the same ticks*: each tick hands over what it
// would have drawn -- the camera, and the render lists of models and
// billboards -- and the frames until the next tick show the world part-way
// between the last two of those, the classic fixed-timestep interpolation.
//
// The cost is one tick of latency: a frame drawn at `alpha` shows
// `lerp(previous, current, alpha)`, so what the latest tick decided is fully
// on screen 40ms later. At Original frame rate none of this runs and each
// tick is drawn the moment it finishes, exactly as before M119.
//
// What moves between ticks: the camera (position, heading, pitch) and every
// render-list entry with a nonzero `interpKey` found in both ticks. What
// does not: animation frames (a model's keyframes are drawn as the tick
// picked them), and anything that appeared or vanished this tick, which is
// drawn where it is. A jump too large to be motion -- a teleport, a level
// change, a door the script flipped half a turn -- is drawn where it landed
// rather than swept across the screen.
struct InterpolationLimits {
    // Raw world units in one tick. The player's own top speed is a few
    // hundred; nothing that walks, falls or flies gets near four tiles.
    float maxStep = 1024.0f;
    // Radians in one tick -- far past any turn rate the game has.
    float maxTurn = 1.5707963f;
};

inline float LerpScalar(float a, float b, float t) { return a + (b - a) * t; }

// Shortest way round, so a heading crossing +-pi turns through the small
// angle rather than spinning the long way.
inline float AngleDelta(float from, float to) {
    constexpr float kTwoPi = 6.2831853f;
    float d = std::fmod(to - from, kTwoPi);
    if (d > 3.1415927f) d -= kTwoPi;
    if (d < -3.1415927f) d += kTwoPi;
    return d;
}

inline bool IsJump(float ax, float ay, float az, float bx, float by, float bz,
                   const InterpolationLimits& limits) {
    const float dx = bx - ax, dy = by - ay, dz = bz - az;
    return dx * dx + dy * dy + dz * dz > limits.maxStep * limits.maxStep;
}

inline Camera LerpCamera(const Camera& a, const Camera& b, float t,
                         const InterpolationLimits& limits = {}) {
    if (IsJump(a.x, a.y, a.z, b.x, b.y, b.z, limits)) return b;
    const float dYaw = AngleDelta(a.yaw, b.yaw);
    if (std::fabs(dYaw) > limits.maxTurn) return b;
    Camera c = b;
    c.x = LerpScalar(a.x, b.x, t);
    c.y = LerpScalar(a.y, b.y, t);
    c.z = LerpScalar(a.z, b.z, t);
    c.yaw = a.yaw + dYaw * t;
    c.pitch = LerpScalar(a.pitch, b.pitch, t);
    return c;
}

class FrameInterpolator {
public:
    // One tick's worth of what to draw. The previous tick's becomes the
    // interpolation's starting point; with no previous tick (the first one
    // after Reset) the frame is simply this one.
    void Push(const Camera& camera, const std::vector<PlacedEntity>& entities,
              const std::vector<SpriteBillboard>& billboards) {
        if (has_) {
            prevCamera_ = camera_;
            prevEntities_.swap(entities_);
            prevBillboards_.swap(billboards_);
            hasPrev_ = true;
        }
        camera_ = camera;
        entities_ = entities;
        billboards_ = billboards;
        has_ = true;
        IndexPrevious();
    }

    // Forget the history: the next Push starts fresh. For a level load, a
    // return from a menu, anything after which the last tick drawn is not
    // the one before this.
    void Reset() {
        has_ = false;
        hasPrev_ = false;
        prevEntityIndex_.clear();
        prevBillboardIndex_.clear();
    }

    bool ready() const { return has_; }

    // The frame `alpha` of the way from the previous tick to the latest.
    void Build(float alpha, Camera& camera, std::vector<PlacedEntity>& entities,
               std::vector<SpriteBillboard>& billboards) const {
        alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
        entities = entities_;
        billboards = billboards_;
        if (!hasPrev_) {
            camera = camera_;
            return;
        }
        camera = LerpCamera(prevCamera_, camera_, alpha, limits_);
        for (PlacedEntity& e : entities) {
            if (e.interpKey == 0) continue;
            auto it = prevEntityIndex_.find(e.interpKey);
            if (it == prevEntityIndex_.end()) continue;
            const PlacedEntity& p = prevEntities_[it->second];
            if (IsJump(p.x, p.y, p.z, e.x, e.y, e.z, limits_)) continue;
            const float dYaw = AngleDelta(p.yaw, e.yaw);
            e.x = LerpScalar(p.x, e.x, alpha);
            e.y = LerpScalar(p.y, e.y, alpha);
            e.z = LerpScalar(p.z, e.z, alpha);
            if (std::fabs(dYaw) <= limits_.maxTurn) e.yaw = p.yaw + dYaw * alpha;
        }
        for (SpriteBillboard& b : billboards) {
            if (b.interpKey == 0) continue;
            auto it = prevBillboardIndex_.find(b.interpKey);
            if (it == prevBillboardIndex_.end()) continue;
            const SpriteBillboard& p = prevBillboards_[it->second];
            if (IsJump(p.x, p.y, p.z, b.x, b.y, b.z, limits_)) continue;
            b.x = LerpScalar(p.x, b.x, alpha);
            b.y = LerpScalar(p.y, b.y, alpha);
            b.z = LerpScalar(p.z, b.z, alpha);
        }
    }

private:
    void IndexPrevious() {
        prevEntityIndex_.clear();
        prevBillboardIndex_.clear();
        if (!hasPrev_) return;
        // A creature's attached weapon is pushed with its body's key: it is
        // drawn with the body's own transform, so pairing it with the
        // body's previous entry is exactly right. emplace keeps the first.
        for (size_t i = 0; i < prevEntities_.size(); ++i) {
            const uint64_t key = prevEntities_[i].interpKey;
            if (key != 0) prevEntityIndex_.emplace(key, i);
        }
        for (size_t i = 0; i < prevBillboards_.size(); ++i) {
            if (prevBillboards_[i].interpKey != 0) {
                prevBillboardIndex_.emplace(prevBillboards_[i].interpKey, i);
            }
        }
    }

    InterpolationLimits limits_;
    bool has_ = false;
    bool hasPrev_ = false;
    Camera camera_, prevCamera_;
    std::vector<PlacedEntity> entities_, prevEntities_;
    std::vector<SpriteBillboard> billboards_, prevBillboards_;
    std::unordered_map<uint64_t, size_t> prevEntityIndex_;
    std::unordered_map<uint64_t, size_t> prevBillboardIndex_;
};

}  // namespace sk
