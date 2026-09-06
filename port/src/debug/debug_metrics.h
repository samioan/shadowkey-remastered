#pragma once

// M68: the instrumentation sink behind "give me detailed stats on what is
// running each time I do something".
//
// Three things, all cheap enough to leave permanently on:
//
//   * **Named counters.** Every counter carries a running total, this
//     frame's value and the previous completed frame's value. `BeginFrame`
//     rolls them. That gives both "how many times has this ever happened"
//     and "how many times did it happen in the frame I am looking at".
//
//   * **Mark / diff.** `Mark()` snapshots every counter; `Diff()` reports
//     only the ones that moved since. This is the actual bug-hunting loop:
//     mark, press the key / run the command, diff -- and you get exactly
//     the set of things that fired, with no reading of a log.
//
//   * **An event ring.** A bounded deque of (frame, seconds, category,
//     text). Bounded on purpose: an unbounded trace of a 25Hz game loop
//     with script tracing on will eat memory and, worse, scroll the one
//     line you needed off the top.
//
// Timing uses `Sample()`, which keeps a small windowed average per name so
// a per-frame millisecond figure does not flicker unreadably.
//
// Deliberately a plain process-global (`Metrics::Get()`): the emitters are
// scattered across libraries that must not depend on each other (a
// soft-fail observer inside `sk_bindings`, a script tracer wrapping the
// vendored interpreter, the host adapter in main.cpp), and threading a
// context pointer to all of them would be a far more invasive change to the
// game than a global that nothing reads unless the debug suite is built.

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace sk_debug {

struct LogEvent {
    uint64_t frame = 0;
    double seconds = 0.0;
    std::string category;
    std::string text;
};

struct CounterDelta {
    std::string name;
    long long before = 0;
    long long after = 0;
    long long delta() const { return after - before; }
};

class Metrics {
public:
    static Metrics& Get();

    // ---- frame bookkeeping ----------------------------------------

    // Rolls per-frame counters into "last frame" and zeroes them.
    // `frameSeconds` is the wall-clock length of the frame that just ended.
    void BeginFrame(double frameSeconds);
    uint64_t frame() const { return m_Frame; }
    double elapsedSeconds() const { return m_Elapsed; }
    double frameMs() const { return m_FrameMs; }
    double framesPerSecond() const;

    // ---- counters --------------------------------------------------

    void Count(const std::string& name, long long amount = 1);
    long long total(const std::string& name) const;
    long long thisFrame(const std::string& name) const;
    long long lastFrame(const std::string& name) const;
    std::vector<std::string> counterNames() const;
    void ResetCounters();

    // ---- mark / diff -----------------------------------------------

    void Mark();
    uint64_t markFrame() const { return m_MarkFrame; }
    // Every counter whose total moved since the last Mark(), largest delta
    // first. A counter that appeared after the mark reports before = 0.
    std::vector<CounterDelta> Diff() const;

    // ---- timing samples ---------------------------------------------

    // A windowed mean over the last `kSampleWindow` submissions.
    void Sample(const std::string& name, double milliseconds);
    double averageMs(const std::string& name) const;
    std::vector<std::string> sampleNames() const;

    // ---- event ring --------------------------------------------------

    void Log(const std::string& category, std::string text);
    const std::deque<LogEvent>& events() const { return m_Events; }
    // Newest first, at most `count`, optionally filtered by category
    // substring (empty = everything).
    std::vector<LogEvent> RecentEvents(size_t count, const std::string& categoryFilter) const;
    void ClearEvents();
    void SetEventCapacity(size_t capacity);
    size_t eventCapacity() const { return m_EventCapacity; }

    // Categories seen so far, with how many events each has produced.
    std::vector<std::pair<std::string, long long>> categoryCounts() const;

private:
    Metrics() = default;

    struct Counter {
        long long total = 0;
        long long thisFrame = 0;
        long long lastFrame = 0;
        long long mark = 0;
    };
    struct SampleSet {
        static constexpr size_t kWindow = 32;
        double values[kWindow] = {};
        size_t count = 0;
        size_t next = 0;
    };

    std::map<std::string, Counter> m_Counters;
    std::map<std::string, SampleSet> m_Samples;
    std::map<std::string, long long> m_Categories;
    std::deque<LogEvent> m_Events;
    size_t m_EventCapacity = 512;
    uint64_t m_Frame = 0;
    uint64_t m_MarkFrame = 0;
    double m_Elapsed = 0.0;
    double m_FrameMs = 0.0;
    double m_FrameMsAverage = 0.0;
};

// Shorthand for the call sites, which are scattered and want to stay short.
inline void Count(const std::string& name, long long amount = 1) {
    Metrics::Get().Count(name, amount);
}
inline void Log(const std::string& category, std::string text) {
    Metrics::Get().Log(category, std::move(text));
}

// RAII millisecond timer feeding Metrics::Sample().
class ScopedTimer {
public:
    explicit ScopedTimer(std::string name);
    ~ScopedTimer();
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string m_Name;
    long long m_Start;
};

}  // namespace sk_debug
