#pragma once

// M51: the engine's own sound mixer, recovered.
//
// M27 decoded the audio *data* (plain PCM `.wav`, plain Ogg Vorbis, and
// the per-zone `<zone>_sounds.txt` slot manifests) and the *script* call
// surface, and left five things open: the native-only player-action
// sounds, the main-menu music trigger, `steamsound.s`'s four-argument
// `PlaySound`, positional audio, and the volume/pan mixing formula. All
// five were one system, and this file is the arithmetic half of it.
//
// The sound manager is `app->+0x3a8`. Its shape:
//
//     +0x048 + slot*4   256 sound objects, one per manifest slot
//     +0x448            the 16-bit output buffer
//     +0x648            the 32-bit mixing accumulator
//     +0xa54  bit 0     "the command queue below has work"
//     +0xa58 + i*4      8 pending slot ids
//     +0xa78 + i*4        their volumes
//     +0xa98 + i*4        their repeat counts
//     +0xab8            master **music** volume
//     +0xabc            master **SFX** volume
//     +0xac0            which slot counts as "the music"
//     +0xac4            mute-everything flag
//     +0xac5 / +0xac6   music fading out / fading in
//     +0xac8            the volume to fade back up to
//
// and a sound object is `{u8 volume; u8 repeats; u8 flags; ... }` with
// flags bit 0 = "playing".
//
// The per-buffer tick is `FUN_10008b34(mgr, 256)`: it advances the music
// fade, zeroes the accumulator, walks all 256 slots mixing every playing
// one through `FUN_100080a0`, and clamps down to 16 bits. Two structural
// facts fall out of it, and both are reproduced here rather than in the
// XAudio2 backend, because they are engine behaviour and not platform
// behaviour:
//
//   * **At most eight voices sound at once.** The mix loop breaks after
//     the eighth playing slot, in slot order -- so the cap is not
//     "oldest wins" or "loudest wins", it is "lowest manifest slot
//     number wins".
//   * **Every voice is halved before summing** (`sample >> 1`), which is
//     the headroom that makes eight voices fit in 16 bits.
//
// There is **no panning anywhere** -- one accumulator, one output
// channel. The "positional audio" the roadmap asked about turns out to
// be a volume model only, below.

#include <cstdint>

namespace sk {

// ---------------------------------------------------------------------
// The volume curve -- FUN_10009324, a 101-entry i16 table at 0x100a5e9e
// indexed by a 0..100 volume, saturating at index 100.
//
// It is *almost* `round(v * 2.56)` but not quite (index 13 is 35 where
// the formula gives 33), so it is transcribed verbatim rather than
// recomputed.
// ---------------------------------------------------------------------
extern const int16_t kVolumeCurve[101];
constexpr int kVolumeCurveMax = 256;  // curve[99] and curve[100]

// Clamped table lookup: anything above 100 reads the last entry, which
// is what the real function's `if (v < 0x65)` else-branch does.
int VolumeCurve(int volume0to100);

// ---------------------------------------------------------------------
// The per-voice gain -- FUN_100080a0.
//
//     a = curve[masterVolume];  b = curve[voiceVolume];
//     accumulator += ((a * b >> 8) * (sample >> 1)) >> 8;
//
// so the linear amplitude a voice contributes is
// `((a*b >> 8) / 256) * (1/2)`. Returned as 0..1 for a backend that
// applies gain per voice instead of mixing by hand.
//
// Note the halving: a single voice at full volume on both knobs plays at
// **half** amplitude, not full.
// ---------------------------------------------------------------------
float VoiceGain(int masterVolume0to100, int voiceVolume0to100);

// The mix loop stops after this many playing slots.
constexpr int kMaxSimultaneousVoices = 8;

// ---------------------------------------------------------------------
// Positional volume -- FUN_10027980, and its per-entity twin
// FUN_10064e00.
//
// `dx`/`dy` are (listener - source) in the engine's 8.8 world units, 256
// per tile. Both functions compute the same thing:
//
//     d2 = (dx*dx >> 8) + (dy*dy >> 8);
//     if      ((d2 >> 8) == 0)      out = volume;   // inside one tile
//     else if ((d2 >> 8) >= range)  out = 0;
//     else out = ((volume << 16) / (range << 8)) * ((range << 8) - d2) >> 16;
//
// i.e. **linear in squared distance**, full volume within one tile,
// silent at `range` tiles. The one-shot path hardcodes range = 512 (its
// literal is the pre-shifted 0x20000); an entity-attached looping
// ambient carries its own range instead.
//
// 512 is a large number of tiles -- at 22.6 tiles out a sound is already
// at zero, because the falloff is in *squared* distance, so the audible
// radius is `sqrt(range)` tiles, not `range`.
// ---------------------------------------------------------------------
constexpr int kDefaultSoundRange = 512;
int PositionalVolume(int volume0to100, int dx, int dy, int range = kDefaultSoundRange);

// ---------------------------------------------------------------------
// Directional volume -- the other half of FUN_10027980, applied only
// when a caller asks for it (the third `PlaySound` argument).
//
// It compares the bearing from the source to the listener against the
// **source's own heading**, so it models an emitter that faces a
// direction, not a listener that has two ears:
//
//     diff = shortest signed angle between the two, |diff| >> 8,
//            clamped to 32
//     volume -= diff * ((volume >> 3) / 32)
//
// The most it can ever take off is `volume >> 3`, i.e. 12.5% -- a nudge,
// not a directional cutoff.
//
// **And in the shipped game it takes off nothing at all.** The step is
// an integer divide: `(volume >> 3) / 32` is zero for every volume below
// 256, and a volume is 0..100 (the script range, and the literal 100
// every native call passes). So the entire directional branch subtracts
// zero. It is reproduced exactly rather than dropped -- the arithmetic
// is what makes it dead, and a reader deserves to see why -- but nothing
// in the shipped data can hear it.
//
// `bearing` and `emitterYaw` are engine angles (0x10000 to the turn).
// The real bearing comes from FUN_1001c20c, a normalised-vector lookup
// quantised to a 32x32 grid; a caller here can use a real atan2, since
// the term it feeds is zero either way.
// ---------------------------------------------------------------------
int DirectionalVolume(int volume0to100, int bearing, int emitterYaw);

// ---------------------------------------------------------------------
// The music fade -- FUN_100090bc (FadeMusic), FUN_1000915c
// (UnFadeMusic), and the step the per-buffer tick applies.
//
// The step is 5 per 256-sample buffer. The shipped sounds are 8 kHz, so
// a buffer is 32 ms and a full 100 -> 0 fade takes 20 buffers, about
// **0.64 seconds**.
// ---------------------------------------------------------------------
constexpr int kMusicFadeStep = 5;
constexpr int kMixBufferSamples = 256;
constexpr int kSampleRateHz = 8000;

class MusicFade {
public:
    // FadeMusic(): remembers the current volume as the restore target
    // (only if it is nonzero -- fading out from silence stores nothing,
    // which is what makes a later UnFadeMusic a no-op), cancels an
    // in-flight fade-in, and starts fading out.
    void Fade(int currentVolume);
    // UnFadeMusic(): only does anything once a target has been stored.
    void UnFade();
    // One mixing buffer's worth of fade. Returns the new volume.
    int Step(int currentVolume);

    bool fadingOut() const { return m_FadingOut; }
    bool fadingIn() const { return m_FadingIn; }
    int restoreTarget() const { return m_Target; }
    void Reset() { *this = MusicFade{}; }

private:
    bool m_FadingOut = false;
    bool m_FadingIn = false;
    int m_Target = -1;  // +0xac8, -1 = nothing to restore
};

// ---------------------------------------------------------------------
// The native sound slots. Every one of these is a literal in the
// decompiled engine, and every zone's manifest maps them identically
// (checked across azra / crypt1 / broken1 / snowline).
// ---------------------------------------------------------------------
constexpr int kSoundBowFire = 1;         // barch_firebow.wav
constexpr int kSoundChestOpen = 0x3b;    // 59, chest_open.wav
constexpr int kSoundDoorOpen = 0x3f;     // 63, door_open.wav
constexpr int kSoundAttackHit = 0x50;    // 80, pl_attack_impale.wav
constexpr int kSoundAttackMiss = 0x51;   // 81, pl_attack_sword.wav
constexpr int kSoundPowerUp = 0x57;      // 87, pl_cast_powerup.wav
constexpr int kSoundJumpFemale = 0x5b;   // 91, pl_jump_female.wav
constexpr int kSoundJumpMale = 0x5c;     // 92, pl_jump_male.wav
constexpr int kSoundMenuMove = 99;       // menuNEWxbx.wav
constexpr int kSoundMenuSelect = 100;    // menu1.wav
// menu_sounds.txt's own slot 70, started by FUN_1002707c when the game
// returns to the front end.
constexpr int kSoundMenuMusic = 0x46;    // 70, battle3.ogg
constexpr const char* kMenuSoundCategory = "menu";

// M73: the jump's fatigue cost used to live here, because the jump also
// picks between the two sounds above. It is one of four action costs now,
// so it moved to simkin_bindings/vitals.h with the rest of them.

// The default third and fourth `PlaySound` arguments, from the
// dispatcher's own skRValue defaults: volume 100, no directional
// attenuation, play once.
constexpr int kDefaultSoundVolume = 100;
constexpr bool kDefaultSoundDirectional = false;
constexpr int kDefaultSoundRepeats = 1;
// What the engine passes for a sound that should not stop: the repeat
// count is a byte, so this is simply its maximum.
constexpr int kRepeatForever = 255;

}  // namespace sk
