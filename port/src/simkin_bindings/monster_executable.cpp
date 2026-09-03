#include "simkin_bindings/monster_executable.h"

#include <cstdio>
#include <cstdlib>

#include "assets/sound_archive.h"
#include "assets/string_table.h"
#include "audio/audio_engine.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace sk_bindings {

MonsterExecutable::MonsterExecutable(const skString& filename, skExecutableContext& ctxt,
                                      const sk::StringTable* strings, PlayerExecutable& player,
                                      MenuStack& stack)
    : skScriptedExecutable(filename, ctxt),
      m_Strings(strings),
      m_Player(player),
      m_Stack(stack),
      m_Interpreter(ctxt.getInterpreter()) {}

std::string MonsterExecutable::name() const {
    if (m_Strings && m_NameId >= 0) return m_Strings->Get(m_NameId);
    return m_Id.empty() ? std::string("?") : m_Id;
}

void MonsterExecutable::PlayNoise(int soundId) {
    // M28: -1 covers both "SetXNoise() never called" (e.g. an NPC that
    // never sets one) and any script that genuinely passes a negative id
    // -- neither should reach SoundArchive::GetSound(), same defensive
    // shape the real slot-index convention already assumes elsewhere.
    if (soundId < 0 || !m_Stack.sounds() || !m_Stack.audio()) return;
    const sk::Sound* sound = m_Stack.sounds()->GetSound(soundId);
    if (sound) m_Stack.audio()->PlaySfx(*sound);
}

void MonsterExecutable::PlayAttackNoise() { PlayNoise(m_AttackNoiseId); }

void MonsterExecutable::ApplyDamage(int amount) {
    // m_Invulnerable (M16): real essential-NPC scripts (Tanyin Aldwyr and
    // the other named quest NPCs) call SetInvulnerable(true) in Init() --
    // honoring it here means main.cpp's melee target selection doesn't
    // even need its own separate check for the common case, though it
    // still skips them too (see main.cpp) so they're never shown as a
    // combat target in the first place.
    if (!m_Alive || amount <= 0 || m_Invulnerable) return;
    m_CurrentHealth -= amount;
    if (m_CurrentHealth <= 0) {
        m_CurrentHealth = 0;
        m_Alive = false;
        // M28: death noise wins over hit noise -- both fire from the same
        // single ApplyDamage() choke point (melee and spell HitTarget
        // both route through here), so there's no risk of ever double-
        // playing.
        PlayNoise(m_DeathNoiseId);
    } else {
        PlayNoise(m_IsHitNoiseId);
    }
}

void MonsterExecutable::InvokeOnUse() {
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnUse's "(s)" parameter, same convention every
                                // Init(s)/OnUse(s)/OnKilled(s) caller in this codebase already
                                // uses (main.cpp's zone-load block, etc.) -- M17 found this one
                                // and InvokeOnKilled() below had drifted from it (empty args),
                                // latent until a real script actually referenced its own `s`
                                // parameter on a path this port hadn't exercised before.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnUse"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnUse(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnUse(): %s\n", e.toString().ptr());
    }
}

void MonsterExecutable::InvokeOnKilled() {
    // azra_rat.s's OnKilled body: `if (GetPlayer().QuestSolved(0)) {
    // return; } else { GetPlayer().AddMonsterKilled(203); if
    // (GetPlayer().MonstersKilled(203) >= 8) { ...if(not
    // GetPlayer().QuestSolved(0)) { if(s=false){Delay(2,0);} }
    // Trthgar=Level.GetEntity("trthgar"); Trthgar.SetPositionMirror(...);
    // ... SetQuestSolved(0,true); } }`. M17: QuestSolved/
    // AddMonsterKilled/MonstersKilled are now real state on
    // PlayerExecutable (see its class comment) -- the kill counter
    // genuinely reaches 8 real rat kills. M18 (simkin_bindings/
    // level_executable.h) closed the remaining gap: a real `Level` global
    // now exists, and `GetEntity("trthgar")` resolves to a real live
    // `Gravel_Trothgar.s`-backed object whenever the host has registered
    // it (main.cpp's zone-load block does, for any zone with that real
    // placement) -- so `SetQuestSolved(0,true)` genuinely runs and quest
    // id 0 flips solved (verified end to end, `m18_level_smoke.cpp`). A
    // host that never registers "trthgar" (`quest_smoke`'s own isolated
    // setup, which doesn't load a full zone) still sees `GetEntity`
    // resolve gracefully to "not found," so
    // `Trthgar.SetPositionMirror(...)` throws its own clean "Cannot call
    // Method ... on a non object" instead -- same net effect (quest stays
    // unsolved there), just no longer because `Level` itself is missing.
    // `InvokeOnKilled()`'s existing try/catch already logs and swallows
    // either exception cleanly rather than crashing, same as any other
    // script error this host-triggered call might hit.
    if (!m_Interpreter) return;
    skRValueArray args;
    args.append(skRValue(0));  // placeholder for OnKilled's "(s)" parameter, see InvokeOnUse()'s
                                // comment above -- this call site had the same missing-argument
                                // bug, found by the same M17 test.
    skRValue ret;
    skExecutableContext ctxt(m_Interpreter);
    try {
        method(skString("OnKilled"), args, ret, ctxt);
    } catch (skParseException& e) {
        std::printf("MonsterExecutable: PARSE ERROR in OnKilled(): %s\n", e.toString().ptr());
    } catch (skRuntimeException& e) {
        std::printf("MonsterExecutable: RUNTIME ERROR in OnKilled(): %s\n", e.toString().ptr());
    }
}

int MonsterExecutable::statModifier(int statIndex) const {
    for (const StatModifier& m : m_StatModifiers) {
        if (m.statIndex == statIndex) return m.delta;
    }
    return 0;
}

void MonsterExecutable::ApplyStatModifier(int statIndex, int delta, int durationSeconds) {
    // FUN_1004aa28's mode-1 path: if this stat already carries a
    // modifier, the incoming one only takes effect when its magnitude is
    // strictly greater (the real code compares absolute values and
    // returns early otherwise, then unlinks the weaker record before
    // adding the new one). So there is never more than one per stat.
    int durationUnits = durationSeconds * 256;  // the real `duration * 0x100`
    for (StatModifier& m : m_StatModifiers) {
        if (m.statIndex != statIndex) continue;
        if (std::abs(delta) <= std::abs(m.delta)) return;  // weaker -- rejected
        m.delta = delta;
        m.remaining = durationUnits;
        return;
    }
    m_StatModifiers.push_back({statIndex, delta, durationUnits});
}

void MonsterExecutable::ApplyEffectFlag(int flagBit, int dotKind, int durationSeconds) {
    // FUN_1004bae8: OR the flag into the bitmask, set the damage-over-time
    // kind, and arm the shared effect timer as `duration << 8`.
    m_EffectFlags |= flagBit;
    m_DotKind = dotKind;
    m_EffectTimer = durationSeconds * 256;
    m_DotAccumulator = 0;
}

void MonsterExecutable::ApplyBurn(int damagePerTick, int durationSeconds) {
    // FUN_100458e4's IgniteFoe branch, in its own write order.
    m_BurnKind = kPeriodicBurn;
    m_BurnTimer = durationSeconds * 256;  // the real `magnitude << 9`
    m_BurnAccumulator = 0;
    m_DotKind = damagePerTick;  // +0x76 -- deliberately the shared field
}

void MonsterExecutable::SetAiPackageTimed(int package, int durationUnits) {
    // FUN_10086b98, byte for byte in behaviour: set the package, arm the
    // countdown as `duration << 8`, and -- only for the flee package --
    // forget the current target and record idle as what to return to.
    m_AiPackage = package;
    m_AiPackageTimer = durationUnits << 8;
    if (package != kAiFlee) return;
    m_SavedAiPackage = kAiIdle;
}

void MonsterExecutable::TickAi(int deltaUnits) {
    if (m_AiPackageTimer > 0) {
        m_AiPackageTimer -= deltaUnits;
        if (m_AiPackageTimer <= 0) {
            m_AiPackageTimer = 0;
            // The real tick's `if (timer expired) package = saved` --
            // this is what ends a Fear effect.
            m_AiPackage = m_SavedAiPackage;
        }
    }
    if (m_ParalysisTimer > 0) {
        m_ParalysisTimer -= deltaUnits;
        if (m_ParalysisTimer < 0) m_ParalysisTimer = 0;
    }

    // M33: timed stat modifiers expire independently of each other.
    for (size_t i = 0; i < m_StatModifiers.size();) {
        m_StatModifiers[i].remaining -= deltaUnits;
        if (m_StatModifiers[i].remaining <= 0) {
            m_StatModifiers.erase(m_StatModifiers.begin() + static_cast<long>(i));
        } else {
            ++i;
        }
    }

    // M33: the shared effect timer (stats block +0x72). When it runs out
    // the flags clear -- blindness lifts, poison stops.
    if (m_EffectTimer > 0) {
        m_EffectTimer -= deltaUnits;
        if (m_EffectTimer <= 0) {
            m_EffectTimer = 0;
            // M34 correction. The real expiry is not a clear-to-zero:
            //
            //   flags  = 2;   // an assignment, not an &=
            //   dotKind = 3;
            //
            // Bit 1 is left set (its meaning is not decoded -- nothing in
            // this port reads any bit but blind's 4 and poison's 8, for
            // which `= 2` and `= 0` are identical), and dotKind is *reset
            // to poison's 3* rather than cleared. That second write used to
            // be unobservable, because channel 1 stops ticking the moment
            // its own timer hits zero. It stops being unobservable now that
            // the burn channel reads the same field: a poison wearing off
            // while a creature is on fire really does triple its burn from
            // 1/sec to 3/sec. Faithful, and the reason this is written the
            // odd way it is.
            m_EffectFlags = 2;
            m_DotKind = 3;
            m_DotAccumulator = 0;
        } else if (m_DotKind > 0) {
            // The real damage-over-time tick (FUN_10049780):
            //
            //   accumulator += frameDelta;
            //   if ((accumulator >> 8) > 0) { accumulator = 0;
            //                                 DoDamage(dotKind); }
            //
            // Note the `+0x76` field is both the effect kind *and* the
            // damage dealt -- it is passed straight to DoDamage -- so
            // poison's 3 means three points every 256 delta units, the
            // same "one second" every other engine timer counts in. The
            // real code zeroes the accumulator rather than subtracting,
            // so a long frame cannot bank extra ticks; reproduced.
            m_DotAccumulator += deltaUnits;
            if (m_DotAccumulator >= 256) {
                m_DotAccumulator = 0;
                ApplyDamage(m_DotKind);
            }
        }
    }

    // M34: the second periodic channel (+0x78/+0x7a/+0x7c). Same one-second
    // accumulator shape as channel 1, but selected by its own `kind`:
    //
    //   kind 6: magicka += spellPower
    //   kind 7: health  += spellPower, clamped to maxHealth and to 0
    //   kind 8: health  -= (+0x76), and on reaching 0 calls the actor's
    //           kill vtable slot (+0x28) directly
    //
    // Only kind 8 is reachable from the status-effect dispatcher -- the
    // IgniteFoe branch is its single arming site anywhere in that function.
    // Kinds 6 and 7 read the stats block's spell-power short (+0x34), which
    // this port has no equivalent of and nothing decompiled so far arms, so
    // they are documented here rather than guessed at.
    //
    // Note kind 8 writes health *directly* instead of going through
    // DoDamage the way poison does, so a burn is not reduced by armour or
    // resistance at all -- it is the flat 1/sec it says it is. The one
    // deliberate divergence: this routes through ApplyDamage() anyway, so
    // that a real SetInvulnerable(true) quest NPC still cannot be burned to
    // death (that guard is a port safety property, and the alternative is
    // an essential NPC dying to a stray fire spell and stranding a quest).
    if (m_BurnTimer > 0) {
        if (m_BurnKind == kPeriodicBurn) {
            m_BurnAccumulator += deltaUnits;
            if (m_BurnAccumulator >= 256) {
                m_BurnAccumulator = 0;
                ApplyDamage(m_DotKind);
            }
        }
        m_BurnTimer -= deltaUnits;
        if (m_BurnTimer <= 0) {
            m_BurnTimer = 0;
            m_BurnKind = kPeriodicNone;
        }
    }
}

bool MonsterExecutable::ConsumeAttackCadence(int deltaUnits) {
    m_AttackCadence += deltaUnits;
    if (m_AttackCadence <= kAiAttackCadenceThreshold) return false;
    // Real reset: `rand & 0x1f`. Uses the same host RNG the rest of this
    // port's combat rolls use rather than reproducing the engine's own
    // generator, which is not decompiled.
    m_AttackCadence = std::rand() & 0x1f;
    return true;
}

bool MonsterExecutable::method(const skString& methodName, skRValueArray& args,
                                skRValue& returnValue, skExecutableContext& context) {
    if (methodName == skString("PlaySound") && args.entries() >= 1) {
        // M27: a real monster script's own bare self-call (e.g. monsters/
        // umbra_keth.s's `PlaySound(83)`) -- same real per-zone-manifest
        // slot-index convention PlayerExecutable::PlaySound() documents in
        // full (assets/sound_archive.h).
        if (m_Stack.sounds() && m_Stack.audio()) {
            const sk::Sound* sound = m_Stack.sounds()->GetSound(args[0].intValue());
            if (sound) m_Stack.audio()->PlaySfx(*sound);
        }
        return true;
    }
    if (methodName == skString("SetAggressive") && args.entries() == 1) {
        m_Aggressive = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetName") && args.entries() == 1) {
        m_NameId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetID") && args.entries() == 1) {
        m_Id = ToStdString(args[0].str());
        return true;
    }
    if (methodName == skString("SetExpWorth") && args.entries() == 1) {
        m_ExpWorth = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetAttack") && args.entries() == 1) {
        m_Attack = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDefense") && args.entries() == 1) {
        m_Defense = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSpellcast") && args.entries() == 1) {
        m_Spellcast = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMagicResistance") && args.entries() == 1) {
        m_MagicResistance = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDamageMin") && args.entries() == 1) {
        m_DamageMin = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDamageMax") && args.entries() == 1) {
        m_DamageMax = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetArmorValue") && args.entries() == 1) {
        m_ArmorValue = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMaxHealth") && args.entries() == 1) {
        m_MaxHealth = args[0].intValue();
        m_CurrentHealth = m_MaxHealth;
        return true;
    }
    if (methodName == skString("SetWimpy") && args.entries() == 1) {
        m_Wimpy = args[0].intValue();
        return true;
    }
    // ---- M32: the real Ai* package calls ----
    //
    // Only AiDetect() and AiSleep() are ever called by any script in the
    // corpus; the rest exist for completeness and for the native paths
    // (the Fear spell reaches AiFlee's package through
    // SetAiPackageTimed()). Package values are the decompiled ones -- see
    // monster_executable.h's AiPackage enum.
    if (methodName == skString("AiDetect") && args.entries() == 0) {
        m_AiPackage = kAiIdle;
        return true;
    }
    if (methodName == skString("AiSleep") && args.entries() == 0) {
        m_AiPackage = kAiAsleep;
        return true;
    }
    if (methodName == skString("AiAttack") && args.entries() >= 1) {
        m_AiPackage = kAiPursue;
        return true;
    }
    if (methodName == skString("AiFlee") && args.entries() >= 1) {
        // The dispatcher's own flee case sets the package with no timer --
        // the timed form is what the Fear spell uses.
        m_AiPackage = kAiFlee;
        m_SavedAiPackage = kAiIdle;
        return true;
    }
    if (methodName == skString("AiSpellAssistTarget") && args.entries() >= 1) {
        // Faithfully inert: the dispatcher stores package 6 and a target,
        // and **nothing anywhere in the binary ever reads package 6** --
        // a creature left in it matches neither branch of the real AI tick
        // and simply stops acting. Reproduced rather than invented.
        m_AiPackage = kAiSpellAssist;
        return true;
    }
    if ((methodName == skString("AiActivate") || methodName == skString("AiWounded") ||
         methodName == skString("AiPursue")) &&
        args.entries() >= 0) {
        // Genuine no-ops: each of these dispatcher cases falls straight
        // through to the shared `break` and stores nothing.
        return true;
    }
    if (methodName == skString("SetParalyzed") && args.entries() >= 1) {
        SetParalyzed(args[0].intValue());
        return true;
    }
    // M31: the real stand-off distance (monster+0x2dc). Same
    // scaled-squared units as SetChaseRadius -- see monster_executable.h.
    if (methodName == skString("SetAttackRange") && args.entries() == 1) {
        m_AttackRange = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetMeleeAttackRange") && args.entries() == 1) {
        // Accepted and deliberately ignored: the real dispatcher's case for
        // this name falls straight through to its shared `break` and stores
        // nothing, so it is a genuine no-op in the shipped game too (only
        // one script in the whole corpus calls it). Handled explicitly so
        // it stops being reported as "not implemented" when reproducing it
        // faithfully means doing nothing.
        return true;
    }
    if (methodName == skString("SetChaseRadius") && args.entries() == 1) {
        m_ChaseRadius = args[0].intValue();
        return true;
    }
    // M30: a creature's level. Real spell scripts scale their damage off
    // it (`Random(3, (GetOwner().GetLevel() + 1) * 2)`), so leaving the
    // setter soft-failing meant GetLevel() had nothing to return.
    if (methodName == skString("SetLevel") && args.entries() == 1) {
        m_Level = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetLevel") && args.entries() == 0) {
        returnValue = skRValue(m_Level);
        return true;
    }
    if (methodName == skString("SetMob") && args.entries() == 1) {
        m_Mob = args[0].intValue();
        return true;
    }
    // M28: the real animation clip numbers. Every monster script sets all
    // four; they index the model resource's own clip table (see
    // world/model_archive.h's AnimationClip -- decoded this session). All
    // of these soft-failed before, so creatures rendered as static
    // resting-pose statues sliding around the level.
    if (methodName == skString("SetIdleAnimation") && args.entries() == 1) {
        m_IdleAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetWalkAnimation") && args.entries() == 1) {
        m_WalkAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetSwingAnimation") && args.entries() == 1) {
        m_SwingAnim = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDeathAnimation") && args.entries() == 1) {
        m_DeathAnim = args[0].intValue();
        return true;
    }
    // `PlayAnimation(n)` / `PlayAnimationOffset(n)` -- both appear in real
    // Init() bodies to set the creature's starting pose (e.g. arat.s's
    // `PlayAnimation(0)`). Treated identically here: the host AI loop
    // overrides the clip every tick from the creature's own state anyway,
    // so this only decides what it looks like before it first acts.
    if ((methodName == skString("PlayAnimation") ||
         methodName == skString("PlayAnimationOffset")) &&
        args.entries() == 1) {
        m_CurrentAnim = args[0].intValue();
        return true;
    }
    // Real per-instance appearance, previously soft-failed and therefore
    // invisible in the port (every creature rendered as skin 0 at 1:1
    // scale). Both showed up in a real play session's own soft-fail log.
    if (methodName == skString("SetSkin") && args.entries() == 1) {
        // A models.idx resource genuinely carries several skins
        // (Model::skinCount, docs/MODEL_FORMAT.md) -- e.g. the real
        // corpus picks skin 9/10 for some creature variants, which the
        // port was drawing with skin 0's texture.
        m_Skin = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetScale") && args.entries() == 1) {
        // 8.8 fixed point, so 256 == 1:1 -- consistent with every real
        // call site: literal values cluster at 230-306, and azra_rat.s
        // uses `SetScale(Random(206,306))` to vary rat size around 256.
        // A value of exactly 256 being the identity is what makes that
        // range read as "0.8x to 1.2x".
        m_Scale = args[0].intValue();
        return true;
    }
    if (methodName == skString("GetPlayer") && args.entries() == 0) {
        returnValue = skRValue(static_cast<skiExecutable*>(&m_Player), false);
        return true;
    }
    if (TryHandleRandom(methodName, args, returnValue)) {
        // M21: azra_rat.s's own `SetScale(Random(206,306))` -- bare, same
        // real gap native_binding_common.h's TryHandleRandom() comment
        // explains (previously soft-failed to 0 on every monster).
        return true;
    }
    // M16: NPC-mode fields/calls -- see class comment.
    if (methodName == skString("SetUsable") && args.entries() == 1) {
        m_Usable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("SetUseText") && args.entries() == 1) {
        m_UseTextId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetInvulnerable") && args.entries() == 1) {
        m_Invulnerable = args[0].boolValue();
        return true;
    }
    if (methodName == skString("OpenMenu") && args.entries() == 1) {
        // M17: ReopenMenu(), not OpenMenu() -- an NPC's OnUse() call here
        // starts a fresh conversation each time, and the real quest-state
        // branching lives in the target menu's Init() (see menu_stack.h's
        // comment), which needs to rerun on every visit, not just the
        // first.
        m_Stack.ReopenMenu(ToStdString(args[0].str()));
        return true;
    }
    if (methodName == skString("SetLoot") && args.entries() >= 2) {
        // M21: see lootTag()'s comment -- main.cpp's death handling
        // resolves and spawns this as a real world pickup.
        m_LootTag = ToStdString(args[1].str());
        return true;
    }
    if (methodName == skString("DestroyObjectMirror")) {
        // M23: see destroyed()'s comment -- azra.s's own
        // `M1.DestroyObjectMirror(M1)` (self-passed, network/replication
        // bookkeeping in the original, same `DoorOpened()`-style pattern
        // -- this port has no multiplayer) after a real save flag says
        // an entity is no longer relevant.
        m_Destroyed = true;
        return true;
    }
    if (methodName == skString("SetAttackNoise") && args.entries() == 1) {
        // M28: see PlayAttackNoise()'s comment.
        m_AttackNoiseId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetDeathNoise") && args.entries() == 1) {
        m_DeathNoiseId = args[0].intValue();
        return true;
    }
    if (methodName == skString("SetIsHitNoise") && args.entries() == 1) {
        m_IsHitNoiseId = args[0].intValue();
        return true;
    }
    // SetWalkAnimation/SetSwingAnimation/SetDeathAnimation/
    // SetIdleAnimation/PlayAnimationOffset/SetScale/AiDetect -- azra_rat.s
    // calls all of these, but this port has no skeletal animation and
    // renders every entity at a fixed scale/skin (consistent with every
    // prior milestone -- see docs/PORT_ROADMAP.md's M12 entry), so they
    // fall through to the soft-fail below rather than getting dedicated
    // no-op handlers -- there's nothing meaningful to store them into yet.
    if (skScriptedExecutable::method(methodName, args, returnValue, context)) {
        return true;
    }
    return SoftFailNativeCall("Monster", methodName, args, returnValue);
}

}  // namespace sk_bindings
