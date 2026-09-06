#include "debug/debug_metrics.h"

#include <algorithm>
#include <chrono>

namespace sk_debug {

namespace {

long long NowMicros() {
    using namespace std::chrono;
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

Metrics& Metrics::Get() {
    static Metrics instance;
    return instance;
}

void Metrics::BeginFrame(double frameSeconds) {
    ++m_Frame;
    m_Elapsed += frameSeconds;
    m_FrameMs = frameSeconds * 1000.0;
    // A light exponential smoothing purely for display -- the raw per-frame
    // number on a 40ms fixed tick jitters by a millisecond or two and is
    // unreadable as a live figure.
    m_FrameMsAverage = m_FrameMsAverage <= 0.0 ? m_FrameMs
                                                : m_FrameMsAverage * 0.9 + m_FrameMs * 0.1;
    for (auto& entry : m_Counters) {
        entry.second.lastFrame = entry.second.thisFrame;
        entry.second.thisFrame = 0;
    }
}

double Metrics::framesPerSecond() const {
    return m_FrameMsAverage > 0.0 ? 1000.0 / m_FrameMsAverage : 0.0;
}

void Metrics::Count(const std::string& name, long long amount) {
    Counter& c = m_Counters[name];
    c.total += amount;
    c.thisFrame += amount;
}

long long Metrics::total(const std::string& name) const {
    auto it = m_Counters.find(name);
    return it == m_Counters.end() ? 0 : it->second.total;
}

long long Metrics::thisFrame(const std::string& name) const {
    auto it = m_Counters.find(name);
    return it == m_Counters.end() ? 0 : it->second.thisFrame;
}

long long Metrics::lastFrame(const std::string& name) const {
    auto it = m_Counters.find(name);
    return it == m_Counters.end() ? 0 : it->second.lastFrame;
}

std::vector<std::string> Metrics::counterNames() const {
    std::vector<std::string> names;
    names.reserve(m_Counters.size());
    for (const auto& entry : m_Counters) names.push_back(entry.first);
    return names;
}

void Metrics::ResetCounters() {
    m_Counters.clear();
    m_MarkFrame = m_Frame;
}

void Metrics::Mark() {
    for (auto& entry : m_Counters) entry.second.mark = entry.second.total;
    m_MarkFrame = m_Frame;
}

std::vector<CounterDelta> Metrics::Diff() const {
    std::vector<CounterDelta> out;
    for (const auto& entry : m_Counters) {
        if (entry.second.total == entry.second.mark) continue;
        out.push_back({entry.first, entry.second.mark, entry.second.total});
    }
    std::sort(out.begin(), out.end(), [](const CounterDelta& a, const CounterDelta& b) {
        if (a.delta() != b.delta()) return a.delta() > b.delta();
        return a.name < b.name;
    });
    return out;
}

void Metrics::Sample(const std::string& name, double milliseconds) {
    SampleSet& s = m_Samples[name];
    s.values[s.next] = milliseconds;
    s.next = (s.next + 1) % SampleSet::kWindow;
    if (s.count < SampleSet::kWindow) ++s.count;
}

double Metrics::averageMs(const std::string& name) const {
    auto it = m_Samples.find(name);
    if (it == m_Samples.end() || it->second.count == 0) return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < it->second.count; ++i) sum += it->second.values[i];
    return sum / static_cast<double>(it->second.count);
}

std::vector<std::string> Metrics::sampleNames() const {
    std::vector<std::string> names;
    names.reserve(m_Samples.size());
    for (const auto& entry : m_Samples) names.push_back(entry.first);
    return names;
}

void Metrics::Log(const std::string& category, std::string text) {
    LogEvent event;
    event.frame = m_Frame;
    event.seconds = m_Elapsed;
    event.category = category;
    event.text = std::move(text);
    m_Events.push_back(std::move(event));
    while (m_Events.size() > m_EventCapacity) m_Events.pop_front();
    ++m_Categories[category];
}

std::vector<LogEvent> Metrics::RecentEvents(size_t count, const std::string& categoryFilter) const {
    std::vector<LogEvent> out;
    for (auto it = m_Events.rbegin(); it != m_Events.rend() && out.size() < count; ++it) {
        if (!categoryFilter.empty() &&
            it->category.find(categoryFilter) == std::string::npos) {
            continue;
        }
        out.push_back(*it);
    }
    return out;
}

void Metrics::ClearEvents() {
    m_Events.clear();
}

void Metrics::SetEventCapacity(size_t capacity) {
    m_EventCapacity = capacity == 0 ? 1 : capacity;
    while (m_Events.size() > m_EventCapacity) m_Events.pop_front();
}

std::vector<std::pair<std::string, long long>> Metrics::categoryCounts() const {
    std::vector<std::pair<std::string, long long>> out(m_Categories.begin(), m_Categories.end());
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    return out;
}

ScopedTimer::ScopedTimer(std::string name) : m_Name(std::move(name)), m_Start(NowMicros()) {}

ScopedTimer::~ScopedTimer() {
    Metrics::Get().Sample(m_Name, static_cast<double>(NowMicros() - m_Start) / 1000.0);
}

}  // namespace sk_debug
