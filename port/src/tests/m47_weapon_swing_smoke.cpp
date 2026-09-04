// M47 smoke test: the real weapon-swing trigger.
//
// M25 decompiled the viewmodel's draw function but left this open: "the
// exact native call that actually starts a swing (writes +0x230/+0x234/
// +0x238) wasn't found despite tracing every reachable write site from the
// Weapon class dispatcher." It was recorded as an exhaustive-search gap.
//
// The search was exhaustive in the wrong direction. Grepping the whole
// decompiled corpus for the *write text* (`"0x234) ="`) -- M46's technique
// -- turns it up in one pass, along with the fact that two of the three
// fields were being read as the wrong thing entirely:
//
//   * `FUN_100425bc(player)` -- the player's attack function, the primary
//     trigger. Also carries the player's attack cadence (`+0xf48`), the
//     4-point fatigue cost per swing, and the ranged branch that spawns a
//     projectile.
//   * `FUN_10042394(player, item)` -- "use item at target"; starts the
//     same swing after a cast lands.
//   * `FUN_1001d880` / `FUN_1001d778` -- player vtable +0x190 / +0x194.
//     The second is the **weapon swap**, which is what `+0x230` (the
//     previous weapon's sprite) and `+0x238` (the transition timer)
//     actually are -- not an "alternate swing sprite" and a "post-swing
//     hold".
//   * `FUN_1001f230(player, speed)` -- the walk bob's phase.
//
// The payoff this asserts is the sprite arithmetic. A melee weapon owns 16
// consecutive global.spr slots: the base is its idle pose, and the three
// randomly chosen swing variants (`item+0x17f` = 0 / 5 / 10) are three
// separate five-frame swings after it. For weapons/club.s that is
// 89-93, 94-98 and 99-103 -- checked frame by frame below.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/sprite_archive.h"
#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/weapon_viewmodel.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-76s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    if (!ok) ++g_failures;
}

// Runs one swing to exhaustion and returns the distinct sprite slots it
// drew, in order.
std::vector<int> SwingSequence(sk_bindings::WeaponViewmodel& vm) {
    std::vector<int> slots;
    for (int guard = 0; guard < 1000 && vm.swingAccum != 0; ++guard) {
        sk_bindings::ViewmodelDraw d = sk_bindings::ResolveViewmodelDraw(vm);
        if (d.visible && (slots.empty() || slots.back() != d.spriteSlot)) {
            slots.push_back(d.spriteSlot);
        }
        sk_bindings::TickWeaponViewmodel(vm, 0);
    }
    return slots;
}

std::string Join(const std::vector<int>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) s += ",";
        s += std::to_string(v[i]);
    }
    return s;
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
        std::printf("m47_weapon_swing_smoke: FAILED to load entities.txt\n");
        return 1;
    }

    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto it = std::make_unique<Item>(
                skString((std::string(scriptRoot) + "/" + rel).c_str()), loadCtxt, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            it->method(skString("Init"), args, ret, callCtxt);
            return it;
        } catch (skParseException& e) {
            std::printf("  (parse error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        } catch (skRuntimeException& e) {
            std::printf("  (runtime error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
            return nullptr;
        }
    };

    std::printf("=== M47: the real weapon-swing trigger ===\n\n");

    std::unique_ptr<Item> club = loadItem("weapons/club.s");
    std::unique_ptr<Item> bow = loadItem("weapons/bandit_longbow.s");
    std::unique_ptr<Item> spell = loadItem("spells/blind.s");
    Check(club && bow && spell, "weapons/club.s, weapons/bandit_longbow.s, spells/blind.s load");
    if (!club || !bow || !spell) {
        std::printf("\nm47_weapon_swing_smoke: FAILED\n");
        return 1;
    }

    // ---- 1. The gate ----
    //
    // FUN_100425bc refuses in three cases, in this order: a swap running, a
    // swing running, and an item with no animation frames.
    {
        VM vm;
        Check(!sk_bindings::StartWeaponSwing(vm, nullptr), "no item: no swing");
        Check(!sk_bindings::StartWeaponSwing(vm, spell.get()),
              "a real spell (no SetWeaponSprite) cannot swing");
        Check(sk_bindings::StartWeaponSwing(vm, club.get()), "a real club can");
        Check(!sk_bindings::StartWeaponSwing(vm, club.get()),
              "...and cannot be re-triggered while its own swing is running");
        // The real machine has no interruptible "hold" -- M25's Hold phase
        // was the swap timer misread, so a swing simply runs to completion.
        while (vm.swingAccum != 0) sk_bindings::TickWeaponViewmodel(vm, 0);
        Check(sk_bindings::StartWeaponSwing(vm, club.get()),
              "once the accumulator empties, the next swing starts");
    }
    {
        VM vm;
        sk_bindings::NotifyWeaponChanged(vm, club.get());
        sk_bindings::NotifyWeaponChanged(vm, bow.get());
        Check(vm.swapping(), "changing weapon starts the swap transition (FUN_1001d778)");
        Check(!sk_bindings::StartWeaponSwing(vm, bow.get()),
              "...and no swing may start while a swap is running");
    }

    // ---- 2. The accumulator's real seed ----
    //
    // `(frames + 2) << 8` for a plain melee weapon; `frames << 8` for
    // anything that called SetBow / SetCrossbow / SetThrowingWeapon.
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        Check(vm.swingAccum == ((club->animationFrames() + 2) << 8),
              "club (5 frames, melee): accumulator seeded to (5+2)<<8 = 1792");
    }
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, bow.get());
        Check(vm.swingAccum == (bow->animationFrames() << 8),
              "bandit_longbow (4 frames, SetBow): seeded to 4<<8 = 1024, no +2");
        Check(vm.swingVariant == 0, "...and a ranged weapon never rolls a swing variant");
    }

    // ---- 3. The three melee swing variants ----
    //
    // The variant is rolled inside StartWeaponSwing, so it is forced here
    // to check all three deterministically. Each is exactly five frames,
    // and together they fill the club's 16-slot strip after the idle pose
    // at slot 88 -- which is what the 16-slot spacing between weapon bases
    // (72 / 88 / 104 / 120 / 136) is for.
    const int expected[3][5] = {{89, 90, 91, 92, 93}, {94, 95, 96, 97, 98}, {99, 100, 101, 102, 103}};
    const int variants[3] = {0, 5, 10};
    for (int v = 0; v < 3; ++v) {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        vm.swingVariant = variants[v];
        std::vector<int> seq = SwingSequence(vm);
        // The last entry is the boundary artifact: the accumulator lands on
        // exactly 0x200 on the final tick, which the `< 0x200` test lets
        // through for one frame at one slot past the animation. Reproduced
        // rather than trimmed -- it is what the arithmetic does.
        bool tailOk = seq.size() == 6 && seq[5] == expected[v][4] + 1;
        bool bodyOk = seq.size() >= 5;
        for (int i = 0; bodyOk && i < 5; ++i) bodyOk = seq[static_cast<size_t>(i)] == expected[v][i];
        Check(bodyOk, "club swing variant " + std::to_string(variants[v]) + " draws slots " +
                          std::to_string(expected[v][0]) + ".." +
                          std::to_string(expected[v][4]) + " (got " + Join(seq) + ")");
        Check(tailOk, "...plus the one-frame acc==0x200 boundary artifact at " +
                          std::to_string(expected[v][4] + 1));
    }

    // ---- 4. A ranged weapon starts two frames in ----
    //
    // Dropping the `+2` costs the bow its first two frames: it shows only
    // 178 and 179, the last two slots of its five-slot strip (175..179).
    // Reproduced as found -- the code does not say whether that is a bug or
    // a deliberate "no windup, straight to the release".
    {
        VM vm;
        sk_bindings::StartWeaponSwing(vm, bow.get());
        std::vector<int> seq = SwingSequence(vm);
        Check(seq.size() == 2 && seq[0] == 178 && seq[1] == 179,
              "bandit_longbow draws only 178,179 -- the tail of its strip (got " + Join(seq) + ")");
    }

    // ---- 5. The swap transition ----
    //
    // 0x800 when swapping cleanly out of a weapon that was on screen,
    // 0x400 otherwise. Above 0x400 the *old* weapon is drawn at (0,0);
    // below it the new one, dropped 124px.
    {
        VM vm;
        sk_bindings::NotifyWeaponChanged(vm, club.get());
        Check(vm.swapTimer == sk_bindings::kSwapTimerShort,
              "first weapon (no previous sprite): the short 0x400 transition");
        while (vm.swapTimer != 0) sk_bindings::TickWeaponViewmodel(vm, 0);
        sk_bindings::NotifyWeaponChanged(vm, bow.get());
        Check(vm.swapTimer == sk_bindings::kSwapTimerFull,
              "club -> longbow: the full 0x800 transition");
        Check(vm.prevSprite == 88 && vm.currentSprite == 175,
              "prevSprite/currentSprite hold the club's and the bow's real slots");
        sk_bindings::ViewmodelDraw d = sk_bindings::ResolveViewmodelDraw(vm);
        Check(d.visible && d.spriteSlot == 88 && d.y == 0,
              "early in the swap the OLD weapon is still drawn, at y=0");
        while (vm.swapTimer > sk_bindings::kSwapShowOld) sk_bindings::TickWeaponViewmodel(vm, 0);
        d = sk_bindings::ResolveViewmodelDraw(vm);
        Check(d.visible && d.spriteSlot == 175 && d.y == sk_bindings::kSwapRaiseY,
              "past the halfway mark the NEW weapon is drawn 124px low");
        // Re-equipping the same weapon must not re-trigger anything.
        while (vm.swapTimer != 0) sk_bindings::TickWeaponViewmodel(vm, 0);
        sk_bindings::NotifyWeaponChanged(vm, bow.get());
        Check(!vm.swapping(), "re-equipping the same weapon does not restart the transition");
    }

    // ---- 6. The bob ----
    {
        VM vm;
        sk_bindings::NotifyWeaponChanged(vm, club.get());
        while (vm.swapTimer != 0) sk_bindings::TickWeaponViewmodel(vm, 0);
        // A swing or a swap leaves the phase at 0, and the real wrap
        // (`if (phase > 0) return; phase += 0x1400;`) fires on the very
        // next tick regardless of speed -- so the phase snaps to a full
        // cycle once and then holds while the player is stationary.
        sk_bindings::TickWeaponViewmodel(vm, 0);
        Check(vm.swayPhase == sk_bindings::kSwayCycle,
              "the phase wraps to a full cycle on the first tick after a reset");
        int before = vm.swayPhase;
        sk_bindings::TickWeaponViewmodel(vm, 0);
        sk_bindings::TickWeaponViewmodel(vm, 0);
        Check(vm.swayPhase == before, "standing still then freezes the bob phase");
        int minX = 999, maxX = -999, minY = 999, maxY = -999;
        for (int i = 0; i < 200; ++i) {
            sk_bindings::TickWeaponViewmodel(vm, 40);  // this port's walk speed
            sk_bindings::ViewmodelDraw d = sk_bindings::ResolveViewmodelDraw(vm);
            if (d.x < minX) minX = d.x;
            if (d.x > maxX) maxX = d.x;
            if (d.y < minY) minY = d.y;
            if (d.y > maxY) maxY = d.y;
        }
        Check(minX >= 0 && maxX <= 10 && minY >= 0 && maxY <= 10,
              "walking bobs the viewmodel inside 0..10px on both axes (x " +
                  std::to_string(minX) + ".." + std::to_string(maxX) + ", y " +
                  std::to_string(minY) + ".." + std::to_string(maxY) + ")");
        Check(maxX > 5 && maxY > 5, "...and actually uses most of that range");
        sk_bindings::StartWeaponSwing(vm, club.get());
        Check(vm.swayPhase == 0, "starting a swing resets the bob phase (player+0x244 = 0)");
    }

    // ---- 7. The engine's own sine table ----
    //
    // Four checkpoints read out of the real table at 0x100f4954, which the
    // port regenerates rather than embeds.
    Check(sk_bindings::SwaySine(0) == 0, "sine table: k=0 -> 0");
    Check(sk_bindings::SwaySine(256) == 181, "sine table: k=256 -> 181 (0.7071 * 256)");
    Check(sk_bindings::SwaySine(384) == 237, "sine table: k=384 -> 237");
    Check(sk_bindings::SwaySine(512) == 256, "sine table: k=512 -> 256 (1.0 in 8.8)");
    Check(sk_bindings::SwaySine(1536) == -256, "sine table: k=1536 -> -256");

    // ---- 8. The art is really full-screen ----
    //
    // The real blit puts a swing frame at (0,0) with the sprite's full
    // width and height, and every viewmodel slot in global.spr is a
    // 176x208 frame. This port used to draw them small, in the bottom
    // right corner.
    sk::SpriteArchive sprites;
    if (sprites.Load(scriptRoot)) {
        sprites.LoadCategory(scriptRoot, "azra");
        bool allFullScreen = true;
        for (int slot = 88; slot <= 103; ++slot) {
            const sk::Sprite* s = sprites.GetSprite(slot);
            if (!s || s->width != 176 || s->height != 208) allFullScreen = false;
        }
        Check(allFullScreen, "every slot of the club's strip (88..103) is a full 176x208 frame");
        VM vm;
        sk_bindings::StartWeaponSwing(vm, club.get());
        sk_bindings::ViewmodelDraw d = sk_bindings::ResolveViewmodelDraw(vm);
        Check(d.visible && d.x == 0 && d.y == 0, "a swing frame is drawn flush at (0,0)");
    } else {
        std::printf("global.spr not loadable -- skipping the full-screen art checks\n");
    }

    std::printf("\nm47_weapon_swing_smoke: %s (%d failure(s))\n",
                g_failures == 0 ? "PASSED" : "FAILED", g_failures);
    return g_failures == 0 ? 0 : 1;
}
