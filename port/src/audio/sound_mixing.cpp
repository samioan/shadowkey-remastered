#include "audio/sound_mixing.h"

namespace sk {

// FUN_10009324's table, read straight out of the image at 0x100a5e9e.
const int16_t kVolumeCurve[101] = {
    0,   2,   5,   7,   10,  12,  15,  17,  20,  23,  25,  28,  30,  35,  38,  40,  43,
    46,  48,  51,  53,  56,  58,  61,  64,  66,  69,  71,  74,  76,  79,  81,  84,  87,
    89,  92,  94,  97,  99,  102, 104, 107, 110, 112, 115, 117, 120, 122, 125, 128, 130,
    133, 135, 138, 140, 143, 145, 148, 151, 153, 156, 158, 161, 163, 166, 168, 171, 174,
    176, 179, 181, 184, 186, 189, 192, 194, 197, 199, 202, 204, 207, 209, 212, 215, 217,
    220, 222, 225, 227, 230, 232, 235, 238, 240, 243, 245, 248, 250, 253, 256, 256,
};

int VolumeCurve(int volume0to100) {
    // The real function is `if (v < 0x65) table[v]; else table[100];` --
    // note it has no lower guard at all, so a negative volume would index
    // behind the table. Nothing in the engine can produce one (every
    // producer clamps first), and this port refuses rather than reading
    // out of bounds.
    if (volume0to100 < 0) return 0;
    if (volume0to100 > 100) return kVolumeCurve[100];
    return kVolumeCurve[volume0to100];
}

float VoiceGain(int masterVolume0to100, int voiceVolume0to100) {
    // FUN_100080a0's early-out: a zero on either knob skips the mix
    // entirely (it still advances the voice's read cursor, so a silenced
    // sound still finishes on schedule rather than pausing).
    if (masterVolume0to100 == 0 || voiceVolume0to100 == 0) return 0.0f;
    const int a = VolumeCurve(masterVolume0to100);
    const int b = VolumeCurve(voiceVolume0to100);
    const int gain = (a * b) >> 8;  // 0..256
    // ...applied as `(gain * (sample >> 1)) >> 8`, so the amplitude is
    // gain/256 of a *halved* sample.
    return static_cast<float>(gain) / 256.0f * 0.5f;
}

int PositionalVolume(int volume0to100, int dx, int dy, int range) {
    const int d2 = ((dx * dx) >> 8) + ((dy * dy) >> 8);
    const int d = d2 >> 8;
    if (d == 0) return volume0to100;
    if (d >= range) return 0;
    const int scaled = range << 8;
    return ((volume0to100 << 16) / scaled) * (scaled - d2) >> 16;
}

int DirectionalVolume(int volume0to100, int bearing, int emitterYaw) {
    const int a = static_cast<int16_t>(bearing);
    const int b = static_cast<int16_t>(emitterYaw);
    int diff;
    if (a > b) {
        diff = a - b;
        if (diff > 0x8000) diff = b - (a + 1);
    } else {
        diff = b - a;
        if (diff > 0x8000) diff = a - (b + 1);
    }
    diff >>= 8;
    if (diff < 0) diff = -diff;
    if (diff >= 0x20) diff = 0x20;
    // The step is computed from the volume, so the whole term is bounded
    // by `volume >> 3` -- at most an eighth off, when the emitter is
    // pointing directly away.
    return volume0to100 - diff * ((volume0to100 >> 3) / 32);
}

void MusicFade::Fade(int currentVolume) {
    if (m_FadingOut) return;  // already on the way down
    if (m_FadingIn) {
        m_FadingIn = false;
    } else if (currentVolume != 0) {
        // Only a nonzero volume is worth remembering. Fading out from
        // silence stores nothing, which is exactly why a later UnFade
        // does nothing -- reproduced, not tidied.
        m_Target = currentVolume;
    }
    m_FadingOut = true;
}

void MusicFade::UnFade() {
    if (m_Target == -1) return;
    m_FadingOut = false;
    m_FadingIn = true;
}

int MusicFade::Step(int currentVolume) {
    if (m_FadingOut) {
        currentVolume = currentVolume < kMusicFadeStep + 1 ? 0 : currentVolume - kMusicFadeStep;
        if (currentVolume == 0) m_FadingOut = false;
    }
    if (m_FadingIn) {
        currentVolume += kMusicFadeStep;
        if (currentVolume >= m_Target) {
            m_FadingIn = false;
            currentVolume = m_Target;
            m_Target = -1;
        }
    }
    return currentVolume;
}

}  // namespace sk
