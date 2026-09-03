#pragma once

// Native binding for the object AddMenuSlider() returns.
//
// The real Options screen (options.s) is the corpus's only user, and it
// creates exactly two:
//
//     AddMenuSlider(4062, "SoundFXSlider", 100, 10);
//     AddMenuSlider(4063, "MusicSlider",   100, 10);
//
// i.e. (labelStringId, callbackName, maxValue, step). Neither callback is
// defined anywhere in the corpus -- like several other Options-screen
// natives, the real engine owns the behaviour and the name is just what it
// dispatches on -- so this port applies the value directly to
// audio/audio_engine.h's master gains and still fires the callback name
// through the usual TryInvoke path (a silent no-op for these two, but
// correct if a future script ever does define one).
//
// Both calls were soft-failing before this, which meant the two rows
// simply didn't exist: the real Options screen was missing its volume
// controls entirely, and its remaining rows shifted up.

#include <string>

#include "simkin_bindings/native_stub_executable.h"

namespace sk_bindings {

class SliderExecutable : public NativeStubExecutable {
public:
    SliderExecutable(std::string callback, int maxValue, int step)
        : NativeStubExecutable("MenuSlider"),
          m_Callback(std::move(callback)),
          m_Max(maxValue > 0 ? maxValue : 100),
          m_Step(step > 0 ? step : 1),
          m_Value(maxValue > 0 ? maxValue : 100) {}

    const std::string& callback() const { return m_Callback; }
    int value() const { return m_Value; }
    int maxValue() const { return m_Max; }

    // Left/Right on the row moves the value by one real step, clamped to
    // [0, max] (no wraparound -- a volume slider that jumps from silent to
    // full on one keypress would be a poor substitute for the real one).
    void Adjust(int direction) {
        m_Value += direction * m_Step;
        if (m_Value < 0) m_Value = 0;
        if (m_Value > m_Max) m_Value = m_Max;
    }

    void SetValue(int value) {
        m_Value = value < 0 ? 0 : (value > m_Max ? m_Max : value);
    }

    bool method(const skString& methodName, skRValueArray& args, skRValue& returnValue,
                skExecutableContext& context) override;

private:
    std::string m_Callback;
    int m_Max;
    int m_Step;
    int m_Value;
};

}  // namespace sk_bindings
