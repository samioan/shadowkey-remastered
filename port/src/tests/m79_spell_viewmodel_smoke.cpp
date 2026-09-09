// M79 smoke test: the spell's viewmodel, the cast animation, the fireball,
// and the swing speed M78 got wrong.
//
// Reported: equipping the Blaze spell shows no hands, no cast animation and
// no fireball, where a weapon shows all three. Four findings, all of them
// constructor writes this port never made:
//
//   | class | ctor | `+0x19c` sprite | `+0x180` frames | `+0x184` scale |
//   |-------|------|-----------------|-----------------|----------------|
//   | base item | `FUN_1006c960` | 0 | 0 | `0x100` |
//   | weapon (cat 4, 16) | `FUN_1002ce9c` | -- | -- | **`0x300`** |
//   | spell (cat 5, 14) | `FUN_10047740` | **`0x98`** | **5** | -- |
//
// So every spell in the game already owns global.spr slot 152 and a
// five-frame animation without a single spell script calling
// SetWeaponSprite -- and every weapon's swing accumulator drains three
// times as fast as M78 assumed, which is the *other* reported symptom
// ("the weapon swinging animation is really slow") coming back with a real
// cause this time.
//
// The fireball is the third half of it: a spell projectile is an
// animated-sprite entity (`FUN_1008b25c`), not a mesh, and this port had
// no billboard pass at all. That draw also settles what `+0x138` is --
// Ghidra mis-typed `FUN_1004f91c`'s parameter list by one, so M63 read a
// blend level as a size scale.
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/sprite_archive.h"
#include "assets/string_table.h"
#include "graphics/backbuffer.h"
#include "render3d/camera.h"
#include "render3d/zone_renderer.h"
#include "simkin_bindings/effect_entity.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/spell_projectile.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

std::string Join(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    return s;
}

// Every distinct sprite slot one swing/cast draws, in order.
std::vector<int> DrawnSlots(sk_bindings::WeaponViewmodel& vm) {
    std::vector<int> slots;
    int guard = 0;
    while (vm.swingAccum != 0 && guard++ < 1000) {
        sk_bindings::ViewmodelDraw d = sk_bindings::ResolveViewmodelDraw(vm);
        if (d.visible && (slots.empty() || slots.back() != d.spriteSlot)) {
            slots.push_back(d.spriteSlot);
        }
        sk_bindings::TickWeaponViewmodel(vm, 0);
    }
    return slots;
}

}  // namespace

int main(int argc, char** argv) {
    using Item = sk_bindings::ItemExecutable;
    using VM = sk_bindings::WeaponViewmodel;

    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    strings.Load(std::string(scriptRoot) + "/stringtable.eng");
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m79_spell_viewmodel_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    // The real creation path -- the category is what picks the C++ class,
    // and the class constructor is the whole subject of this test.
    auto makeItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        const int typeId = stack.level().TypeIdForScript(rel);
        if (typeId < 0) return nullptr;
        return stack.level().CreateItem(typeId, true);
    };

    std::printf("=== M79: the spell's viewmodel, its cast, and the fireball ===\n\n");

    // entities.txt: `50 30 5 blaze.s` (category 5, the spell itself) and
    // `4902 92 14 spells\U_Blaze_lvl5.s` (category 14, its scroll).
    std::unique_ptr<Item> blaze = makeItem("blaze.s");
    std::unique_ptr<Item> scroll = makeItem("spells/u_blaze_lvl5.s");
    std::unique_ptr<Item> club = makeItem("weapons/club.s");
    std::unique_ptr<Item> bread = makeItem("items/bread.s");
    Check(blaze && scroll && club && bread,
          "blaze.s (spell), U_Blaze_lvl5.s (scroll), club.s (weapon), bread.s (consumable) load");
    if (!blaze || !scroll || !club || !bread) {
        std::printf("\nm79_spell_viewmodel_smoke: FAILED\n");
        return 1;
    }

    // ---- 1. The constructor writes ----
    //
    // Category 5 runs `FUN_10047740` and category 14 (a scroll) derives
    // from it, adding only the scroll flag -- so both get the same art.
    {
        Check(blaze->itemType() == sk_bindings::kItemTypeSpell &&
                  scroll->itemType() == sk_bindings::kItemTypeSpell,
              "both spell categories (5 and 14) report kItemTypeSpell");
        Check(blaze->weaponSprite() == sk_bindings::kSpellViewmodelSprite &&
                  scroll->weaponSprite() == sk_bindings::kSpellViewmodelSprite,
              "...and both carry the Spell constructor's global.spr slot 152");
        Check(blaze->animationFrames() == sk_bindings::kSpellAnimationFrames,
              "...and its five animation frames, with no SetAnimationFrames anywhere in the script");
        Check(blaze->equipSlot() == sk_bindings::kEquipSlotRight,
              "...and `+0x1c0 = 1`, so a spell is a right-hand item");
        Check(blaze->reloadSpeed() == sk_bindings::kDefaultReloadSpeed,
              "a spell keeps the base item constructor's `+0x184 = 0x100`");
        Check(club->reloadSpeed() == sk_bindings::kWeaponReloadSpeed,
              "a weapon takes the Weapon constructor's `+0x184 = 0x300` -- three times as fast");
        Check(bread->weaponSprite() < 0 && bread->reloadSpeed() ==
                                               sk_bindings::kDefaultReloadSpeed,
              "a consumable gets neither: no viewmodel and the plain drain rate");
    }

    // ---- 2. What the scale is worth ----
    //
    // `delta = frameDelta * 4; if (+0x184 != 0x100) delta = +0x184 * delta
    // >> 8;` -- 40 a tick unscaled, 120 for a weapon.
    {
        Check(sk_bindings::SwingDrainPerFrame(nullptr) == 40,
              "no item: the unscaled 4 * frameDelta = 40 a tick");
        Check(sk_bindings::SwingDrainPerFrame(blaze.get()) == 40,
              "a spell: the same 40, so a cast animation runs the full 1792 units");
        Check(sk_bindings::SwingDrainPerFrame(club.get()) == 120,
              "a weapon: 0x300 * 40 >> 8 = 120 a tick");
    }

    // ---- 3. The cast animation ----
    //
    // `FUN_10042394` case 2 seeds `(frames + 2) << 8` from whatever is on
    // `player+0x204`, and after M79 that is the spell itself -- so the
    // drawn slots are the spell strip's own, 153..157 after the idle 152.
    {
        VM vm;
        sk_bindings::NotifyWeaponChanged(vm, blaze.get());
        while (vm.swapTimer != 0) sk_bindings::TickWeaponViewmodel(vm, 0);
        sk_bindings::ViewmodelDraw idle = sk_bindings::ResolveViewmodelDraw(vm);
        Check(idle.visible && idle.spriteSlot == 152,
              "a spell in hand draws slot 152 -- the idle 'holding a spell' pose");

        sk_bindings::StartSpellSwing(vm);
        Check(vm.swingAccum == ((sk_bindings::kSpellAnimationFrames + 2) << 8),
              "casting seeds (5+2)<<8 = 1792, the same melee seed a weapon gets");
        std::vector<int> slots = DrawnSlots(vm);
        bool run = slots.size() == 5;
        for (int i = 0; run && i < 5; ++i) run = slots[static_cast<size_t>(i)] == 153 + i;
        Check(run, "the cast draws 153,154,155,156,157 and stops (got " + Join(slots) + ")");
        // 1792 - 512 = 1280 is exactly 32 drains of 40, so a spell lands on
        // the `acc == 0x200` overrun M78 fixed -- the guard's beneficiary
        // moved from the sword to the cast when the drain rate changed.
        Check(slots.size() == 5 && slots.back() == 157,
              "...never reaching 158, the slot after the spell's own strip");
    }
    {
        VM vm;
        sk_bindings::NotifyWeaponChanged(vm, blaze.get());
        while (vm.swapTimer != 0) sk_bindings::TickWeaponViewmodel(vm, 0);
        sk_bindings::StartSpellSwing(vm);
        int ticks = 0;
        while (vm.swinging() && ticks < 1000) {
            sk_bindings::TickWeaponViewmodel(vm, 0);
            ++ticks;
        }
        Check(ticks == 45, "a cast animation runs 45 ticks -- 1792 / 40, i.e. 1.8s at 25Hz");
    }

    // ---- 4. The weapon swing, at the rate the Weapon constructor sets ----
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        vm.swingVariant = 0;
        std::vector<int> slots = DrawnSlots(vm);
        bool run = slots.size() == 5;
        for (int i = 0; run && i < 5; ++i) run = slots[static_cast<size_t>(i)] == 89 + i;
        Check(run, "the club still draws exactly 89..93 at the faster drain (got " + Join(slots) +
                       ")");
    }
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        int ticks = 0;
        while (vm.swinging() && ticks < 1000) {
            sk_bindings::TickWeaponViewmodel(vm, 0);
            ++ticks;
        }
        Check(ticks == 15, "...in 15 ticks, 0.6s -- not the 45 M78 measured against 0x100");
    }

    // ---- 5. The billboard's geometry ----
    //
    // `halfW = ((i16)(sizeX * spriteW) * spriteW) >> 8` -- the sprite's own
    // dimension enters twice, which is why the scalars run inversely to the
    // art. A projectile keeps `FUN_1008b420`'s `+0x12c`/`+0x130` of 8.
    {
        Check(sk_bindings::SpriteHalfExtent(sk_bindings::kProjectileDrawSize, 32) == 32,
              "a 32x32 fireball at size 8 is 32 units half-wide -- 64 across, a quarter tile");
        Check(sk_bindings::SpriteHalfExtent(512, 16) == 512,
              "crypt1's flame (size 512 against a 16px sprite) is 512 -- 1024 units across");
        Check(sk_bindings::kProjectileBlendMode == 0,
              "a spell projectile passes `+0x58 = 0`, so the fireball is an opaque blit");
    }

    // ---- 6. The fireball on screen ----
    //
    // Rendered for real, against a real zone, through the same
    // ZoneRenderer main.cpp uses.
    {
        sk::SpriteArchive sprites;
        bool loaded = sprites.Load(scriptRoot);
        const sk::Sprite* fireball = loaded ? sprites.GetSprite(2) : nullptr;
        Check(fireball != nullptr && fireball->width == 32 && fireball->height == 32,
              "global.spr slot 2 -- blaze's own `+0x134` -- decodes as a 32x32 sprite");

        sk::Zone zone;
        bool zoneLoaded = zone.Load(scriptRoot, "azra");
        Check(zoneLoaded, "azra loads, to render the projectile against");

        if (fireball && zoneLoaded) {
            sk::Camera camera;
            camera.x = static_cast<float>(zone.playerStartX);
            camera.y = static_cast<float>(zone.playerStartY);
            camera.z = static_cast<float>(zone.playerStartZ) + sk::kEyeHeightOffset;
            camera.yaw = 0.0f;
            camera.fovY = sk::kEngineFovY;

            sk::ZoneRenderer renderer;
            sk::Backbuffer bare;
            renderer.Render(bare, zone, camera, {}, nullptr, {});

            // Two tiles straight ahead of the camera (yaw 0 is +x), bottom
            // edge a little under the eye so the whole quad is in frame.
            const int halfExtent =
                sk_bindings::SpriteHalfExtent(sk_bindings::kProjectileDrawSize, fireball->width);
            sk::SpriteBillboard bb;
            bb.x = camera.x + 2.0f * sk::kTileScale;
            bb.y = camera.y;
            bb.z = camera.z - static_cast<float>(halfExtent);
            bb.sprite = fireball;
            bb.halfWidth = halfExtent;
            bb.halfHeight = halfExtent;
            bb.blendMode = sk_bindings::kProjectileBlendMode;
            bb.blendLevel = sk_bindings::kProjectileBlendLevel;

            sk::Backbuffer withShot;
            renderer.Render(withShot, zone, camera, {}, nullptr, {bb});

            int differing = 0;
            int minX = sk::Backbuffer::kWidth, maxX = -1;
            for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
                const uint16_t* a = bare.Row(y);
                const uint16_t* b = withShot.Row(y);
                for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                    if (a[x] == b[x]) continue;
                    ++differing;
                    if (x < minX) minX = x;
                    if (x > maxX) maxX = x;
                }
            }
            std::printf("  %d pixels changed, spanning columns %d..%d\n", differing, minX, maxX);
            // A 32x32 sprite at size 8 is 64 world units across, which two
            // tiles out is a 13x13 quad -- 169 pixels at most, and fewer
            // once the sprite's own transparent corners are taken off. Small
            // is the answer the arithmetic gives (effect_entity.h flags the
            // magnitudes as unverified against a device screenshot); what is
            // checked here is that it draws at all and at the projected
            // size, not that it looks right.
            Check(differing > 40, "a fireball two tiles ahead actually draws");

            // Its width is the projection, not a guess: 2 * halfWidth world
            // units at 2 tiles, focal 104. Allow a pixel of rounding on
            // each edge and whatever the sprite's own transparent border
            // trims off the sides.
            const float expected = 2.0f * static_cast<float>(halfExtent) /
                                   (2.0f * sk::kTileScale) * 104.0f;
            const int drawnWidth = maxX - minX + 1;
            std::printf("  drawn width %d px, projected width %.1f px\n", drawnWidth,
                        static_cast<double>(expected));
            Check(drawnWidth <= static_cast<int>(expected) + 2 &&
                      drawnWidth >= static_cast<int>(expected) / 2,
                  "...at the size the engine's own projection gives it");

            // Behind the camera it must not.
            sk::SpriteBillboard behind = bb;
            behind.x = camera.x - 2.0f * sk::kTileScale;
            sk::Backbuffer backwards;
            renderer.Render(backwards, zone, camera, {}, nullptr, {behind});
            int behindDiffering = 0;
            for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
                const uint16_t* a = bare.Row(y);
                const uint16_t* b = backwards.Row(y);
                for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                    if (a[x] != b[x]) ++behindDiffering;
                }
            }
            Check(behindDiffering == 0, "a projectile behind the camera draws nothing");

            // And a blend level of 0 draws nothing at all -- `FUN_1004f218`
            // returns before its first row when `(v + 0x1f) >> 6 == 0`.
            sk::SpriteBillboard invisible = bb;
            invisible.blendMode = 1;
            invisible.blendLevel = 0;
            sk::Backbuffer faded;
            renderer.Render(faded, zone, camera, {}, nullptr, {invisible});
            int fadedDiffering = 0;
            for (int y = 0; y < sk::Backbuffer::kHeight; ++y) {
                const uint16_t* a = bare.Row(y);
                const uint16_t* b = faded.Row(y);
                for (int x = 0; x < sk::Backbuffer::kWidth; ++x) {
                    if (a[x] != b[x]) ++fadedDiffering;
                }
            }
            Check(fadedDiffering == 0,
                  "blend mode 1 at level 0 draws nothing -- the ramp is a fade, not a scale");
        }
    }

    std::printf("\nm79_spell_viewmodel_smoke: %s (%d failure(s))\n",
                g_failures == 0 ? "OK" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
