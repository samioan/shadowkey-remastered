#include "audio/audio_engine.h"

#include <windows.h>

#include <xaudio2.h>

#include <cstdio>
#include <vector>

namespace sk {

struct AudioEngine::Impl {
    bool comInitialized = false;
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* mastering = nullptr;
    IXAudio2SourceVoice* musicVoice = nullptr;
    std::vector<IXAudio2SourceVoice*> sfxVoices;
    // Real Options-screen slider values, 0-100 (see the header). Both
    // start at full so audio behaves exactly as before until the player
    // actually moves a slider.
    int sfxVolumePercent = 100;
    int musicVolumePercent = 100;

    IXAudio2SourceVoice* CreateVoiceFor(const Sound& sound) {
        WAVEFORMATEX fmt = {};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = static_cast<WORD>(sound.channels);
        fmt.nSamplesPerSec = static_cast<DWORD>(sound.sampleRate);
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = static_cast<WORD>(fmt.nChannels * fmt.wBitsPerSample / 8);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        IXAudio2SourceVoice* voice = nullptr;
        if (FAILED(engine->CreateSourceVoice(&voice, &fmt))) return nullptr;
        return voice;
    }
};

AudioEngine::AudioEngine() : impl_(new Impl()) {}

AudioEngine::~AudioEngine() {
    StopMusic();
    for (IXAudio2SourceVoice* v : impl_->sfxVoices) v->DestroyVoice();
    if (impl_->mastering) impl_->mastering->DestroyVoice();
    if (impl_->engine) impl_->engine->Release();
    if (impl_->comInitialized) CoUninitialize();
    delete impl_;
}

bool AudioEngine::Init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // S_OK/S_FALSE: we hold a real COM reference, must CoUninitialize() to
    // balance it. RPC_E_CHANGED_MODE: COM was already initialized on this
    // thread under a different concurrency model by someone else -- we
    // hold no reference, must NOT call CoUninitialize() ourselves.
    impl_->comInitialized = (hr == S_OK || hr == S_FALSE);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        std::printf("AudioEngine: CoInitializeEx failed (0x%08lx)\n", static_cast<unsigned long>(hr));
        return false;
    }
    if (FAILED(XAudio2Create(&impl_->engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        std::printf("AudioEngine: XAudio2Create failed -- no audio device?\n");
        return false;
    }
    if (FAILED(impl_->engine->CreateMasteringVoice(&impl_->mastering))) {
        std::printf("AudioEngine: CreateMasteringVoice failed\n");
        return false;
    }
    return true;
}

void AudioEngine::PlaySfx(const Sound& sound, float volume01) {
    if (!impl_->engine || sound.empty()) return;
    IXAudio2SourceVoice* voice = impl_->CreateVoiceFor(sound);
    if (!voice) return;
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = static_cast<UINT32>(sound.samples.size() * sizeof(int16_t));
    buf.pAudioData = reinterpret_cast<const BYTE*>(sound.samples.data());
    buf.Flags = XAUDIO2_END_OF_STREAM;
    voice->SubmitSourceBuffer(&buf);
    // Scaled by the real Options screen's SoundFXSlider value.
    voice->SetVolume(volume01 * static_cast<float>(impl_->sfxVolumePercent) / 100.0f);
    voice->Start();
    impl_->sfxVoices.push_back(voice);
}

void AudioEngine::PlayMusic(const Sound& sound, float volume01) {
    if (!impl_->engine || sound.empty()) return;
    StopMusic();
    IXAudio2SourceVoice* voice = impl_->CreateVoiceFor(sound);
    if (!voice) return;
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = static_cast<UINT32>(sound.samples.size() * sizeof(int16_t));
    buf.pAudioData = reinterpret_cast<const BYTE*>(sound.samples.data());
    buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    voice->SubmitSourceBuffer(&buf);
    // Scaled by the real Options screen's MusicSlider value.
    voice->SetVolume(volume01 * static_cast<float>(impl_->musicVolumePercent) / 100.0f);
    voice->Start();
    impl_->musicVoice = voice;
}

void AudioEngine::SetSfxVolumePercent(int percent) {
    impl_->sfxVolumePercent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
}

void AudioEngine::SetMusicVolumePercent(int percent) {
    impl_->musicVolumePercent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    // Re-gain an already-looping track so a music slider is audible while
    // it's being dragged, not only on the next PlayMusic().
    if (impl_->musicVoice) {
        impl_->musicVoice->SetVolume(static_cast<float>(impl_->musicVolumePercent) / 100.0f);
    }
}

int AudioEngine::sfxVolumePercent() const { return impl_->sfxVolumePercent; }
int AudioEngine::musicVolumePercent() const { return impl_->musicVolumePercent; }

void AudioEngine::StopMusic() {
    if (!impl_->musicVoice) return;
    impl_->musicVoice->Stop();
    impl_->musicVoice->DestroyVoice();
    impl_->musicVoice = nullptr;
}

void AudioEngine::Update() {
    for (auto it = impl_->sfxVoices.begin(); it != impl_->sfxVoices.end();) {
        XAUDIO2_VOICE_STATE state;
        (*it)->GetState(&state);
        if (state.BuffersQueued == 0) {
            (*it)->DestroyVoice();
            it = impl_->sfxVoices.erase(it);
        } else {
            ++it;
        }
    }
}

}  // namespace sk
