// M117 smoke test: the spell cooldown, and the fireball's aim.
//
//   FUN_10047740  the spell constructor: `+0x1d2 = 300`, the refire rate
//   FUN_10046680  the cast gate: `now < player+0xfc4 + spell+0x1d2` refuses,
//                 otherwise stamp +0xfc4 -- before the cast itself runs
//   FUN_1005f0b4  the projectile's spawn, aimed off the caster's heading
//
// Part 1  every spell starts at 300, template-built or path-built, and a
//         script's own SetRefireRate (Sanctuary's 768) still wins
// Part 2  the gate holds a held key to one cast per 300 units
// Part 3  a fireball aimed from any camera yaw flies along the camera's
//         own forward vector, and hits what is in front of it
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/game_constants.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/spell_cast.h"
#include "simkin_bindings/spell_projectile.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"

namespace sk_b = sk_bindings;
using Item = sk_b::ItemExecutable;
using Monster = sk_b::MonsterExecutable;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-92s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

constexpr int kBlaze = 50;
constexpr int kSanctuary = 4028;

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m117_spell_refire_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    entityTypes.Load(root);
    skInterpreter interpreter;
    sk_b::MenuStack stack(root, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);
    sk_b::PlayerExecutable& player = stack.player();

    // Built straight from a script path, the way a new character's starting
    // kit is -- no entities.txt category, so the type is inferred.
    auto loadItem = [&](const std::string& rel) -> std::unique_ptr<Item> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto item = std::make_unique<Item>(skString((root + "/" + rel).c_str()), loadCtxt,
                                               stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            item->method(skString("Init"), args, ret, callCtxt);
            return item;
        } catch (skParseException& e) {
            std::printf("  (parse error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("  (runtime error loading %s: %s)\n", rel.c_str(), e.toString().ptr());
        }
        return nullptr;
    };
    auto loadMonster = [&](const std::string& rel) -> std::unique_ptr<Monster> {
        skExecutableContext loadCtxt(&interpreter);
        try {
            auto m = std::make_unique<Monster>(skString((root + "/" + rel).c_str()), loadCtxt,
                                               &strings, player, stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            m->method(skString("Init"), args, ret, callCtxt);
            return m;
        } catch (skParseException&) {
        } catch (skRuntimeException&) {
        }
        return nullptr;
    };

    // ---------------------------------------------------------------
    std::printf("\n== Part 1: the constructor's refire rate ==\n");
    std::unique_ptr<Item> blaze = stack.level().CreateItem(kBlaze, false);
    {
        Check(blaze && blaze->itemType() == sk_b::kItemTypeSpell &&
                  blaze->refireRate() == sk_b::kSpellDefaultRefireRate,
              "a template-built Blaze has the constructor's 300");
        std::unique_ptr<Item> pathBlaze = loadItem("blaze.s");
        Check(pathBlaze && pathBlaze->itemType() == sk_b::kItemTypeSpell &&
                  pathBlaze->refireRate() == sk_b::kSpellDefaultRefireRate,
              "  and so does one built from its script path (the starting kit)");
        std::unique_ptr<Item> sanctuary = stack.level().CreateItem(kSanctuary, false);
        Check(sanctuary && sanctuary->refireRate() == 768,
              "Sanctuary's own SetRefireRate(768) still overrides it");
        std::unique_ptr<Item> sword = stack.level().CreateItem(510, false);
        Check(sword && sword->refireRate() == 0, "a weapon has no refire rate");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 2: a held key at 25 Hz ==\n");
    if (blaze) {
        // What main.cpp's runSpellCast does each tick the key is held: gate,
        // stamp, cast. Count the passes over three seconds of ticks.
        sk_b::SpellCastCooldown cooldown;
        int now = 1000;  // a stamp of 0 reads as "never cast", as in the engine
        int passes = 0;
        std::vector<int> at;
        for (int tick = 0; tick < 75; ++tick, now += sk_b::kAiFrameDeltaUnits) {
            if (!sk_b::SpellCastAllowed(*blaze, cooldown, now)) continue;
            sk_b::NoteSpellCast(cooldown, now);
            ++passes;
            at.push_back(now);
        }
        Check(passes == 3, "three seconds held casts three times, not seventy-five");
        Check(at.size() >= 2 && at[1] - at[0] >= sk_b::kSpellDefaultRefireRate,
              "  each at least 300 units (~1.17s) after the last");
    }

    // ---------------------------------------------------------------
    std::printf("\n== Part 3: the fireball goes where the camera faces ==\n");
    if (blaze) {
        const float kPi = 3.14159265f;
        const float yaws[] = {0.0f, kPi * 0.5f, kPi, kPi * 1.25f, -kPi * 0.3f};
        bool allAligned = true;
        for (float yaw : yaws) {
            sk_b::SpellProjectile shot = sk_b::SpawnSpellProjectile(
                blaze.get(), &player, 20000, 20000, 0, sk_b::EngineYawFromPortYaw(yaw), 0, 2, 0);
            // The camera's forward, as main.cpp computes it for auto-aim.
            const float fx = std::cos(yaw), fy = std::sin(yaw);
            const float len = std::sqrt(float(shot.vx) * shot.vx + float(shot.vy) * shot.vy);
            const float dot = (shot.vx * fx + shot.vy * fy) / (len > 0 ? len : 1.0f);
            if (dot < 0.999f) allAligned = false;
        }
        Check(allAligned, "at five yaws the velocity lies along (cos yaw, sin yaw)");

        std::unique_ptr<Monster> rat = loadMonster("monsters/Azra_Rat.s");
        Check(rat != nullptr, "a real rat loads");
        if (rat) {
            // A rat four tiles away along a diagonal the camera faces.
            const float yaw = kPi * 0.75f;
            const int px = 20000, py = 20000;
            sk_b::ProjectileTarget target;
            target.actor = rat.get();
            target.script = rat.get();
            target.x = px + static_cast<int>(std::cos(yaw) * 4 * 256);
            target.y = py + static_cast<int>(std::sin(yaw) * 4 * 256);
            target.halfWidth = sk_b::kProjectileRadius;
            target.halfDepth = sk_b::kProjectileRadius;
            std::vector<sk_b::ProjectileTarget> targets{target};

            // Blaze's HitTarget is a DoAttackRoll, which can miss; fire
            // until one lands, as a player would.
            auto never = [](int, int) { return false; };
            bool hitEver = false;
            bool damaged = false;
            for (int attempt = 0; attempt < 40 && !damaged; ++attempt) {
                const int before = rat->actorHealth();
                sk_b::SpellProjectile shot =
                    sk_b::SpawnSpellProjectile(blaze.get(), &player, px, py, 0,
                                               sk_b::EngineYawFromPortYaw(yaw), 0, 2, 0);
                sk_b::ProjectileImpact impact;
                while (shot.alive) impact = sk_b::TickSpellProjectile(shot, 200, 200, never, targets);
                hitEver = hitEver || (impact.hit && impact.target == rat.get());
                damaged = rat->actorHealth() < before;
            }
            Check(hitEver, "a fireball aimed at a rat on a diagonal collides with it");
            Check(damaged, "  and blaze.s's HitTarget does real damage");

            // And one aimed the other way does not.
            sk_b::SpellProjectile away = sk_b::SpawnSpellProjectile(
                blaze.get(), &player, px, py, 0, sk_b::EngineYawFromPortYaw(yaw + kPi), 0, 2, 0);
            sk_b::ProjectileImpact miss;
            while (away.alive) miss = sk_b::TickSpellProjectile(away, 200, 200, never, targets);
            Check(!miss.hit, "one fired the opposite way flies past nothing");
        }
    }

    std::printf("\nm117_spell_refire_smoke: %d/%d checks passed\n", g_checks - g_failures,
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
