#pragma once

#include <map>
#include <string>

// Combat vertical-slice: native binding for a real monster .s script
// object (monsters/Azra_Rat.s to start -- see docs/PORT_ROADMAP.md's
// combat-slice entry). Same shape as ItemExecutable (a real script's
// Init() actually runs, backed by a real TreeNode), just for the
// SimKin "Monster" class's own native surface
// (shadowkey/simkin_native_bindings.json trie 0x14da4) instead of
// Item/Weapon/Armor's -- this port doesn't reproduce the real trie-class
// split (Monster/Character-stats/generic-base all contribute methods a
// monster script calls), one object answers whatever subset
// monsters/azra_rat.s's Init/OnKilled actually use, matching
// ItemExecutable's own precedent.
//
// GetPlayer() is implemented here (not soft-failed) because
// azra_rat.s's OnKilled calls it directly -- returns the shared
// PlayerExecutable passed into the constructor, same pattern
// MenuExecutable::method()'s own GetPlayer handler already uses.
// QuestSolved/AddMonsterKilled/MonstersKilled/SetQuestSolved are real
// state on PlayerExecutable as of M17 (its own class comment) -- see
// monster_executable.cpp's OnKilled comment for what that does and
// doesn't unlock.
//
// M16: generalized past the single-hardcoded-typeId (202/azra_rat) M12
// slice -- this same class also answers for the real corpus's non-
// hostile "monster"-category NPCs (docs/PORT_ROADMAP.md's M16 entry):
// entities.txt's category 2 covers both literal monsters and named
// quest NPCs (Tanyin Aldwyr, Acolyte Menlin, ...) alike, distinguished
// only by script content -- an NPC's Init() calls SetAggressive(false)
// + SetUsable(true)/SetUseText(...) + (usually) SetInvulnerable(true),
// and its OnUse() calls OpenMenu(...) to start a real dialogue tree
// (an ordinary MenuExecutable-driven branching conversation) instead of
// participating in the AI/melee loop at all. `m_Stack` (new) is needed
// for that OpenMenu() call -- same MenuStack reference MenuExecutable's
// own OpenMenu handler already routes through.

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "simkin_bindings/actor_stats.h"
#include "simkin_bindings/spell_actor.h"
#include "simkin_bindings/spell_cast.h"
#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

// M35 -- CORRECTED. This was 40, on the reasoning that the engine's
// per-frame delta (FUN_1001afa4) was never recovered so the port might as
// well count its own 40ms tick period. That conflated milliseconds with
// the engine's own unit, and ran every AI timer in this file **3.9x too
// fast** -- the visible symptom being a creature's attack noise firing
// about four times a second, i.e. continuously.
//
// The unit is not a free choice: it is fixed by the durations themselves.
// Every timed value in the engine is stored as `seconds * 0x100` --
// FUN_1004aa28's `expiry = now + duration * 0x100`, Disease's literal
// 0x1e (30 seconds), Paralyze's `(magnitude + 4) * 0x100`, IgniteFoe's
// `magnitude << 9` (magnitude*2 seconds) -- and those expiries are
// compared against a clock at `+0x460` which FUN_1002f694 advances by
// exactly this per-frame delta each frame (`clock += FUN_1001afa4(...)`,
// zeroed in the constructor, FUN_1002fca4). So the clock counts in
// 1/256ths of a second, and the per-frame delta is
//
//     0x100 * frameSeconds  ==  256 / 25  ==  10.24
//
// at the real 25Hz tick (docs/RENDER_LOOP.md). Truncated to 10 below,
// which makes a nominal second take 25.6 ticks instead of 25 -- 2.4%
// slow, against the 290% fast it was. With this, the 0x100 attack-cadence
// threshold is ~26 ticks, i.e. **one swing a second**, and the `rand &
// 0x1f` reset is the small jitter that keeps a pack out of lockstep --
// which is what those two constants are proportioned for.
constexpr int kAiFrameDeltaUnits = 0x100 / 25;

// The decompiled attack-cadence threshold (monster+0x2c4 must exceed this
// before the creature may act).
constexpr int kAiAttackCadenceThreshold = 0x100;

class MenuStack;
class PlayerExecutable;
class ItemExecutable;

class MonsterExecutable : public skScriptedExecutable, public SpellActor {
public:
    // `strings` may be null, same fallback convention as ItemExecutable.
    MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                       const sk::StringTable* strings, PlayerExecutable& player, MenuStack& stack);
    // Out of line so ItemExecutable (m_OwnedSpells, M43) only has to be a
    // complete type in the .cpp -- same reason LevelExecutable's own
    // destructor is declared here rather than defaulted inline.
    ~MonsterExecutable() override;

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

    // M38: keep an object assigned to a *pre-declared* script field --
    // see native_binding_common.h's StoreScriptObjectField() for the
    // vendored-Simkin behaviour this works around and the real script
    // (lothcav.s) that exposed it.
    bool setValue(const skString& fieldName, const skString& attribute,
                  const skRValue& value) override;
    bool getValue(const skString& fieldName, const skString& attribute,
                  skRValue& value) override;


    // M21: guaranteed-non-blank strValue() -- see ItemExecutable::
    // strValue()'s comment for the real bug this avoids (a found object's
    // default-empty strValue() could otherwise compare equal to the
    // blank `null` global via skRValue::operator=='s T_Object-vs-T_String
    // branch) -- e.g. `Trthgar = Level.GetEntity("trthgar")` if a script
    // ever null-checked it (azra_rat.s's own OnKilled() doesn't, but a
    // future one might).
    skString strValue() const override { return skString("Monster"); }

    std::string name() const;

    // Real per-instance combat stats, read directly off azra_rat.s's own
    // Init() call (see class comment) -- host-side accessors, not script
    // calls, so main.cpp's combat loop doesn't need to go through the
    // interpreter to read a number.
    // M33: the *effective* values, with any active timed status modifier
    // folded in (Drain and Blind subtract from attack, Blind and Disease
    // from defense, HarmArmor from armor). Clamped at 0 -- the real stat
    // setters are signed shorts, but a negative attack has no meaning in
    // this port's damage roll.
    int attack() const { return (std::max)(0, m_Attack + statModifier(kStatAttack)); }
    int defense() const { return (std::max)(0, m_Defense + statModifier(kStatDefense)); }
    // M49: SpellActor's ranged-combat ratings -- see spell_actor.h. A
    // creature's arrow and the player's are resolved by the same code.
    int actorAttackRating() const override { return attack(); }
    int actorDefenseRating() const override { return defense(); }
    // The unmodified script values, for tests and UI that want the base.
    int baseAttack() const { return m_Attack; }
    int baseDefense() const { return m_Defense; }
    int damageMin() const { return m_DamageMin; }
    int damageMax() const { return m_DamageMax; }
    int armorValue() const { return (std::max)(0, m_ArmorValue + statModifier(kStatArmor)); }
    int baseArmorValue() const { return m_ArmorValue; }
    // M31 (decompiled): SetChaseRadius/SetAttackRange do NOT store a
    // linear radius -- the AI compares them against
    // `FUN_100683d4(self, target)`, which is
    //     (dx*dx >> 8) + (dy*dy >> 8)   ==   (dx^2 + dy^2) / 256
    // i.e. a **scaled squared** distance. So the real distance a script
    // value means is `sqrt(value * 256)` == `16 * sqrt(value)` raw world
    // units. That single fact is what made enemy aggro look unlimited:
    // the corpus's dominant SetChaseRadius(18000) is **8.4 tiles**, not
    // the 70 tiles the port got by treating it as a plain radius.
    //
    // Verified from the dispatcher (0x10084924): case 0x26 writes
    // `monster+0x2b8` and case 0x21 reads it back -- a Set/Get pair on one
    // field, which is what pins the switch cases as running 3 below the
    // enumerated binding-table indices (0x26+3 = 0x29 = SetChaseRadius,
    // 0x21+3 = 0x24 = GetChaseRadius). Consumers are in the AI tick
    // (0x10082224): `+0x2b8` is the give-up distance (`if (chase < dist)`
    // -> drop target, back to the idle package) and `+0x2dc` the
    // stand-off (`if (dist < attackRange)` -> zero velocity and swing).
    //
    // Both accessors return **raw world units**, already converted.
    float chaseRadius() const { return ScaledSquaredToUnits(m_ChaseRadius); }

    // The distance at which the creature stops advancing and attacks --
    // real field `monster+0x2dc`, written by SetAttackRange. Default
    // 0x6a4, i.e. 660 world units (~0.8x a person's height, so roughly
    // arm's length -- most monster scripts, arat.s included, never set it
    // and use exactly this).
    float attackRange() const { return ScaledSquaredToUnits(m_AttackRange); }

    // M49 (`monster+0x2d8`, SetProjectile): does this creature shoot? The
    // real attack routine spawns an arrow when this is set and **skips its
    // melee resolution entirely** -- the whole melee tail sits inside
    // `if (+0x2d8 == -1)` -- so an archer never also punches. Twelve shipped
    // scripts are archers; see arrow_projectile.h for why the value itself
    // is dead and only its presence matters.
    bool shootsProjectile() const { return m_ProjectileArt != -1; }
    int projectileArt() const { return m_ProjectileArt; }
    bool aggressive() const { return m_Aggressive; }
    // M22: real SetMagicResistance() -- stored since M12, never read back
    // until now (spellcasting's own damage formula, ItemExecutable::
    // DoAttackRoll()).
    int magicResistance() const { return m_MagicResistance; }

    // M37: the target side of the real magic to-hit model (combat.h) --
    // `magicResistance + willpower / 5`, the same formula as the player's,
    // because both read the same stats-block layout. Creature scripts
    // never call SetWillpower (the whole corpus has exactly two
    // SetWillpower call sites, both on the player), so in practice a
    // creature resists with its SetMagicResistance() alone -- which is
    // what makes an unresisting creature's chance exactly 0x100 and
    // exercises the gate's `== 0x100` special case constantly.
    int spellResistance() const;
    // The caster side, for the real case of a *monster* casting: the
    // corpus has creature scripts that cast these same spells
    // (crypt2/pergan_asuul_crypt2.s's `AddSpell(Level.CreateEntity(4024),
    // 35)`), so the caster is not always the player.
    int spellToHit() const;
    int willpower() const { return m_Will; }
    int spellcast() const { return m_Spellcast; }

    // M37: `monster+0x2d0`, a single creature-kind field the real
    // dispatcher writes 1 for SetSpider and 2 for SetUndead, and whose
    // IsUndead() binding is literally `field == 2`. Both setters exist in
    // the shipped corpus (15 SetUndead(true) call sites) and both used to
    // soft-fail here. Needed by the DeadToDust spell, whose whole branch
    // is skipped unless the target is undead.
    enum CreatureKind { kCreatureNormal = 0, kCreatureSpider = 1, kCreatureUndead = 2 };
    bool undead() const { return m_CreatureKind == kCreatureUndead; }
    bool spider() const { return m_CreatureKind == kCreatureSpider; }

    // M28: the real animation clip numbers this creature's script set --
    // indices into its model resource's own clip table (world/
    // model_archive.h's AnimationClip). -1 means the script never set one,
    // in which case the caller should leave the pose alone.
    int idleAnimation() const { return m_IdleAnim; }
    int walkAnimation() const { return m_WalkAnim; }
    int swingAnimation() const { return m_SwingAnim; }
    int deathAnimation() const { return m_DeathAnim; }
    // The starting pose a real Init()'s PlayAnimation() asked for.
    int currentAnimation() const { return m_CurrentAnim; }

    // Real per-instance appearance (see the SetSkin/SetScale handlers).
    int level() const { return m_Level; }

    // M39: see the SetMob handler -- true once the script has called it,
    // which is what opts this creature into the zone's SetZone() XP
    // budget.
    bool countsForZoneExperience() const { return m_CountsForZoneExperience; }
    void SetExpWorth(int value) { m_ExpWorth = value; }
    int expWorth() const { return m_ExpWorth; }

    // M39: a script-driven teleport (SummonMe()'s whole implementation --
    // see the SetPosition handler). Host-side: main.cpp drains this once
    // per tick and moves the live MonsterInstance, the same
    // defer-to-a-safe-point shape PickupItem()/markedForRemoval() already
    // use. Returns false when nothing has changed.
    bool TakePendingPosition(float& x, float& y, float& z) {
        if (!m_PositionDirty) return false;
        m_PositionDirty = false;
        x = static_cast<float>(m_PositionX);
        y = static_cast<float>(m_PositionY);
        z = static_cast<float>(m_PositionZ);
        return true;
    }
    int skin() const { return m_Skin; }
    // SetScale()'s 8.8 fixed-point value as a plain multiplier (256 -> 1).
    float scale() const { return m_Scale > 0 ? static_cast<float>(m_Scale) / 256.0f : 1.0f; }

    // See chaseRadius(): the real comparison value is (d^2)/256, so the
    // distance it stands for is 16*sqrt(value).
    static float ScaledSquaredToUnits(int scaledSquared) {
        if (scaledSquared <= 0) return 0.0f;
        return 16.0f * std::sqrt(static_cast<float>(scaledSquared));
    }

    // ---- M33/M34: the real timed status effects ----
    //
    // M43 moved the implementation to ActorStats (actor_stats.h), which is
    // where the real engine keeps it: every one of these primitives takes
    // the *stats block*, an object a monster and the player each own a copy
    // of at their own fixed offset. These stay as forwarders so nothing
    // that already read them had to change, and so the enum names below
    // keep working unqualified.
    using StatIndex = ActorStats::StatIndex;
    using EffectFlag = ActorStats::EffectFlag;
    using PeriodicKind = ActorStats::PeriodicKind;
    static constexpr int kStatAttack = ActorStats::kStatAttack;
    static constexpr int kStatDefense = ActorStats::kStatDefense;
    static constexpr int kStatArmor = ActorStats::kStatArmor;
    static constexpr int kEffectFlagBlind = ActorStats::kEffectFlagBlind;
    static constexpr int kEffectFlagPoison = ActorStats::kEffectFlagPoison;
    static constexpr int kPeriodicBurn = ActorStats::kPeriodicBurn;

    void ApplyStatModifier(int statIndex, int delta, int durationSeconds) {
        m_Stats.ApplyStatModifier(statIndex, delta, durationSeconds);
    }
    void ApplyEffectFlag(int flagBit, int dotKind, int durationSeconds) {
        m_Stats.ApplyEffectFlag(flagBit, dotKind, durationSeconds);
    }
    void ApplyBurn(int damagePerTick, int durationSeconds) {
        m_Stats.ApplyBurn(damagePerTick, durationSeconds);
    }
    bool burning() const { return m_Stats.burning(); }
    bool blinded() const { return m_Stats.blinded(); }
    bool poisoned() const { return m_Stats.poisoned(); }
    int statModifier(int statIndex) const { return m_Stats.statModifier(statIndex); }

    // ---- M32: the real AI package state machine (monster+0x2a8) ----
    //
    // Decompiled from the AI tick (FUN_10082224), the dispatcher
    // (FUN_10084924) and the timed-package helper (FUN_10086b98). See
    // docs/WORLD_MODEL.md's "The monster AI" section.
    enum AiPackage {
        kAiAsleep = -1,      // the actor constructor's own initial value; AiSleep()
        kAiIdle = 2,         // look for a target -- the post-spawn default, and AiDetect()
        kAiPursue = 3,       // chase/attack monster+0x20c; set by the tick on acquiring a target
        kAiFlee = 4,         // run away for a limited time -- the Fear spell, see below
        kAiSpellAssist = 6,  // AiSpellAssistTarget(); **no reader anywhere in the binary**
    };
    int aiPackage() const { return m_AiPackage; }
    void SetAiPackage(int package) { m_AiPackage = package; }

    // FUN_10086b98(actor, package, duration): sets the package, arms the
    // countdown at monster+0x300 (`duration << 8`), and -- for the flee
    // package specifically -- drops the current target and records
    // "return to idle" in monster+0x2fc. When the countdown expires the
    // tick restores that saved package.
    void SetAiPackageTimed(int package, int durationUnits);

    // Real SetParalyzed (dispatcher case 0xe -> monster+0x294): a general
    // action lockout. The attack function's very first test is
    // `monster+0x294 < 1`, and the tick only steers toward a target while
    // it is exactly 0, so a paralysed creature neither swings nor turns.
    void SetParalyzed(int durationUnits) { m_Stats.SetParalyzed(durationUnits); }
    bool paralyzed() const { return m_Stats.paralyzed(); }

    // Per-tick countdowns for both timers above. `deltaUnits` is the
    // engine's own per-frame delta (the value FUN_1001afa4 returns) --
    // see kAiFrameDeltaUnits.
    void TickAi(int deltaUnits);

    // The real attack cadence (monster+0x2c4). The tick adds the frame
    // delta every frame, only lets the creature act once the accumulator
    // passes 0x100, and then resets it to `rand & 0x1f` -- a small random
    // jitter so a pack doesn't swing in lockstep. Returns true on the
    // frames the creature is allowed to attack, consuming the accumulator.
    //
    // This replaces the port's own invented fixed cooldown.
    bool ConsumeAttackCadence(int deltaUnits);

    int currentHealth() const { return m_CurrentHealth; }
    int maxHealth() const { return m_MaxHealth; }
    bool alive() const { return m_Alive; }

    // M23: a real, silent world-removal distinct from combat death (no
    // OnKilled()/loot spawn) -- zone-root scripts call
    // `Entity.DestroyObjectMirror(Entity)` on entities like azra.s's own
    // "m1".."m20" (real, named .ent placements this session confirmed
    // real -- docs/PORT_ROADMAP.md's M23 entry) once a real save flag
    // (`saved_EndGame`) says they're no longer relevant. Checked
    // alongside `alive()` everywhere a destroyed entity shouldn't act,
    // render, or be targetable -- kept as its own flag rather than
    // folded into `alive()` since the two have genuinely different real
    // triggers and consequences (this one never runs `InvokeOnKilled()`
    // or spawns loot).
    bool destroyed() const { return m_Destroyed; }

    // M16: NPC-mode fields (see class comment) -- usable()/useTextId()
    // mirror door_executable.h's own accessors of the same name (same
    // native class, same SetUsable/SetUseText calls), invulnerable()
    // gates ApplyDamage() below and main.cpp's melee target selection so
    // an essential quest NPC can't accidentally be killed.
    bool usable() const { return m_Usable; }
    int useTextId() const { return m_UseTextId; }
    bool invulnerable() const { return m_Invulnerable; }

    // M21: azra_rat.s's own `SetLoot(300, "Loot_ratseye", 1, 8)` -- the
    // 2nd argument (only) is real, loadable data: lowercased, it's the
    // exact real filename of a loot-bag script (`loot_ratseye.s`, this
    // port's filesystem being case-insensitive, same convention every
    // other path lookup here already relies on) -- see docs/
    // PORT_ROADMAP.md's M21 entry for the full real-corpus decode. The
    // first argument (always literally 300, entities.txt's generic "!
    // bag_loot" container typeId) and the trailing min/max (plausibly a
    // drop-chance roll, not confirmed by any further evidence) aren't
    // stored -- nothing in this port's model reads them back.
    const std::string& lootTag() const { return m_LootTag; }

    // Runs the real script's OnUse() handler -- same host-triggered-call
    // pattern InvokeOnKilled() already establishes. For an NPC this is
    // what actually starts its real dialogue tree (OnUse() calls
    // OpenMenu(...) -- see the class comment).
    void InvokeOnUse();

    // M28: azra_rat.s's own SetAttackNoise(15)/SetDeathNoise(16)/
    // SetIsHitNoise(17) -- the monster's own real per-instance combat
    // sound-effect ids (same <zone>_sounds.txt manifest slot convention
    // as PlaySound(), see method()'s comment), previously stored nowhere
    // and soft-failed. ApplyDamage() below plays the hit/death noise
    // itself (the one choke point every damage source -- melee, spell
    // HitTarget -- already routes through), so only the attack noise
    // needs a host-triggered call, played from main.cpp's monster-attack
    // block at the same point it already rolls damage against the player.
    void PlayAttackNoise();

    // Clamps m_CurrentHealth at 0 and flips alive() false there -- does
    // NOT itself invoke OnKilled(), matching ItemExecutable::
    // MarkForRemoval()'s split between "mutate state now" and "let the
    // host decide when to react to it" (main.cpp calls InvokeOnKilled()
    // explicitly once it observes alive() go false).
    void ApplyDamage(int amount);

    // ---- M43: SpellActor (spell_actor.h) ----
    //
    // A creature is the `vtable+0xe4` side of FUN_1002fd30's two-way
    // resolution, and its stats block is the one at actor+0x224.
    bool isPlayerActor() const override { return false; }
    bool isMonsterActor() const override { return true; }
    ActorStats& actorStats() override { return m_Stats; }
    int actorLevel() const override { return m_Level; }
    int actorHealth() const override { return m_CurrentHealth; }
    void SetActorHealth(int value) override;
    void ApplyActorDamage(int amount) override { ApplyDamage(amount); }
    bool actorAlive() const override { return m_Alive && !m_Destroyed; }
    bool actorUndead() const override { return undead(); }
    void SetActorAiPackageTimed(int package, int durationUnits) override {
        SetAiPackageTimed(package, durationUnits);
    }
    // M48: a creature owns the same two pools the player does, because it
    // owns the same stats block -- FUN_10046764 deducts a creature's
    // magicka exactly as it deducts the player's. It just never *gates* on
    // it, which is what lets the 32 shipped caster scripts work while none
    // of them sets a pool at all: every creature starts (and stays) at 0.
    int actorMagicka() const override { return m_Magicka; }
    void SetActorMagicka(int value) override { m_Magicka = value < 0 ? 0 : value; }
    int actorFatigue() const override { return m_Fatigue; }
    void SetActorFatigue(int value) override { m_Fatigue = value < 0 ? 0 : value; }

    // ---- M43: the real creature spell table (monster+0x30c..+0x32c) ----
    //
    // The Monster class's AddSpell binding (dispatcher 0x10084924 case 8,
    // and the identical standalone FUN_10086ab0) fills a fixed **four-slot**
    // table of `{ u8 chance; Spell* spell; }` 8-byte records, taking the
    // first slot that is either empty or already holds the same spell
    // typeId -- so a fifth distinct spell is dropped with a debug log, and
    // re-adding one replaces it rather than duplicating.
    //
    // `chance` is a **cumulative** threshold over a rand(0, 100), which is
    // what the shipped scripts' own comments say: tunnel_wight.s adds three
    // spells at 30 / 70 / 100 and annotates them "30%" / "40% of the time"
    // / "30%". The one-argument form `AddSpell(Item)` stores 100, i.e.
    // always.
    static constexpr int kSpellSlots = 4;
    // `0xfaa`. AddSpell caches the slot of a Blind spell specially, in
    // `monster+0x32c`, so the AI can avoid re-casting it -- see
    // ChooseSpell().
    static constexpr int kBlindSpellTypeId = 4010;
    // The sound the melee/spell branch plays on a cast, when the script set
    // SetPlaySpellCasting(true): `FUN_1001b198(engine, 6, x, y, ...)`.
    static constexpr int kSpellCastSoundId = 6;
    struct SpellSlot {
        int chance = 0;                        // +0x30c + 8*i
        ItemExecutable* spell = nullptr;       // +0x310 + 8*i
    };
    const SpellSlot& spellSlot(int index) const { return m_SpellSlots[index]; }
    // `monster+0x310` -- the melee/spell branch's own test is literally
    // "is slot 0 occupied", not "do I have any spell".
    bool hasSpells() const { return m_SpellSlots[0].spell != nullptr; }

    // SetMeleeRoll -> `monster+0x304`, and the branch that reads it
    // (FUN_100835b8):
    //
    //   if (slot0 == 0 || (meleeRoll != 0 && meleeRoll <= rand(0,100)))
    //        ... melee ...
    //   else ... cast ...
    //
    // Note the direction, which the name does not suggest: a creature
    // melees only when the roll *reaches* the value, so a **higher**
    // SetMeleeRoll means **less** melee, and the default of 0 means never
    // melee at all. That default is not a quirk -- it is exactly what the
    // shipped mage scripts rely on: bandit_mage.s, highwaymage.s and
    // yelnicin.s call AddSpell but never SetMeleeRoll, and so cast every
    // single attack.
    int meleeRoll() const { return m_MeleeRoll; }
    bool RollForMelee() const;

    // FUN_1008457c: choose a spell for this attack. Returns null when the
    // roll lands past the last filled slot (possible when the thresholds do
    // not run up to 100) or when the chosen slot is empty.
    //
    // `targetBlinded` drives the real special case at the top of that
    // function: a creature that knows Blind (typeId 4010, whose slot index
    // it caches in `monster+0x32c` at AddSpell time) and whose current
    // target is *already* blinded skips the roll entirely and casts the
    // first non-Blind spell it has, rather than wasting the turn.
    ItemExecutable* ChooseSpell(bool targetBlinded) const;

    // The whole spell half of the attack, host side: choose a spell, then
    // run the real cast (`FUN_10046764`, spell_cast.h) on it.
    // `FUN_10046680`'s cooldown is deliberately not consulted -- it applies
    // only when the owner is the player (`vtable+0xcc`), so a
    // creature-owned spell is never gated by it, which is the whole reason
    // the AI can cast on every attack cadence.
    //
    // M48: this used to call the spell's `HitTarget()` straight at the
    // target, which is what made a creature's spell hitscan. It no longer
    // touches the target at all -- an offensive spell now returns a
    // projectile for the caller to launch, and a self-targeted one has
    // already applied itself to the creature by the time this returns.
    struct CastAttempt {
        ItemExecutable* spell = nullptr;  // null when the AI had nothing to cast
        SpellCastResult result;
    };
    CastAttempt CastSpellAt(SpellActor* target);

    // SetPlaySpellCasting -> `monster+0x2bd`: play sound id 6 on casting.
    // Every shipped script that sets it is a caster.
    bool playSpellCasting() const { return m_PlaySpellCasting; }

    // M46: SetAttachedWeapon -> `monster+0x2c2`, a **signed 16-bit
    // models.idx archive index**, initialised to -1 by the constructor
    // (FUN_100815e0) -- "no weapon in hand".
    //
    // The engine draws it as a *second whole model welded to the body*.
    // The monster's render override (`FUN_10083490`) runs before the
    // normal actor draw:
    //
    //   if (actor->attachedWeapon != -1) {
    //       model = engine->modelCache[0x6b38][attachedWeapon];
    //       if (model) {
    //           t = Transform();                       // FUN_1006807c
    //           t.pos    = actor->+0x94, +0x9c;
    //           t.orient = actor->+0xa4, +0xa8, +0xb2, +0xb6;
    //           t.scale  = actor->+0x5e;               // SetScale
    //           t.anim   = actor->+0x64 .. +0x80;      // incl. frame +0x70
    //           Actor3D_TransformAndSubmitModel(engine, t, actor->+0x2d4, 0, -1);
    //       }
    //   }
    //   thunk_FUN_10064ffc(actor);                     // then the body
    //
    // Copying the whole animation block rather than a bone offset is not
    // laziness: the five weapon models are **frame-aligned exports of the
    // humanoid rig**. Every one of them carries 144 frames and a
    // byte-identical 11-clip table to `male_long_tunic` / `male_short_
    // tunic` / `female_long_tunic` / `female_short_tunic` / `delfran` --
    // and those eleven are the *only* models in the 226-entry archive with
    // 144 frames. So the hand's motion is baked into the weapon's own
    // vertices, and handing it the body's absolute frame index is the
    // whole attachment mechanism. (`FUN_10065f7c`'s rotation-matrix
    // attachment path, described in RENDERER_3D.md, is a different
    // mechanism and is not what this uses.)
    //
    // The five values the shipped scripts pass are exactly the five weapon
    // models, and they sit at the same archive indices in all 21 real
    // zones' `<zone>_models.txt` (only `menu_models.txt`, the main-menu
    // pseudo-zone, leaves them NULL).
    static constexpr int kNoAttachedWeapon = -1;
    static constexpr int kWeaponModelSword = 222;   // sword.bin
    static constexpr int kWeaponModelMace = 223;    // mace.bin
    static constexpr int kWeaponModelDagger = 224;  // dagger.bin
    static constexpr int kWeaponModelBow = 225;     // bow.bin
    static constexpr int kWeaponModelAxe = 226;     // ax.bin
    int attachedWeaponModel() const { return m_AttachedWeaponModel; }

    // The second consumer, and the reason the field is gameplay state and
    // not just decoration: the AI tick (`FUN_10082224`) hardcodes the bow's
    // archive index when deciding whether this creature can attack at
    // range.
    //
    //   ranged = (stats->equipped != 0 && stats->equipped->isRanged)   // +0x48, +0x1a7
    //         || actor->spellSlot0 != 0                                 // +0x310
    //         || actor->attachedWeapon == 225;                          // +0x2c2
    //
    // A ranged creature skips the facing-cone test (`|yawDelta| < 0x200`)
    // and skips the melee reach/LOS raycast (`FUN_10082004`) entirely --
    // it just attacks anything inside `SetAttackRange`.
    //
    // The `== 225` clause is corroborated perfectly by the corpus: all 24
    // scripts that pass 225 are archers (`archer_guard`, `elite_bowman`,
    // `deadeye`, `arrow_shade`, ...) and no script that passes any other
    // value is.
    //
    // The equipped-weapon clause is not reproduced -- this port's monsters
    // carry no inventory item in the stats block's `+0x48` slot, so it can
    // never be the deciding term.
    bool ranged() const {
        return hasSpells() || m_AttachedWeaponModel == kWeaponModelBow;
    }

    // ---- M43: SetMob is a stat template, not just a flag ----
    //
    // M39 read the handler's first line (`monster+0x2ef = 1`, the zone-XP
    // opt-in) and stopped there. The rest of dispatcher case 0 is a nested
    // `switch (tier)` over four templates that **overwrite** most of the
    // stats the script just set by hand, scaled by the difficulty of the
    // zone the creature is standing in.
    //
    // This turned up as a prerequisite for creature casting rather than as
    // a separate find: 21 of the 32 shipped caster scripts call
    // SetSpellcast(0), which would give a spell-to-hit of 0 and, through
    // the gate's `if (power <= 0) return 0`, a spell that can never land.
    // All 21 call SetMob; all 11 that do not call SetMob set a real
    // spellcast instead. No exceptions either way. What SetMob gives them
    // is a willpower, and the gate is `spellcast + 2 * willpower`.

    // FUN_1008467c: 21 zone names to 1..21, in the game's own progression
    // order, and 1 for anything unlisted. Case-insensitive, as the real
    // strcasecmp chain is.
    static int ZoneDifficulty(const std::string& zoneName);

    // The template itself. `bonus` is SetMob's optional second argument,
    // added to the zone's difficulty before anything is derived from it
    // (crypt1/shriekfloater_q65.s's `SetMob(3, 1)`).
    void ApplyMobTemplate(int tier, int bonus);

    // Runs the real script's OnKilled() handler, same
    // skParseException/skRuntimeException-catching convention
    // PlayerExecutable::LoadStartingInventory already uses for a script
    // call that might throw -- azra_rat.s's OnKilled body calls real
    // quest-state methods as of M17 (see the .cpp for the full trace,
    // including one call chain it still can't reach).
    void InvokeOnKilled();

private:
    // M38: object-valued script fields -- see setValue() above.
    std::map<std::string, skRValue> m_ObjectFields;
    // M28: shared by ApplyDamage()/PlayAttackNoise() -- same null-checked
    // sounds()/audio() lookup the PlaySound() method handler already uses
    // (monster_executable.cpp), just reusable for a host-triggered call
    // instead of a script one.
    void PlayNoise(int soundId);

    const sk::StringTable* m_Strings;
    PlayerExecutable& m_Player;
    MenuStack& m_Stack;
    skInterpreter* m_Interpreter;  // for InvokeOnKilled()'s own fresh
                                    // skExecutableContext -- same reason
                                    // PlayerExecutable::LoadStartingInventory
                                    // builds one, there's no live call
                                    // frame's context to reuse when the
                                    // host (not a script) triggers the call.

    int m_NameId = -1;
    std::string m_Id;
    int m_ExpWorth = 0;
    int m_Attack = 0;
    int m_Defense = 0;
    int m_Spellcast = 0;
    int m_MagicResistance = 0;
    int m_Will = 0;  // M37: stats+0x1a -- see spellResistance()
    int m_CreatureKind = kCreatureNormal;  // M37: monster+0x2d0
    // M39: see countsForZoneExperience() / TakePendingPosition().
    bool m_CountsForZoneExperience = false;
    int m_PositionX = 0, m_PositionY = 0, m_PositionZ = 0;
    bool m_PositionDirty = false;
    int m_DamageMin = 0;
    int m_DamageMax = 0;
    int m_ArmorValue = 0;
    int m_MaxHealth = 1;
    int m_CurrentHealth = 1;
    int m_Wimpy = 0;
    // Real constructor defaults (FUN_100815e0): +0x2b8 = 0x7fff,
    // +0x2dc = 0x6a4. Both in the scaled-squared form -- see chaseRadius().
    int m_ChaseRadius = 0x7fff;
    int m_AttackRange = 0x6a4;
    int m_ProjectileArt = -1;  // M49, monster+0x2d8 -- see shootsProjectile()
    int m_Mob = 0;
    int m_Level = 0;  // M30: SetLevel/GetLevel, read by real spell damage formulas
    // M48: the stats block's +0x2e / +0x2c. No shipped creature script sets
    // either, so both stay 0 and every deduction clamps straight back.
    int m_Magicka = 0;
    int m_Fatigue = 0;
    // M32: see the AI package block above. Defaults match the real actor
    // constructor (FUN_100815e0): package -1, timers clear.
    int m_AiPackage = kAiAsleep;
    int m_SavedAiPackage = kAiIdle;  // monster+0x2fc
    int m_AiPackageTimer = 0;        // monster+0x300
    int m_AttackCadence = 0;         // monster+0x2c4
    // M33/M34/M43: the stats block -- see actor_stats.h. Holds the timed
    // stat modifiers, the effect flags and both periodic channels, plus the
    // paralysis lockout.
    ActorStats m_Stats;
    // M43: the creature's own spells. It *owns* them -- the real AddSpell
    // adds the spell entity to the monster's object collection
    // (`FUN_1006cf38(monster+0x1f8, spell)`) and sets itself as its owner
    // (`FUN_1006d510(spell, monster)`) -- so the unique_ptrs live here,
    // taken from LevelExecutable's pending-CreateEntity slot the same way
    // ItemExecutable::AddObject already claims a loot bag's contents.
    std::vector<std::unique_ptr<ItemExecutable>> m_OwnedSpells;
    SpellSlot m_SpellSlots[kSpellSlots];
    int m_BlindSpellSlot = -1;  // monster+0x32c, 0xff when there is none
    int m_MeleeRoll = 0;        // monster+0x304
    bool m_PlaySpellCasting = false;  // monster+0x2bd
    // M46: monster+0x2c2, -1 from the constructor -- see
    // attachedWeaponModel().
    int m_AttachedWeaponModel = kNoAttachedWeapon;
    int m_Skin = 0;
    int m_Scale = 256;  // 8.8 fixed point, 256 == 1:1
    int m_IdleAnim = -1;
    int m_WalkAnim = -1;
    int m_SwingAnim = -1;
    int m_DeathAnim = -1;
    int m_CurrentAnim = -1;
    bool m_Aggressive = false;
    bool m_Alive = true;
    bool m_Usable = false;
    int m_UseTextId = -1;
    bool m_Invulnerable = false;
    std::string m_LootTag;  // M21: see lootTag()'s comment.
    bool m_Destroyed = false;  // M23: see destroyed()'s comment.
    // M28: see PlayAttackNoise()'s comment -- -1 (SetXNoise() never
    // called, e.g. an NPC) is a real, harmless no-play case, same
    // sentinel convention m_NameId/m_UseTextId already use.
    int m_AttackNoiseId = -1;
    int m_DeathNoiseId = -1;
    int m_IsHitNoiseId = -1;
};

}  // namespace sk_bindings
