#include "simkin_bindings/effect_entity.h"

#include <algorithm>

namespace sk_bindings {

EffectEntity MakeEffect(int firstSprite, int lastSprite, int drawFlags, int x, int y, int z,
                        int animRate, int lifetime, int sizeX, int sizeY) {
    EffectEntity e;
    // FUN_10060744's own two writes that the case does not overwrite, and
    // then the case body in its real order.
    e.firstSprite = firstSprite;
    e.lastSprite = lastSprite;
    e.sprite = firstSprite;
    e.animCursor = firstSprite * kEffectFrameStep;
    e.animRate = animRate * kEffectTimeScale;
    e.life = lifetime * kEffectTimeScale;
    e.lifeMax = lifetime * kEffectTimeScale;
    // `if (arg7 == 0) entity->0x152 = 1` -- the test is on the raw script
    // argument, not on the scaled value, though the two agree.
    e.immortal = (lifetime == 0);
    e.x = x;
    e.y = y;
    e.z = static_cast<int16_t>(z & 0xffff);  // the real store is 16-bit
    e.sizeX = sizeX;
    e.sizeY = sizeY;
    e.drawFlags = drawFlags;
    return e;
}

void TickEffect(EffectEntity& effect, int frameDelta, int gravityUnits) {
    if (!effect.alive) return;

    // FUN_1005ef94's first block: the lifetime, which an immortal effect
    // skips outright.
    if (!effect.immortal) {
        effect.life -= frameDelta;
        if (effect.life < 1) {
            effect.alive = false;
            return;
        }
    }

    // FUN_100606d0: advance the cursor, then either loop or die. The real
    // code returns without updating the drawn slot when it dies, and does
    // update it after a loop-around.
    effect.animCursor += (frameDelta * effect.animRate) >> 8;
    if (effect.animCursor >= effect.lastSprite * kEffectFrameStep + kEffectFrameStep) {
        if (!effect.looping) {
            effect.alive = false;
            return;
        }
        effect.animCursor = effect.firstSprite * kEffectFrameStep;
    }
    effect.sprite = effect.animCursor >> 8;

    // Back in FUN_1005ef94: gravity, then the three position integrations.
    if (effect.gravity) {
        effect.vz = static_cast<int16_t>(effect.vz + ((frameDelta * gravityUnits) >> 8));
    }
    effect.x += (frameDelta * effect.vx) >> 8;
    effect.y += (frameDelta * effect.vy) >> 8;
    effect.z = static_cast<int16_t>(effect.z + ((frameDelta * effect.vz) >> 8));

    // The scale ramp, gated on the same immortality flag. `lifeMax` is 0
    // only for an immortal effect, so the divide is safe here, but guard
    // it anyway rather than depend on that.
    if (!effect.immortal && effect.lifeMax > 0) {
        int progress = ((effect.lifeMax - effect.life) * 0x100) / effect.lifeMax;
        effect.scale =
            effect.scaleStart + ((progress * (effect.scaleEnd - effect.scaleStart)) >> 8);
    }
}

int SpriteHalfExtent(int sizeScalar, int spritePixels) {
    auto stored = static_cast<int16_t>((sizeScalar * spritePixels) & 0xffff);
    return (stored * spritePixels) >> 8;
}

int EffectHalfWidth(const EffectEntity& effect, int spriteWidth) {
    return SpriteHalfExtent(effect.sizeX, spriteWidth);
}

int EffectHalfHeight(const EffectEntity& effect, int spriteHeight) {
    return SpriteHalfExtent(effect.sizeY, spriteHeight);
}

void PruneEffects(std::vector<EffectEntity>& effects) {
    effects.erase(std::remove_if(effects.begin(), effects.end(),
                                 [](const EffectEntity& e) { return !e.alive; }),
                  effects.end());
}

}  // namespace sk_bindings
