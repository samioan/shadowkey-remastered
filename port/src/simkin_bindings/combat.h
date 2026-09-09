#pragma once

// Combat: the shared melee resolution for both attack directions
// (player-on-monster, monster-on-player).
//
// M43 -- this file's long-standing "deliberately from-scratch, no RE
// ground truth exists for the real attack/defense-to-damage formula" note
// is now retired for melee. The real one is recovered, and the reason it
// got looked for is worth recording: M43's SetMob pass turned the shipped
// creature stats into *real* numbers (a level-1-zone Azra_Rat's defense is
// 62, not the 4 its script literally says), and feeding those into an
// invented linear `50 + (attack - defense) * 5` produced nonsense. A
// recovered stat needs its recovered consumer.
//
// The two functions it took are FUN_1004b620 (the to-hit gate) and the
// damage tail of FUN_100835b8 (the creature attack routine).

namespace sk_bindings {

// FUN_1004b620, the melee to-hit gate:
//
//   total = attack + defense;  if (total == 0) total = 1;
//   chance = (total == attack) ? 0x80 : (attack << 16) / (total << 8);
//
// i.e. `attack * 256 / (attack + defense)` in 0..256 -- the same ratio
// model as the magic gate below, and kept in the same integer order so the
// truncation matches step for step.
//
// The zero-defense special case is the interesting one, and it is the
// *opposite* of the magic gate's: where a zero-resistance target is a
// guaranteed magical hit (`chance == 0x100`), a zero-defense target caps
// out at 0x80, half. The division would have produced exactly 0x100 there,
// so this is a deliberate substitution, not a guard against overflow.
int MeleeHitChance(int attackerAttack, int defenderDefense);

// `rand % 0x100 <= chance` -- note the `<=`, and note the roll is over
// [0, 255] against a chance that can reach 256, so a very lopsided matchup
// really is a certainty even though the zero-defense case is not.
//
// The real function follows a successful roll with a second, separate
// dodge/block test through the defender's own stats vtable (+0x14), which
// is not decompiled and is not reproduced here -- so this port's melee
// lands slightly more often than the shipped game's.
bool RollMeleeHit(int attackerAttack, int defenderDefense);

// The whole melee resolution: the gate above, then the damage tail of
// FUN_100835b8:
//
//   span = damageMax - damageMin;  if (span < 1) span = 1;
//   damage = damageMin + rand % span - defenderArmorRating;
//   if (damage > 0) DoDamage(damage);
//
// Two details worth keeping: the spread is **exclusive** at the top (a 3..6
// weapon rolls 3, 4 or 5), and the defender's *full* armour rating is
// subtracted, with a fully-absorbed hit dealing literally nothing rather
// than a courtesy 1. Returns 0 for a miss and for an absorbed hit alike --
// which is what the real code does too, since both skip the DoDamage call.
//
// M73: `halveRoll` is `FUN_100425bc`'s exhaustion penalty -- the player's
// own melee branch follows its damage roll with `if (fatigue < 1) damage
// >>= 1`, on the raw roll and before the defender's mitigation comes off,
// which is exactly where this applies it. Only the player's *melee* path
// passes true: the ranged branch has already returned by then, and a
// creature has no fatigue pool to empty. See vitals.h.
int RollDamage(int attackerAttack, int defenderDefense, int defenderArmor, int dmgMin, int dmgMax,
                bool halveRoll = false);

// ---- M85: the player's melee reach, and the height band around it ----
//
// `FUN_100425bc`'s melee arm, after it has picked a target out of the
// object-ID buffer (main.cpp's pickMeleeTarget has the probe layout):
//
//     if (|(s16)target+0xa4 - (s16)player+0xa4| > 0x180) target = 0;
//     if (target && target->vtable[0xe4]() &&
//         target->+0x1e2 == 0 &&
//         target->vtable[0x5c](player) < 0x76c) { ...swing connects... }
//
// `vtable+0x5c` is entity-to-entity distance in raw world units -- the
// same slot `FUN_1004720c`'s area spell compares against 12000 -- so the
// reach is a flat **1900 units, about 7.4 tiles**, and it is the same for
// every melee weapon in the game. A weapon's own `SetRange()` never
// enters this: 384 vs 16384 only decides `item+0x1a7`, the ranged flag,
// which picks between this arm and the arrow arm one branch earlier.
// Reading 384 as the melee reach (which this port did) makes a sword
// reach a fifth as far as it should.
constexpr int kMeleeReachUnits = 0x76c;

// The vertical half-band around the player a melee target has to be
// inside. Distinct from the creature side's 0x200
// (monster_ai.h's kAttackVerticalLimitUnits) -- the player's is 0x180,
// and the two are separate constants in separate functions.
constexpr int kPlayerMeleeVerticalLimit = 0x180;

// ---- M87: the player's melee damage, which is not the creature's ----
//
// RollDamage above is the tail of `FUN_100835b8`, the **creature** attack.
// The player's melee arm lives in `FUN_100425bc` and rolls its damage a
// different way, which this port had never noticed:
//
//     damage = FUN_100730c8(rng, weapon->+0x1cc, weapon->+0x1ce);
//     if (player->+0x3d8 < 1) damage >>= 1;                  // exhausted
//     if (target->+0x2d0 == 1 &&                             // a spider
//         FUN_1002eb48(weapon, "spiders", &lo, &hi))
//         damage += lo + rand % (hi - lo);
//     ...
//     if (weapon->+0xc8 == 0x23d) {                          // Magicka Edge
//         roll = FUN_100730c8(rng, 0, 100);
//         if (stats->+0x2e > 10 && roll < 0x15) {
//             damage += 10;
//             player->+0x3da -= 10;                          // 10 magicka
//         }
//     }
//     DoDamage((s16)(damage - target->armourRating()));
//
// and `FUN_100730c8(rng, min, max)` is `min + rand % (max - min + 1)` --
// **inclusive** at the top, where the creature's own `rand % (max - min)`
// is exclusive. So a 4..12 axe rolls 4..12 in the player's hand and 4..11
// in a bandit's, and the port was using the creature's rule for both:
// every player swing in the game was missing its top value and averaging
// half a point low. On a Magicka Edge Axe (6..64) that is four points of
// mean damage, and its maximum was simply unreachable.
//
// The two bonus arms are both real, both hardcoded, and both were absent:
//
//   * **The Spider Impaler.** `weapons/spider_impaler.s` is the corpus's
//     only `AddDamageBonus` call site (`("spiders", 4, 22)`) and
//     `FUN_100425bc` is its only reader, keyed on the literal string and
//     on `SetSpider`'s own creature-kind field. Against a spider it
//     roughly triples the weapon.
//   * **The Magicka Edge Axe**, entities.txt typeId 573
//     (`weapons\Magicka_edge_Axe.s`), tested by template id and nothing
//     else: a 21% chance of ten extra damage, paid for with ten magicka,
//     and only while the player has more than ten to spend. The magicka
//     comes off `player+0x3ac+0x2e`, the same field the HUD's middle bar
//     reads.
//
// Returned together because the caller has to pay the magicka: `damage` is
// what to hand ApplyDamage, `magickaSpent` what to take off the player.
struct PlayerMeleeResult {
    int damage = 0;
    int magickaSpent = 0;
};

// `targetArmor` comes off last and can absorb the whole blow, exactly as
// in RollDamage. `spiderBonus`/`spiderBonusMax` are the weapon's own
// AddDamageBonus("spiders") pair, ignored unless `targetIsSpider`;
// `weaponIsMagickaEdge` is `weapon->templateId() == kTemplateMagickaEdge`
// and `magicka` the player's current pool.
PlayerMeleeResult RollPlayerMeleeDamage(int attackerAttack, int defenderDefense, int defenderArmor,
                                         int dmgMin, int dmgMax, bool halveRoll,
                                         bool targetIsSpider, int spiderBonusMin,
                                         int spiderBonusMax, bool weaponIsMagickaEdge, int magicka);

// entities.txt row `573 30 4 weapons\Magicka_edge_Axe.s` -- the one
// template id `FUN_100425bc` names outright.
constexpr int kTemplateMagickaEdgeAxe = 0x23d;
constexpr int kMagickaEdgeProcChance = 0x15;   // rand(0,100) < 21
constexpr int kMagickaEdgeProcDamage = 10;
constexpr int kMagickaEdgeProcCost = 10;       // magicka, and the floor to spend it

// M20 (ranged weapons): true if (targetX,targetY) is within `range` world
// units of (attackerX,attackerY) and inside a 60-degree forward-facing
// cone given the attacker's yaw (radians) -- the same nearest-in-cone
// shape main.cpp's tryAttack/findNearby* lambdas already use inline,
// factored out here so the real per-weapon range values
// (ItemExecutable::range(), e.g. a real bow script's SetRange(16384))
// are verifiable without needing the full windowed game loop. No line-
// of-sight/wall check -- same "no RE ground truth, from-scratch and
// deliberately simple" footing as RollDamage() above.
bool InAttackRange(float attackerX, float attackerY, float attackerYaw, float targetX,
                    float targetY, float range);

// M30: the same nearest-in-cone rule for the **Use** action (doors, NPCs,
// world items and containers), with the cone width made explicit --
// `minFacing` is the minimum cosine of the angle between the player's
// facing and the direction to the target, so 0.5 is a 60-degree cone and
// 0.30 about 72.
//
// Factored out of main.cpp's three near-identical findNearby* lambdas so
// the numbers are verifiable against real placement data without the
// windowed game loop. The reach this is called with (384 raw world units)
// is the game's own melee reach -- every real melee weapon in the corpus
// calls SetRange(384) -- rather than an invented value; the previous 140
// was barely half a tile, which is why interact prompts almost never
// appeared.
bool InInteractRange(float playerX, float playerY, float playerYaw, float targetX, float targetY,
                      float range, float minFacing);

// M22 (spellcasting): a spell's damage roll -- `rating` is the casting
// item's own real SetRating() value (e.g. blaze.s's SetRating(2)),
// `targetMagicResistance` the real monster's SetMagicResistance() (M12).
// Deliberately from-scratch: unlike RollDamage() above, this covers only
// the spells that are *not* in the real dispatcher's typeId table, where by
// definition there is no real formula to recover.
//
// M33/M34 update: the *effect* half is no longer unmodelled -- the real
// selector (FUN_100458e4) is decompiled and nine of its branches are
// implemented (ItemExecutable::statusEffect()), several with their exact
// shipped constants. Two of those branches, Absorb and IgniteFoe, carry
// their own real damage formula and so bypass this function entirely,
// using only the resistance step below.
int RollSpellDamage(int rating, int targetMagicResistance);

// M34: the resistance step on its own, factored out of RollSpellDamage()
// (which is now exactly `SpellDamageAfterResistance(rating * 3, ...)`, an
// identity refactor -- its behaviour is unchanged).
//
// M37 retired this from the status-effect dispatcher: the real engine does
// not subtract resistance from spell damage at all, it resolves resistance
// as a *hit chance* gate in front of the whole effect (RollSpellHit()
// below). Kept only for RollSpellDamage()'s own from-scratch path, which
// covers spells that are not in the real dispatcher's typeId table.
int SpellDamageAfterResistance(int rawDamage, int targetMagicResistance);

// ---------------------------------------------------------------------
// M37: the real magic to-hit model. Unlike everything above, none of this
// is from-scratch -- all four functions are transcriptions of decompiled
// code, and the two stat formulas are each confirmed twice over (once in
// the standalone helper the dispatcher calls, once in the Character-stats
// native binding a script can call directly, which compute the same thing
// inline).
//
// The stats block's own layout was recovered the same pass, from
// FUN_1004ad40's index switch cross-checked against FUN_10048244's
// getters: `+0x04` spellcast, `+0x06` magic resistance, `+0x1a` willpower,
// `+0x34` character level.

// `GetSpellToHit` (Character-stats index 3) / FUN_1004bc60 -- how hard
// this caster's magic lands: `spellcast + 2 * willpower`.
//
// Both real functions add a further enchantment term for an equipped item
// whose enchantment type is 4 (to-hit) or 7 (resistance, in the function
// below); this port has no enchantment objects to read one from, and the
// omission is a missing bonus rather than a wrong formula.
int SpellToHit(int spellcast, int willpower);

// `GetSpellResistance` (Character-stats index 4) / FUN_1004bbd0 -- how
// hard it is to land magic on this target: `magicResistance +
// willpower / 5`. The /5 is a magic-multiply in the binary (0x66666667
// with a 33-bit shift), not a written division.
int SpellResistance(int magicResistance, int willpower);

// The gate itself, from FUN_100458e4:
//
//   chance = (power << 16) / ((power + resistance) * 0x100)
//
// i.e. `power * 256 / (power + resistance)`, in 0..0x100 -- kept in the
// real integer order so the truncation matches step for step. A caster
// with no power at all (`power <= 0`) gets 0 and can never land anything.
int SpellHitChance(int casterPower, int targetResistance);

// `chance == 0x100 || rand(0, 0x100) < chance`. The `== 0x100` special
// case is the engine's own and is load-bearing: a zero-resistance target
// yields exactly 0x100, which a `< chance` roll over [0, 0x100] would
// still miss 1 time in 257.
//
// This gates the **entire** effect, status included -- the real dispatcher
// does all its damage and status work inside this branch, so a resisted
// Poison applies no poison at all rather than a weakened one.
bool RollSpellHit(int casterPower, int targetResistance);

}  // namespace sk_bindings
