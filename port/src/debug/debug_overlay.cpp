#include "debug/debug_overlay.h"

#include <algorithm>
#include <cstdio>

#include "debug/debug_console.h"
#include "debug/debug_metrics.h"

namespace sk_debug {

namespace {

constexpr sk::OverlayColor kPanelColor{10, 12, 20};
constexpr sk::OverlayColor kTitleColor{140, 180, 255};
constexpr sk::OverlayColor kNameColor{150, 155, 170};
constexpr sk::OverlayColor kValueColor{225, 228, 235};
constexpr sk::OverlayColor kBarColor{90, 110, 150};

std::string TwoDecimals(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

}  // namespace

std::vector<std::string> DebugOverlay::BuiltInPages() {
    return {"metrics", "events", "watch", "binds"};
}

void DebugOverlay::CyclePage(const DebugHost& host, int direction) {
    std::vector<std::string> pages;
    pages.push_back(std::string());  // off
    for (const std::string& page : host.Pages()) pages.push_back(page);
    for (const std::string& page : BuiltInPages()) pages.push_back(page);

    auto it = std::find(pages.begin(), pages.end(), m_Page);
    int index = it == pages.end() ? 0 : static_cast<int>(it - pages.begin());
    const int count = static_cast<int>(pages.size());
    index = ((index + direction) % count + count) % count;
    m_Page = pages[static_cast<size_t>(index)];
}

bool DebugOverlay::RemoveWatch(size_t index) {
    if (index >= m_Watches.size()) return false;
    m_Watches.erase(m_Watches.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

// ------------------------------------------------------------- built-in pages

std::vector<StatGroup> DebugOverlay::BuildMetricsPage() const {
    const Metrics& metrics = Metrics::Get();
    std::vector<StatGroup> groups;

    StatGroup frame;
    frame.title = "frame";
    frame.Add("frame", std::to_string(metrics.frame()));
    frame.Add("fps", TwoDecimals(metrics.framesPerSecond()));
    frame.Add("frame ms", TwoDecimals(metrics.frameMs()));
    frame.Add("uptime s", TwoDecimals(metrics.elapsedSeconds()));
    for (const std::string& name : metrics.sampleNames()) {
        frame.Add(name + " ms", TwoDecimals(metrics.averageMs(name)));
    }
    groups.push_back(std::move(frame));

    // Counters that moved in the last completed frame first, then the rest
    // by total -- so a panel with 60 counters still shows the live ones at
    // the top where they are readable.
    std::vector<std::string> names = metrics.counterNames();
    std::stable_sort(names.begin(), names.end(),
                     [&metrics](const std::string& a, const std::string& b) {
                         const long long fa = metrics.lastFrame(a);
                         const long long fb = metrics.lastFrame(b);
                         if ((fa > 0) != (fb > 0)) return fa > 0;
                         return metrics.total(a) > metrics.total(b);
                     });
    StatGroup counters;
    counters.title = "counters  (this frame / total)";
    for (size_t i = 0; i < names.size() && i < 24; ++i) {
        const long long perFrame = metrics.lastFrame(names[i]);
        counters.Add(names[i], (perFrame ? std::to_string(perFrame) : std::string("-")) + " / " +
                                    std::to_string(metrics.total(names[i])));
    }
    if (names.size() > 24) {
        counters.Add("...", std::to_string(names.size() - 24) + " more (see `metrics`)");
    }
    groups.push_back(std::move(counters));
    return groups;
}

std::vector<StatGroup> DebugOverlay::BuildEventsPage() const {
    StatGroup group;
    group.title = m_EventFilter.empty() ? "events" : "events  [" + m_EventFilter + "]";
    for (const LogEvent& event : Metrics::Get().RecentEvents(26, m_EventFilter)) {
        group.Add("f" + std::to_string(event.frame) + " " + event.category, event.text);
    }
    if (group.rows.empty()) group.Add("(none)", "");
    return {std::move(group)};
}

std::vector<StatGroup> DebugOverlay::BuildBindsPage(const DebugConsole& console) const {
    StatGroup group;
    group.title = "key bindings";
    for (const auto& entry : console.bindings()) group.Add(entry.first, entry.second);
    if (group.rows.empty()) group.Add("(none)", "bind <key> \"<command>\"");
    return {std::move(group)};
}

std::vector<StatGroup> DebugOverlay::BuildWatchPage(DebugConsole& console) const {
    StatGroup group;
    group.title = "watches";
    for (size_t i = 0; i < m_Watches.size(); ++i) {
        // Only the first line of a watch's output is shown -- a watch is
        // meant to be a scalar you keep an eye on, and letting one command
        // print 40 lines every frame would bury the rest of the panel.
        std::string output = console.Execute(m_Watches[i]);
        const size_t newline = output.find('\n');
        if (newline != std::string::npos) output = output.substr(0, newline) + " ...";
        group.Add(std::to_string(i) + " " + m_Watches[i], output);
    }
    if (group.rows.empty()) group.Add("(none)", "watch \"<command>\"");
    return {std::move(group)};
}

// ----------------------------------------------------------------- rendering

void DebugOverlay::Render(sk::OverlaySurface& surface, const DebugHost& host,
                           DebugConsole& console, int reservedTopPixels) {
    const int charWidth = surface.charWidth();
    const int lineHeight = surface.lineHeight();
    if (charWidth <= 0 || lineHeight <= 0) return;

    if (m_MiniBar) {
        // One line, bottom-right: the numbers you want without giving up any
        // of the view. Deliberately built from the host's own "mini" page so
        // there is exactly one place that decides what those numbers are.
        std::vector<StatGroup> mini;
        host.Inspect("mini", mini);
        std::string text = "fps " + TwoDecimals(Metrics::Get().framesPerSecond());
        for (const StatGroup& group : mini) {
            for (const auto& row : group.rows) text += "  " + row.first + " " + row.second;
        }
        const int width = static_cast<int>(text.size()) * charWidth + 8;
        const int x = (std::max)(0, surface.width() - width - 4);
        const int y = surface.height() - lineHeight - 4;
        surface.FillRect(x, y - 2, width, lineHeight + 4, kPanelColor, 70);
        surface.DrawText(x + 4, y, text, kValueColor);
    }

    if (m_Page.empty()) return;

    std::vector<StatGroup> groups;
    if (m_Page == "metrics") {
        groups = BuildMetricsPage();
    } else if (m_Page == "events") {
        groups = BuildEventsPage();
    } else if (m_Page == "binds") {
        groups = BuildBindsPage(console);
    } else if (m_Page == "watch") {
        groups = BuildWatchPage(console);
    } else {
        host.Inspect(m_Page, groups);
        if (groups.empty()) {
            StatGroup missing;
            missing.title = m_Page;
            missing.Add("(no data)", host.InGame() ? "unknown page" : "not in a zone");
            groups.push_back(std::move(missing));
        }
    }

    // Width is driven by the widest row, clamped so a long value cannot push
    // the panel across the whole screen.
    size_t widestName = 0;
    size_t widestRow = 0;
    for (const StatGroup& group : groups) {
        widestRow = (std::max)(widestRow, group.title.size());
        for (const auto& row : group.rows) {
            widestName = (std::max)(widestName, row.first.size());
        }
    }
    for (const StatGroup& group : groups) {
        for (const auto& row : group.rows) {
            widestRow = (std::max)(widestRow, widestName + 2 + row.second.size());
        }
    }
    const int maxColumns = (std::max)(20, surface.width() / charWidth - 4);
    const int columns = (std::min)(static_cast<int>(widestRow) + 2, maxColumns);

    int rowCount = 0;
    for (const StatGroup& group : groups) rowCount += 1 + static_cast<int>(group.rows.size());

    const int panelWidth = columns * charWidth + 10;
    const int panelHeight = rowCount * lineHeight + 10;
    const int x = (std::max)(0, surface.width() - panelWidth - 4);
    const int y = reservedTopPixels + 4;

    surface.FillRect(x, y, panelWidth, panelHeight, kPanelColor, 82);
    surface.FillRect(x, y, 2, panelHeight, kBarColor, 100);

    int textY = y + 5;
    for (const StatGroup& group : groups) {
        surface.DrawText(x + 6, textY, group.title, kTitleColor);
        textY += lineHeight;
        for (const auto& row : group.rows) {
            surface.DrawText(x + 6, textY, row.first, kNameColor);
            std::string value = row.second;
            const int valueColumn = static_cast<int>(widestName) + 2;
            if (static_cast<int>(value.size()) > columns - valueColumn) {
                value = value.substr(0, static_cast<size_t>((std::max)(0, columns - valueColumn)));
            }
            surface.DrawText(x + 6 + valueColumn * charWidth, textY, value, kValueColor);
            textY += lineHeight;
        }
    }
}

}  // namespace sk_debug
