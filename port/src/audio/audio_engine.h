#pragma once

// M27: real audio playback backend -- XAudio2 (bundled with Windows,
// no redistributable needed), one mastering voice, a fresh transient
// source voice per one-shot SFX (auto-reaped once finished, Update()),
// and a single persistent looping source voice for ambient/battle
// music (PlayMusic() replaces whatever was playing, matching every real
// zone-root script's own single Level.PlayAmbient(id, volume) call in
// Init(), docs/PORT_ROADMAP.md's M27 entry). XAudio2/COM types are kept
// out of this header (pimpl) so including it doesn't drag <windows.h>/
// <xaudio2.h> into every translation unit that needs an AudioEngine&.

#include "audio/sound.h"
#include "audio/sound_mixing.h"

namespace sk {

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Returns false (logged) if XAudio2/COM init fails -- every other
    // call below becomes a silent no-op rather than crashing, same
    // "optional subsystem" tolerance every other asset loader in this
    // port already has (missing sprite/model/sound files).
    bool Init();

    // Fire-and-forget one-shot playback -- `sound`'s underlying sample
    // buffer must outlive playback. Safe in practice: every real Sound
    // this port plays comes from assets/sound_archive.h's own decode
    // cache, which holds it for the whole game session.
    //
    // M51: `volume` and `repeats` are the engine's own units -- a 0..100
    // volume put through the real curve (audio/sound_mixing.h), and a
    // repeat *count*, not a loop flag. Both come straight off the real
    // `PlaySound(id, volume, directional, repeats)` dispatcher, whose
    // own skRValue defaults are the ones used here.
    //
    // The eight-voice cap is the engine's, not XAudio2's: the real mix
    // loop stops after the eighth playing slot, so a ninth concurrent
    // sound is simply not heard. Reproduced (oldest-first here, since
    // this port has no manifest-slot ordering to break the tie with --
    // see sound_mixing.h).
    void PlaySfx(const Sound& sound, int volume = kDefaultSoundVolume,
                 int repeats = kDefaultSoundRepeats);

    // Starts `sound` repeating, replacing whatever was playing.
    // `repeats` is the same count; the engine's own music path passes
    // 255, the largest a byte holds.
    void PlayMusic(const Sound& sound, int volume = kDefaultSoundVolume,
                   int repeats = kRepeatForever);
    void StopMusic();

    // M51: FadeMusic() / UnFadeMusic(), the two real GameEngine bindings
    // (indices 11 and 12, four real call sites between them -- around the
    // multiplayer/bluetooth menus and mainmenu.s's own return path).
    //
    // The real fade runs in the audio tick, 5 volume units per
    // 256-sample buffer; at the shipped 8 kHz that is 32 ms a step, so a
    // full 100 -> 0 fade is about 0.64 s. TickMusicFade() advances it by
    // real elapsed time instead, since this port does not own the mixing
    // buffer -- call it once per frame with the frame's own delta.
    void FadeMusic();
    void UnFadeMusic();
    void TickMusicFade(int deltaMs);
    bool musicFading() const;

    // Reaps one-shot voices that finished playing -- call once per game
    // tick (main.cpp) so PlaySfx() doesn't leak a voice per call.
    void Update();

    // Master gains, driven by the real Options screen's own two
    // `AddMenuSlider(4062,"SoundFXSlider",100,10)` /
    // `AddMenuSlider(4063,"MusicSlider",100,10)` rows (options.s).
    //
    // M51 confirmed these against the binary: the native slider handler
    // (FUN_1007f8f8) compares the slider's own name against exactly those
    // two wide strings and writes soundMgr+0xabc for "SoundFXSlider" and
    // soundMgr+0xab8 for "MusicSlider" -- the same two fields the mixer
    // then picks between per voice, by whether the slot is the designated
    // music slot (+0xac0). So the split really is per-sound, not per-bus.
    // (The native handler clamps to 0..256 while the slider only offers
    // 0..100 in steps of 10; the extra headroom is unreachable.)
    void SetSfxVolumePercent(int percent);
    void SetMusicVolumePercent(int percent);
    int sfxVolumePercent() const;
    int musicVolumePercent() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace sk
