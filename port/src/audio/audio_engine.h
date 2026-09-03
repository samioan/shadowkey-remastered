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
    void PlaySfx(const Sound& sound, float volume01 = 1.0f);

    // Starts `sound` looping forever, replacing whatever was playing.
    void PlayMusic(const Sound& sound, float volume01 = 1.0f);
    void StopMusic();

    // Reaps one-shot voices that finished playing -- call once per game
    // tick (main.cpp) so PlaySfx() doesn't leak a voice per call.
    void Update();

    // Master gains, driven by the real Options screen's own two
    // `AddMenuSlider(4062,"SoundFXSlider",100,10)` /
    // `AddMenuSlider(4063,"MusicSlider",100,10)` rows (options.s). Both
    // are 0-100 in the script's own units; every PlaySfx/PlayMusic
    // `volume01` is scaled by the matching one. Music takes effect on the
    // next PlayMusic() -- there's no live re-gain of an already-playing
    // voice, which is enough for a settings screen that is only reachable
    // between (or paused out of) play.
    void SetSfxVolumePercent(int percent);
    void SetMusicVolumePercent(int percent);
    int sfxVolumePercent() const;
    int musicVolumePercent() const;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace sk
