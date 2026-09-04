#pragma once

// M43: the actor **stats block** -- the object every timed status effect
// actually lives on in the real engine.
//
// This is not a port-side abstraction invented to share code. It is the
// real architecture, and recovering it is what M43's "monsters as spell
// casters" pass turned up. `FUN_1002fd30(actor)` resolves an actor to one
// of these:
//
//     if (actor->vtable[0xe4]())        return actor + 0x224;   // a monster
//     else if (actor->vtable[0xcc]())   return actor + 0x3ac;   // the player
//     else                              return 0;
//
// -- two different offsets into two unrelated classes, one shared layout.
// Every status primitive in the engine takes *this* pointer, never the
// actor: FUN_1004aa28 (timed stat modifier), FUN_1004bae8 (timed effect
// flag), FUN_1004bb88 (SetHealth), FUN_10049780 (the two periodic
// channels), FUN_1004bc60/FUN_1004bbd0 (spell to-hit / resistance). So a
// monster and the player are the same thing as far as being poisoned,
// blinded, drained or set on fire is concerned -- which is exactly what has
// to be true for a monster to cast at the player.
//
// Everything here was decompiled for M33/M34 and lived on
// MonsterExecutable until now; M43 moved it, unchanged, to where the real
// engine keeps it. MonsterExecutable's own accessors remain as forwarders,
// so nothing that already used them had to change.

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace sk_bindings {

class ActorStats {
public:
    // The stat indices FUN_1004ad40 switches on; the three the status
    // effects actually touch are confirmed against the character-stats
    // dispatcher (0x10048244), where SetAttack writes the stat block's
    // first short, SetDefense its second and SetArmorValue its seventh.
    enum StatIndex { kStatAttack = 1, kStatDefense = 2, kStatArmor = 7 };

    // FUN_1004bae8's effect-flag bits (stats block +0x44). M48 adds the
    // disease bit, which is named by the *cure*: `spells\CureDisease.s`
    // clears exactly `0x10`, next to `spells\CurePoison.s` clearing `8`.
    // Nothing in the decompiled status dispatcher ever sets it -- the real
    // Disease branch is two stat modifiers and no flag -- so the bit is
    // carried because the cure names it, not because anything raises it.
    enum EffectFlag { kEffectFlagBlind = 4, kEffectFlagPoison = 8, kEffectFlagDisease = 0x10 };

    // The second periodic channel's `kind` selector (+0x7c). FUN_10049780
    // implements three of them; M48 found the two arming sites the burn's
    // did not account for, in the real cast (spell_cast.h).
    enum PeriodicKind {
        kPeriodicNone = 0,
        // M48. Sanctuary's channel, and FUN_10049780 gives it **no tick
        // behaviour at all** -- it is purely a duration. What it gates is
        // elsewhere: FUN_10042394 refuses to swap weapons while `+0x7c ==
        // 4`, and the channel's own expiry stamps the player's Sanctuary
        // re-cast cooldown.
        kPeriodicSanctuaryTimer = 4,
        // M48 -- CORRECTED. `+0x2c` is **fatigue**, not magicka: magicka is
        // `+0x2e` (FUN_1004bb20 clamps it against `+0x28`, while FUN_1004bb54
        // clamps `+0x2c` against `+0x26`), and M47 independently landed on
        // the same field when it found a weapon swing costing 4 points of
        // `player+0x3d8` -- which is `player+0x3ac` + 0x2c. `spells\Energize.s`
        // arms this one.
        kPeriodicFatigueRegen = 6,  // +0x2c += the actor's own level, once per second
        kPeriodicHealthRegen = 7,   // health += the actor's own level, clamped to max
        kPeriodicBurn = 8,          // health -= (+0x76), and kills at 0
    };

    // FUN_1004aa28 mode 1: apply a timed modifier to one stat, but only if
    // it is *stronger* than whatever is already on that stat (the real
    // function compares absolute deltas and returns early otherwise, then
    // removes the weaker one before adding). Duration is in seconds; the
    // real code stores an absolute expiry of `now + duration * 0x100`, the
    // same <<8 convention every other engine timer uses.
    void ApplyStatModifier(int statIndex, int delta, int durationSeconds) {
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

    // FUN_1004bae8: OR the flag into the bitmask, set the damage-over-time
    // kind, and arm the shared effect timer as `duration << 8`.
    void ApplyEffectFlag(int flagBit, int dotKind, int durationSeconds) {
        m_EffectFlags |= flagBit;
        m_DotKind = dotKind;
        m_EffectTimer = durationSeconds * 256;
        m_DotAccumulator = 0;
    }

    // FUN_100458e4's IgniteFoe branch, in its own write order. Note the
    // last write: the burn's per-tick damage lives in +0x76, the *same
    // field poison uses* for both its kind and its damage. The two channels
    // genuinely share it in the real engine, so poisoning a burning
    // creature really does make its flames tick for 3 instead of 1 (and
    // vice versa). Reproduced rather than tidied up -- see Tick().
    void ApplyBurn(int damagePerTick, int durationSeconds) {
        m_BurnKind = kPeriodicBurn;
        m_BurnTimer = durationSeconds * 256;  // the real `magnitude << 9`
        m_BurnAccumulator = 0;
        m_DotKind = damagePerTick;  // +0x76 -- deliberately the shared field
    }

    // The real action lockout (a monster's own field is +0x294; the
    // Paralyze branch reaches it through a vtable slot on the stats block,
    // FUN_100458e4's `(**(code **)(*(int *)(iVar4 + 0x80) + 0x44))`, which
    // is why it works against the player too). Duration is already in
    // engine delta units, not seconds.
    void SetParalyzed(int durationUnits) { m_ParalysisTimer = durationUnits; }
    bool paralyzed() const { return m_ParalysisTimer > 0; }

    // ---- M48: the four primitives the real cast's self-targeted half
    // uses, none of which had a caller until now (spell_cast.h). ----

    // FUN_1004bd08: drop one effect-flag bit and stop the shared timer.
    // `spells\CurePoison.s`'s entire body is `FUN_1004bd08(stats, 8)`.
    // Note the real function is a no-op when the bit is not set -- it does
    // not zero the timer unconditionally -- so curing poison off a blinded
    // creature leaves the blindness running.
    void ClearEffectFlag(int flagBit) {
        if ((m_EffectFlags & flagBit) == 0) return;
        m_EffectFlags &= ~flagBit;
        m_EffectTimer = 0;
    }

    // FUN_1004bd24: remove one timed modifier. The real signature takes a
    // stat index *and a name* -- CureDisease removes the modifiers named
    // "DISEASE" and "DISEASE_DEF", the two the Disease branch applies to
    // attack and defense. This port's modifiers carry no name, and there is
    // at most one per stat, so the index is the whole key.
    void RemoveStatModifier(int statIndex) {
        for (size_t i = 0; i < m_StatModifiers.size(); ++i) {
            if (m_StatModifiers[i].statIndex != statIndex) continue;
            m_StatModifiers.erase(m_StatModifiers.begin() + static_cast<long>(i));
            return;
        }
    }

    // FUN_10048140 (`spells\RemoveEnchantment.s`): strip every modifier
    // whose delta is negative -- and then, separately, every modifier of
    // kind 3, which this port has no equivalent of (every shipped call site
    // of FUN_1004aa28 passes kind 1). So: remove the debuffs, keep the
    // buffs. The real function also sets flag bit 1 and zeroes both the
    // shared timer and `+0x70`, which the caller follows with
    // FUN_1004ba84(stats, 0, 0) -- the blindness clear.
    void RemoveNegativeStatModifiers() {
        for (size_t i = 0; i < m_StatModifiers.size();) {
            if (m_StatModifiers[i].delta < 0) {
                m_StatModifiers.erase(m_StatModifiers.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
        m_EffectTimer = 0;
        m_EffectFlags |= 2;
    }

    // The second periodic channel's arming half, in the real write order:
    //
    //   stats[0x7a] = 0;  stats[0x7c] = kind;
    //   stats[0x78] = (short)durationUnits;  stats[0x76] = 1;
    //
    // Two things are deliberate. The duration store is a **16-bit** one, so
    // `spells\Energize.s`'s `magnitude * 0xa00` wraps negative at magnitude
    // 13 and the effect simply never runs for a caster that high -- real,
    // and reproduced by the cast rather than hidden here. And that last
    // write lands on `+0x76`, the field poison and the burn already share
    // (see ApplyBurn): arming a regeneration while poisoned really does
    // drop the poison from 3 points a second to 1.
    void ArmPeriodic(int kind, int dotAmount, int durationUnits) {
        m_BurnAccumulator = 0;
        m_BurnKind = kind;
        m_BurnTimer = static_cast<int16_t>(durationUnits);
        m_DotKind = dotAmount;
    }
    int periodicKind() const { return m_BurnKind; }
    int periodicRemaining() const { return m_BurnTimer; }

    // FUN_1004bb54 / FUN_1004bb20 live on the owners (the player already had
    // both pools); these are the two the cast reads back.
    int effectFlags() const { return m_EffectFlags; }
    int dotAmount() const { return m_DotKind; }

    // Net modifier currently applied to a stat (0 when nothing is active).
    int statModifier(int statIndex) const {
        for (const StatModifier& m : m_StatModifiers) {
            if (m.statIndex == statIndex) return m.delta;
        }
        return 0;
    }

    bool blinded() const { return (m_EffectFlags & kEffectFlagBlind) != 0; }
    bool poisoned() const { return (m_EffectFlags & kEffectFlagPoison) != 0; }
    bool burning() const { return m_BurnTimer > 0 && m_BurnKind == kPeriodicBurn; }

    // The two periodic channels plus every expiry, one frame's worth.
    // `dealDamage(int)` stands in for the stats block's own DoDamage vtable
    // slot -- a template so each owner can route it through whatever guards
    // it has (a monster's SetInvulnerable, say) without a callback
    // allocation per tick.
    // M48: `regenerate(kind)` is the second callback FUN_10049780's kinds 6
    // and 7 need -- it adds the actor's *own level* (`stats+0x34`, the same
    // short every spell magnitude reads) to fatigue or to health, and only
    // the owner knows where those live. Called once per elapsed second, the
    // same accumulator shape the burn uses.
    template <class DealDamage, class Regenerate>
    void Tick(int deltaUnits, DealDamage&& dealDamage, Regenerate&& regenerate) {
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

        // M33: the shared effect timer (stats block +0x72). When it runs
        // out the flags clear -- blindness lifts, poison stops.
        if (m_EffectTimer > 0) {
            m_EffectTimer -= deltaUnits;
            if (m_EffectTimer <= 0) {
                m_EffectTimer = 0;
                // M34 correction. The real expiry is not a clear-to-zero:
                //
                //   flags  = 2;   // an assignment, not an &=
                //   dotKind = 3;
                //
                // Bit 1 is left set (its meaning is not decoded -- nothing
                // in this port reads any bit but blind's 4 and poison's 8,
                // for which `= 2` and `= 0` are identical), and dotKind is
                // *reset to poison's 3* rather than cleared. That second
                // write used to be unobservable, because channel 1 stops
                // ticking the moment its own timer hits zero. It stops
                // being unobservable now that the burn channel reads the
                // same field: a poison wearing off while a creature is on
                // fire really does triple its burn from 1/sec to 3/sec.
                // Faithful, and the reason this is written the odd way.
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
                    dealDamage(m_DotKind);
                }
            }
        }

        // M34: the second periodic channel (+0x78/+0x7a/+0x7c). Same
        // one-second accumulator shape as channel 1, but selected by its
        // own `kind`.
        //
        // M48 closes the two this could not: kinds 6 and 7 read the stats
        // block's `+0x34`, which is the actor's own **level** (the same
        // short every spell magnitude comes from, not a separate "spell
        // power"), and their arming sites are `spells\Energize.s` and
        // `spells\AzraSustenance.s` in the real cast. Kind 4 is Sanctuary's
        // and genuinely does nothing here -- it is a bare duration.
        //
        // Note kind 8 writes health *directly* instead of going through
        // DoDamage the way poison does, so a burn is not reduced by armour
        // or resistance at all -- it is the flat 1/sec it says it is. Both
        // owners route it through their own damage entry point anyway, so
        // that a real SetInvulnerable(true) quest NPC still cannot be
        // burned to death. Kind 6's regeneration, by contrast, has **no
        // clamp of any kind** in the real function -- fatigue really can
        // run past its own maximum while Energize is up. Reproduced.
        if (m_BurnTimer > 0) {
            if (m_BurnKind == kPeriodicBurn) {
                m_BurnAccumulator += deltaUnits;
                if (m_BurnAccumulator >= 256) {
                    m_BurnAccumulator = 0;
                    dealDamage(m_DotKind);
                }
            } else if (m_BurnKind == kPeriodicFatigueRegen || m_BurnKind == kPeriodicHealthRegen) {
                m_BurnAccumulator += deltaUnits;
                if (m_BurnAccumulator >= 256) {
                    m_BurnAccumulator = 0;
                    regenerate(m_BurnKind);
                }
            }
            m_BurnTimer -= deltaUnits;
            if (m_BurnTimer <= 0) {
                m_BurnTimer = 0;
                m_BurnKind = kPeriodicNone;
            }
        }
    }

    // The pre-M48 two-argument form, kept so the callers that have no
    // regeneration to route (tests, and anything without health/fatigue of
    // its own) do not have to pass an empty lambda.
    template <class DealDamage>
    void Tick(int deltaUnits, DealDamage&& dealDamage) {
        Tick(deltaUnits, dealDamage, [](int) {});
    }

private:
    // At most one modifier per stat -- the real FUN_1004aa28 replaces a
    // weaker one and rejects a weaker new one, so a list is never needed.
    struct StatModifier {
        int statIndex = 0;
        int delta = 0;
        int remaining = 0;  // engine delta units
    };
    std::vector<StatModifier> m_StatModifiers;
    int m_EffectFlags = 0;     // +0x44
    int m_EffectTimer = 0;     // +0x72, `duration << 8`
    int m_DotKind = 0;         // +0x76 -- 3 = poison, 1 = burn (shared, see ApplyBurn)
    int m_DotAccumulator = 0;  // +0x74
    int m_BurnTimer = 0;        // +0x78, `duration << 8`
    int m_BurnAccumulator = 0;  // +0x7a
    int m_BurnKind = kPeriodicNone;  // +0x7c
    int m_ParalysisTimer = 0;   // a monster's own +0x294
};

}  // namespace sk_bindings
