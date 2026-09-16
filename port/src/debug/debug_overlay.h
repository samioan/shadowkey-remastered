#pragma once

// M68: the always-on-screen half of the suite -- the stat panels you leave
// running while you play, as opposed to the console you open to change
// something.
//
// Pages come from `DebugHost::Pages()` / `DebugHost::Inspect()`, so this
// file has no per-page code: adding a panel is a main.cpp change. The three
// pages that are *not* host data -- the metrics table, the event log and the
// key-binding cheat sheet -- are built here, because they read the debug
// suite's own state rather than the game's.
//
// Layout is a right-aligned column of panels. Right-aligned because the
// game's own HUD (compass banner across the top, vitals cluster bottom
// left) occupies the left and the panels should not sit on top of the thing
// being debugged.

#include <string>
#include <vector>

#include "debug/debug_host.h"
#include "graphics/overlay_surface.h"

namespace sk_debug {

class DebugConsole;

class DebugOverlay {
public:
    // "" means no overlay. Anything else is a host page name, or one of the
    // suite's own: "metrics", "events", "binds", "watch".
    void SetPage(std::string page) { m_Page = std::move(page); }
    const std::string& page() const { return m_Page; }
    bool visible() const { return !m_Page.empty(); }

    // Cycles through host pages then the suite's own, ending back at off.
    void CyclePage(const DebugHost& host, int direction);

    // Page names that are not the host's -- built here.
    static std::vector<std::string> BuiltInPages();

    // `watch` expressions: console command lines whose output is evaluated
    // and shown every frame. The classic "print this every frame without
    // adding a printf" tool.
    void AddWatch(std::string commandLine) { m_Watches.push_back(std::move(commandLine)); }
    void ClearWatches() { m_Watches.clear(); }
    const std::vector<std::string>& watches() const { return m_Watches; }
    bool RemoveWatch(size_t index);

    // The single-line always-on readout (fps, position, tile) shown even
    // when no page is selected, if enabled.
    //
    // M107: **off by default** (Shift+F1 turns it on, `mini` toggles it
    // from the console). It used to default on, which meant every launch
    // -- including a plain "play the game" one -- drew a debug readout
    // over the 3D view before the player had asked for anything. `m_Page`
    // has always defaulted to "" (no panel), so with this the suite now
    // renders nothing at all until it is asked to.
    void SetMiniBar(bool on) { m_MiniBar = on; }
    bool miniBar() const { return m_MiniBar; }

    void SetEventFilter(std::string filter) { m_EventFilter = std::move(filter); }
    const std::string& eventFilter() const { return m_EventFilter; }

    // `console` is non-const because evaluating a watch runs a command.
    void Render(sk::OverlaySurface& surface, const DebugHost& host, DebugConsole& console,
                int reservedTopPixels);

private:
    std::vector<StatGroup> BuildMetricsPage() const;
    std::vector<StatGroup> BuildEventsPage() const;
    std::vector<StatGroup> BuildBindsPage(const DebugConsole& console) const;
    std::vector<StatGroup> BuildWatchPage(DebugConsole& console) const;

    std::string m_Page;
    std::string m_EventFilter;
    std::vector<std::string> m_Watches;
    bool m_MiniBar = false;
};

}  // namespace sk_debug
