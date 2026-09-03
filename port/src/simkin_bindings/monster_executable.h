#pragma once

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
#include <string>
#include <vector>

#include "skScriptedExecutable.h"

class skInterpreter;

namespace sk {
class StringTable;
}

namespace sk_bindings {

// M32: the engine's own per-frame delta (what FUN_1001afa4 returns) is the
// unit every AI timer below counts in. Its magnitude was not recovered --
// it is a field the engine writes each frame -- so this port uses its own
// fixed tick period, 40ms (docs/RENDER_LOOP.md's real 25Hz CPeriodic).
// That makes the decompiled 0x100 attack-cadence threshold work out to
// ~6.4 ticks, i.e. roughly one attack attempt every 256ms, and the
// `rand & 0x1f` reset a sub-frame jitter -- both of which are the
// proportions the real constants imply.
constexpr int kAiFrameDeltaUnits = 40;

// The decompiled attack-cadence threshold (monster+0x2c4 must exceed this
// before the creature may act).
constexpr int kAiAttackCadenceThreshold = 0x100;

class MenuStack;
class PlayerExecutable;

class MonsterExecutable : public skScriptedExecutable {
public:
    // `strings` may be null, same fallback convention as ItemExecutable.
    MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                       const sk::StringTable* strings, PlayerExecutable& player, MenuStack& stack);

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

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
    bool aggressive() const { return m_Aggressive; }
    // M22: real SetMagicResistance() -- stored since M12, never read back
    // until now (spellcasting's own damage formula, ItemExecutable::
    // DoAttackRoll()).
    int magicResistance() const { return m_MagicResistance; }

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
    int skin() const { return m_Skin; }
    // SetScale()'s 8.8 fixed-point value as a plain multiplier (256 -> 1).
    float scale() const { return m_Scale > 0 ? static_cast<float>(m_Scale) / 256.0f : 1.0f; }

    // See chaseRadius(): the real comparison value is (d^2)/256, so the
    // distance it stands for is 16*sqrt(value).
    static float ScaledSquaredToUnits(int scaledSquared) {
        if (scaledSquared <= 0) return 0.0f;
        return 16.0f * std::sqrt(static_cast<float>(scaledSquared));
    }

    // ---- M33: the real timed status effects ----
    //
    // Decompiled from FUN_100458e4 (the status-effect dispatcher, which
    // selects on the spell entity's own entities.txt typeId) and its two
    // primitives, FUN_1004aa28 (a timed stat modifier) and FUN_1004bae8
    // (a timed effect flag). See docs/WORLD_MODEL.md.
    //
    // The stat indices are the ones FUN_1004ad40 switches on; the three
    // the status effects actually touch are confirmed against the
    // character-stats dispatcher (0x10048244), where SetAttack writes the
    // stat block's first short, SetDefense its second and SetArmorValue
    // its seventh:
    enum StatIndex { kStatAttack = 1, kStatDefense = 2, kStatArmor = 7 };

    // FUN_1004bae8's effect-flag bits (stats block +0x44).
    enum EffectFlag { kEffectFlagBlind = 4, kEffectFlagPoison = 8 };

    // FUN_1004aa28 mode 1: apply a timed modifier to one stat, but only
    // if it is *stronger* than whatever is already on that stat (the real
    // function compares absolute deltas and returns early otherwise, then
    // removes the weaker one before adding). Duration is in seconds; the
    // real code stores an absolute expiry of `now + duration * 0x100`, the
    // same <<8 convention every other engine timer uses.
    void ApplyStatModifier(int statIndex, int delta, int durationSeconds);

    // FUN_1004bae8: set an effect flag and arm the shared effect timer.
    // `dotKind` is the real +0x76 value -- 3 for poison, 0 for a flag with
    // no damage-over-time (blind).
    void ApplyEffectFlag(int flagBit, int dotKind, int durationSeconds);

    bool blinded() const { return (m_EffectFlags & kEffectFlagBlind) != 0; }
    bool poisoned() const { return (m_EffectFlags & kEffectFlagPoison) != 0; }

    // Net modifier currently applied to a stat (0 when nothing is active).
    int statModifier(int statIndex) const;

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
    void SetParalyzed(int durationUnits) { m_ParalysisTimer = durationUnits; }
    bool paralyzed() const { return m_ParalysisTimer > 0; }

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

    // Runs the real script's OnKilled() handler, same
    // skParseException/skRuntimeException-catching convention
    // PlayerExecutable::LoadStartingInventory already uses for a script
    // call that might throw -- azra_rat.s's OnKilled body calls real
    // quest-state methods as of M17 (see the .cpp for the full trace,
    // including one call chain it still can't reach).
    void InvokeOnKilled();

private:
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
    int m_Mob = 0;
    int m_Level = 0;  // M30: SetLevel/GetLevel, read by real spell damage formulas
    // M32: see the AI package block above. Defaults match the real actor
    // constructor (FUN_100815e0): package -1, timers clear.
    int m_AiPackage = kAiAsleep;
    int m_SavedAiPackage = kAiIdle;  // monster+0x2fc
    int m_AiPackageTimer = 0;        // monster+0x300
    int m_ParalysisTimer = 0;        // monster+0x294
    int m_AttackCadence = 0;         // monster+0x2c4
    // M33: status effects. At most one modifier per stat -- the real
    // FUN_1004aa28 replaces a weaker one and rejects a weaker new one, so
    // a list is never needed.
    struct StatModifier {
        int statIndex = 0;
        int delta = 0;
        int remaining = 0;  // engine delta units
    };
    std::vector<StatModifier> m_StatModifiers;
    int m_EffectFlags = 0;    // stats block +0x44
    int m_EffectTimer = 0;    // +0x72, `duration << 8`
    int m_DotKind = 0;        // +0x76 -- 3 = poison
    int m_DotAccumulator = 0; // host-side: paces poison damage, see the .cpp
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
