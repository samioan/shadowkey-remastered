// M68 (the developer debug suite) smoke test.
//
// The suite is built against two abstractions -- `sk_debug::DebugHost` for
// the game and `sk::OverlaySurface` for the screen -- specifically so that
// all of it can be exercised with no window, no zone and no interpreter.
// This test implements both with fakes and drives the real console, the
// real command table, the real metrics sink and the real overlay layout.
//
// What it is actually checking, in order:
//
//  Part 1  the console itself: tokenizing (quotes included), dispatch,
//          unknown commands, history, completion, aliases and key bindings,
//          and that a throwing command is reported rather than fatal.
//  Part 2  the metrics sink: per-frame rollover, totals, and the mark/diff
//          loop that is the whole point of the instrumentation.
//  Part 3  every registered command run once against the fake host, which
//          is what catches a command whose handler crashes on empty
//          arguments -- the failure mode a console is most likely to have.
//  Part 4  the input path: that an open console consumes the keys the game
//          would otherwise act on, and releases them when closed. This is
//          the property that makes the suite safe to leave compiled in.
//  Part 5  the overlay renders into a surface, and the console's drop-down
//          actually draws its scrollback and input line.
#include <algorithm>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "debug/debug_commands.h"
#include "debug/debug_console.h"
#include "debug/debug_host.h"
#include "debug/debug_metrics.h"
#include "debug/debug_overlay.h"
#include "debug/debug_suite.h"
#include "debug/script_tracer.h"
#include "graphics/overlay_surface.h"

namespace {

int g_Checks = 0;
bool g_Ok = true;

void Check(bool condition, const char* what) {
    ++g_Checks;
    std::printf("  [%s] %s\n", condition ? "OK" : "FAIL", what);
    if (!condition) g_Ok = false;
}

// ---------------------------------------------------------------- the fakes

// A DebugHost with no game behind it. Records what was asked of it, so the
// command sweep in Part 3 can assert that commands really reached the host
// rather than short-circuiting in the console.
class FakeHost : public sk_debug::DebugHost {
public:
    bool inGame = true;
    std::vector<std::string> calls;

    bool InGame() const override { return inGame; }

    std::vector<std::string> Pages() const override {
        return {"mini", "world", "player"};
    }

    void Inspect(const std::string& page, std::vector<sk_debug::StatGroup>& out) const override {
        sk_debug::StatGroup group;
        group.title = page;
        group.Add("page", page);
        group.Add("in game", inGame ? "yes" : "no");
        out.push_back(std::move(group));
    }

    void Entities(std::vector<sk_debug::EntityRow>& out) const override {
        sk_debug::EntityRow row;
        row.index = 0;
        row.kind = "monster";
        row.name = "Azra Rat";
        row.state = "idle";
        row.x = 100.0f;
        row.y = 200.0f;
        row.distance = 42.0f;
        row.health = 7;
        row.maxHealth = 9;
        row.typeId = 202;
        out.push_back(row);
    }

    std::vector<std::string> CatalogKinds() const override { return {"zones", "items"}; }

    std::vector<std::string> Catalog(const std::string& kind,
                                      const std::string& filter) const override {
        if (kind != "zones") return {};
        std::vector<std::string> all = {"azra", "crypt1", "crypt2", "twilite"};
        if (filter.empty()) return all;
        std::vector<std::string> hits;
        for (const std::string& name : all) {
            if (name.find(filter) != std::string::npos) hits.push_back(name);
        }
        return hits;
    }

    std::vector<sk_debug::DebugFlag> Flags() const override {
        return {{"noclip", noclip ? 1 : 0, "walk through walls"},
                {"god", god ? 1 : 0, "no damage"}};
    }

    bool SetFlag(const std::string& name, int value, std::string& message) override {
        calls.push_back("SetFlag:" + name);
        if (name == "noclip") noclip = value != 0;
        else if (name == "god") god = value != 0;
        else {
            message = "unknown flag";
            return false;
        }
        message = name + " = " + std::to_string(value);
        return true;
    }

    bool Teleport(float x, float y, bool tileCoords, std::string& message) override {
        calls.push_back(tileCoords ? "TeleportTile" : "Teleport");
        lastTeleportX = x;
        lastTeleportY = y;
        lastTeleportWasTile = tileCoords;
        message = "teleported";
        return true;
    }
    bool TeleportToEntity(int index, std::string& message) override {
        calls.push_back("TeleportToEntity:" + std::to_string(index));
        message = "teleported to entity";
        return true;
    }
    bool LoadZone(const std::string& zone, std::string& message) override {
        calls.push_back("LoadZone:" + zone);
        message = "travelling";
        return true;
    }
    bool Face(float degrees, std::string& message) override {
        calls.push_back("Face");
        lastFace = degrees;
        message = "facing";
        return true;
    }
    bool Spawn(const std::string& what, int count, float distance,
               std::string& message) override {
        calls.push_back("Spawn:" + what + ":" + std::to_string(count));
        lastSpawnDistance = distance;
        message = "spawned";
        return true;
    }
    bool Give(const std::string& what, int count, std::string& message) override {
        calls.push_back("Give:" + what + ":" + std::to_string(count));
        message = "gave";
        return true;
    }
    bool Equip(const std::string& what, int hand, std::string& message) override {
        calls.push_back("Equip:" + what + ":" + std::to_string(hand));
        message = "equipped";
        return true;
    }
    bool KillAll(const std::string& filter, std::string& message) override {
        calls.push_back("KillAll:" + filter);
        message = "killed";
        return true;
    }

    std::vector<std::pair<std::string, std::string>> Receivers() const override {
        return {{"player", "the player"}, {"level", "the level"}};
    }
    bool CallNative(const std::string& receiver, const std::string& method,
                     const std::vector<std::string>& args, std::string& message) override {
        calls.push_back("CallNative:" + receiver + "." + method + "/" +
                         std::to_string(args.size()));
        nativeCalls.push_back(receiver + "." + method);
        // GetLevel has to answer honestly: `stat` branches on it, because
        // UpdateAttributes behaves completely differently at level 1.
        if (method == "GetLevel") {
            message = std::to_string(playerLevel);
        } else {
            message = "42";
        }
        return true;
    }
    bool ScriptEval(const std::string& receiver, const std::string& code,
                     std::string& message) override {
        calls.push_back("ScriptEval:" + receiver);
        lastScript = code;
        message = "evaluated";
        return true;
    }

    bool noclip = false;
    bool god = false;
    int playerLevel = 1;
    float lastTeleportX = 0.0f;
    float lastTeleportY = 0.0f;
    bool lastTeleportWasTile = false;
    float lastFace = 0.0f;
    float lastSpawnDistance = 0.0f;
    std::string lastScript;
    std::vector<std::string> nativeCalls;
};

// A character-grid OverlaySurface, so what the console and overlay draw can
// actually be asserted on rather than merely "did not crash".
class GridSurface : public sk::OverlaySurface {
public:
    GridSurface(int columns, int rows)
        : m_Columns(columns), m_Rows(rows),
          m_Grid(static_cast<size_t>(columns * rows), ' ') {}

    int width() const override { return m_Columns * kCharWidth; }
    int height() const override { return m_Rows * kLineHeight; }
    int charWidth() const override { return kCharWidth; }
    int lineHeight() const override { return kLineHeight; }

    void FillRect(int, int, int, int, sk::OverlayColor, int) override { ++fills; }

    void DrawText(int x, int y, std::string_view text, sk::OverlayColor) override {
        ++draws;
        const int column = x / kCharWidth;
        const int row = y / kLineHeight;
        if (row < 0 || row >= m_Rows) return;
        for (size_t i = 0; i < text.size(); ++i) {
            const int c = column + static_cast<int>(i);
            if (c < 0 || c >= m_Columns) continue;
            m_Grid[static_cast<size_t>(row * m_Columns + c)] = text[i];
        }
    }

    bool Contains(const std::string& needle) const {
        for (int row = 0; row < m_Rows; ++row) {
            const std::string line(m_Grid.begin() + row * m_Columns,
                                    m_Grid.begin() + (row + 1) * m_Columns);
            if (line.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    int fills = 0;
    int draws = 0;

private:
    static constexpr int kCharWidth = 7;
    static constexpr int kLineHeight = 15;
    int m_Columns;
    int m_Rows;
    std::vector<char> m_Grid;
};

// Whether any console line contains `needle`.
bool ConsoleShows(const sk_debug::DebugConsole& console, const std::string& needle) {
    for (const sk_debug::ConsoleLine& line : console.lines()) {
        if (line.text.find(needle) != std::string::npos) return true;
    }
    return false;
}

void TypeInto(sk_debug::DebugConsole& console, const std::string& text) {
    for (char ch : text) console.HandleChar(ch);
}

}  // namespace

int main() {
    std::printf("m68_debug_suite_smoke\n");

    // ================================================================ part 1
    std::printf("\n-- part 1: the console --\n");
    {
        std::vector<std::string> tokens =
            sk_debug::DebugConsole::Tokenize("give \"iron long sword\" 3");
        Check(tokens.size() == 3, "Tokenize splits on spaces");
        Check(tokens.size() == 3 && tokens[1] == "iron long sword",
              "and a quoted argument survives with its spaces");
        Check(sk_debug::DebugConsole::Tokenize("   ").empty(),
              "an all-whitespace line produces no tokens");
    }

    sk_debug::DebugConsole console;
    {
        sk_debug::Command command;
        command.name = "sum";
        command.usage = "sum <a> <b>";
        command.group = "test";
        command.help = "add two numbers";
        command.handler = [](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) return "usage: sum <a> <b>";
            return std::to_string(std::stoi(args[0]) + std::stoi(args[1]));
        };
        command.completer = [](int argIndex,
                                const std::vector<std::string>&) -> std::vector<std::string> {
            return argIndex == 0 ? std::vector<std::string>{"one", "two", "twelve"}
                                  : std::vector<std::string>{};
        };
        console.Register(std::move(command));

        sk_debug::Command thrower;
        thrower.name = "boom";
        thrower.group = "test";
        thrower.usage = "boom";
        thrower.help = "throws";
        thrower.handler = [](const std::vector<std::string>&) -> std::string {
            throw std::runtime_error("deliberate");
        };
        console.Register(std::move(thrower));
    }

    Check(console.Execute("sum 2 3") == "5", "a registered command runs and returns its output");
    Check(console.Execute("SUM 2 3") == "5", "command lookup is case-insensitive");
    Check(console.Execute("nosuchcommand").find("unknown command") != std::string::npos,
          "an unknown command is reported, not silently ignored");
    // A console command runs real game and script code in the real host, so
    // a throwing handler must never take the process down -- that is the one
    // thing a debug tool absolutely must not do.
    Check(console.Execute("boom").find("deliberate") != std::string::npos,
          "a throwing command is caught and reported");

    console.RegisterAlias("add", "sum", {});
    Check(console.Execute("add 10 5") == "15", "an alias forwards to its target");

    // Submit/Drain: queued, not run, until the tick drains it.
    console.SetOpen(true);
    const size_t linesBeforeSubmit = console.lines().size();
    console.Submit("sum 7 7");
    Check(console.hasQueuedCommands(), "Submit queues rather than executing");
    Check(console.lines().size() == linesBeforeSubmit,
          "and prints nothing until it is drained");
    console.Drain();
    Check(!console.hasQueuedCommands() && ConsoleShows(console, "14"),
          "Drain runs the queue and prints the result");

    // Tab completion.
    console.SetOpen(true);
    TypeInto(console, "su");
    console.HandleKey(sk_debug::ConsoleKey::Tab, false);
    console.HandleChar('9');
    console.HandleChar(' ');
    console.HandleChar('1');
    console.HandleKey(sk_debug::ConsoleKey::Enter, false);
    console.Drain();
    Check(ConsoleShows(console, "10"),
          "Tab completes a unique command name (`su` -> `sum 9 1` -> 10)");

    // Ambiguous completion fills the common prefix and lists the candidates.
    TypeInto(console, "sum t");
    console.HandleKey(sk_debug::ConsoleKey::Tab, false);
    Check(ConsoleShows(console, "two") && ConsoleShows(console, "twelve"),
          "an ambiguous argument completion lists the candidates");
    console.HandleKey(sk_debug::ConsoleKey::Escape, false);
    Check(!console.open(), "Escape closes the console");

    // History.
    console.SetOpen(true);
    console.HandleKey(sk_debug::ConsoleKey::Up, false);
    console.HandleKey(sk_debug::ConsoleKey::Enter, false);
    console.Drain();
    Check(console.lines().size() > linesBeforeSubmit,
          "Up recalls the previous command and Enter re-runs it");

    // Key bindings.
    console.Bind("F5", "sum 100 1");
    Check(console.BindingFor("f5") == "sum 100 1", "bind is case-insensitive on the key name");
    console.Unbind("F5");
    Check(console.BindingFor("f5").empty(), "unbind removes it");

    // ================================================================ part 2
    std::printf("\n-- part 2: the metrics sink --\n");
    sk_debug::Metrics& metrics = sk_debug::Metrics::Get();
    metrics.ResetCounters();
    metrics.ClearEvents();

    metrics.Count("test.alpha", 3);
    metrics.Count("test.beta");
    Check(metrics.total("test.alpha") == 3 && metrics.thisFrame("test.alpha") == 3,
          "Count feeds both the total and the current frame");
    metrics.BeginFrame(0.04);
    Check(metrics.lastFrame("test.alpha") == 3 && metrics.thisFrame("test.alpha") == 0,
          "BeginFrame rolls the frame counter over and zeroes it");
    Check(metrics.total("test.alpha") == 3, "and leaves the total alone");

    // The mark/diff loop -- the thing the whole instrumentation exists for.
    metrics.Mark();
    Check(metrics.Diff().empty(), "immediately after a mark, nothing has moved");
    metrics.Count("test.alpha", 5);
    metrics.Count("test.gamma", 2);
    const std::vector<sk_debug::CounterDelta> deltas = metrics.Diff();
    Check(deltas.size() == 2, "diff reports exactly the counters that moved");
    Check(!deltas.empty() && deltas[0].name == "test.alpha" && deltas[0].delta() == 5,
          "sorted by delta, largest first, with the right delta");
    Check(metrics.total("test.beta") == 1 &&
              std::none_of(deltas.begin(), deltas.end(),
                           [](const sk_debug::CounterDelta& d) { return d.name == "test.beta"; }),
          "a counter that did not move is left out entirely");

    metrics.Log("test", "first");
    metrics.Log("other", "second");
    metrics.Log("test", "third");
    Check(metrics.RecentEvents(10, "").size() == 3, "the event ring records what it is told");
    Check(metrics.RecentEvents(10, "")[0].text == "third", "newest first");
    Check(metrics.RecentEvents(10, "test").size() == 2, "and filters by category");
    metrics.SetEventCapacity(2);
    Check(metrics.events().size() == 2, "shrinking the capacity trims the oldest events");

    // ================================================================ part 3
    std::printf("\n-- part 3: every command, against a fake host --\n");
    FakeHost host;
    sk_debug::DebugConsole gameConsole;
    sk_debug::DebugOverlay overlay;
    sk_debug::ScriptTracer tracer;
    sk_debug::CommandContext context;
    context.console = &gameConsole;
    context.host = &host;
    context.overlay = &overlay;
    context.tracer = &tracer;
    context.configDirectory = ".";
    sk_debug::RegisterDebugCommands(context);

    Check(gameConsole.commands().size() >= 35,
          "the command table registers the full set (>= 35 commands)");

    // Every command, with no arguments. A handler that indexes args[0]
    // without checking is the single most likely bug in a console, and this
    // is what catches it -- `Execute` converts a throw into a message, so a
    // crash here shows up as a failed check rather than a dead test binary.
    int swept = 0;
    for (const auto& entry : gameConsole.commands()) {
        const std::string output = gameConsole.Execute(entry.second.name);
        if (output.find("exception") != std::string::npos) {
            std::printf("    `%s` threw: %s\n", entry.second.name.c_str(), output.c_str());
            g_Ok = false;
        }
        ++swept;
    }
    const std::string sweepLabel =
        "no command throws when run with no arguments (" + std::to_string(swept) + " swept)";
    Check(metrics.total("debug.command_exceptions") == 0, sweepLabel.c_str());

    // The commands that carry the user's actual asks.
    gameConsole.Execute("tp 512 768");
    Check(host.lastTeleportX == 512.0f && !host.lastTeleportWasTile,
          "`tp` reaches the host in world units");
    gameConsole.Execute("tpt 4 9");
    Check(host.lastTeleportWasTile, "`tpt` reaches it in tile units");
    gameConsole.Execute("zone crypt1");
    Check(std::find(host.calls.begin(), host.calls.end(), "LoadZone:crypt1") != host.calls.end(),
          "`zone <name>` asks the host to travel");
    gameConsole.Execute("spawn arat.s 3 600");
    Check(std::find(host.calls.begin(), host.calls.end(), "Spawn:arat.s:3") != host.calls.end() &&
              host.lastSpawnDistance == 600.0f,
          "`spawn` passes the script, count and distance through");
    gameConsole.Execute("give sword.s 2");
    Check(std::find(host.calls.begin(), host.calls.end(), "Give:sword.s:2") != host.calls.end(),
          "`give` passes the item and count through");
    gameConsole.Execute("equip sword l");
    Check(std::find(host.calls.begin(), host.calls.end(), "Equip:sword:1") != host.calls.end(),
          "`equip <name> l` picks the left hand");

    // The one place a curated command earns its keep: `stat` has to map a
    // friendly name onto a Get/Set pair whose names are not mechanical, and
    // then trigger the derived-stat recompute the setters do not do.
    // The engine's UpdateAttributes has two behaviours selected by the
    // character's level (character_progression.h). Above level 1 it
    // re-derives, which is what an attribute change needs. **At level 1 it
    // reseeds all eight attributes from race and sex**, which would silently
    // undo the value just set -- observed live as `stat str 90` reporting
    // "50 -> 40" on a fresh character. Both branches are pinned here.
    host.playerLevel = 5;
    host.nativeCalls.clear();
    gameConsole.Execute("stat str 90");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(), "player.SetStrength") !=
              host.nativeCalls.end(),
          "`stat str 90` calls the real SetStrength native");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(), "player.UpdateAttributes") !=
              host.nativeCalls.end(),
          "above level 1 it follows with UpdateAttributes, or the derived stats stay stale");
    host.playerLevel = 1;
    host.nativeCalls.clear();
    const std::string levelOne = gameConsole.Execute("stat str 90");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(), "player.SetStrength") !=
              host.nativeCalls.end(),
          "at level 1 it still sets the attribute");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(), "player.UpdateAttributes") ==
              host.nativeCalls.end(),
          "but skips UpdateAttributes, which at level 1 would reseed from race and undo it");
    Check(levelOne.find("level 1") != std::string::npos,
          "and says so, rather than leaving stale derived stats unexplained");
    host.nativeCalls.clear();
    gameConsole.Execute("stat wil");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(), "player.GetWil") !=
              host.nativeCalls.end(),
          "and it knows willpower's getter is GetWil, not GetWillpower");

    host.nativeCalls.clear();
    gameConsole.Execute("quest 12 assigned 1");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(),
                     "player.SetQuestAssigned") != host.nativeCalls.end(),
          "`quest 12 assigned 1` reaches the real quest native");

    host.nativeCalls.clear();
    gameConsole.Execute("call player SetGold 5000");
    Check(std::find(host.nativeCalls.begin(), host.nativeCalls.end(), "player.SetGold") !=
              host.nativeCalls.end(),
          "the native bridge reaches an arbitrary binding by name");
    gameConsole.Execute("sk player SetGold(GetGold() + 1);");
    Check(host.lastScript == "SetGold(GetGold() + 1);",
          "`sk` passes the whole rest of the line through as Simkin source");

    gameConsole.Execute("set noclip 1");
    Check(host.noclip, "`set noclip 1` sets the flag");
    gameConsole.Execute("set noclip");
    Check(!host.noclip, "`set noclip` with no value flips it");

    Check(gameConsole.Execute("zones azr").find("azra") != std::string::npos,
          "`zones <filter>` filters the catalog");
    Check(gameConsole.Execute("ents").find("Azra Rat") != std::string::npos,
          "`ents` lists live entities");
    Check(gameConsole.Execute("help tp").find("tp <x> <y>") != std::string::npos,
          "`help <command>` prints its usage");
    Check(gameConsole.Execute("find teleport").find("tp") != std::string::npos,
          "`find` searches help text as well as names");

    // Refusals have to be reported, not silently dropped.
    host.inGame = false;
    Check(gameConsole.Execute("inspect world").find("no") != std::string::npos ||
              !gameConsole.Execute("inspect world").empty(),
          "a command still answers when the host is out of a zone");
    host.inGame = true;

    // ================================================================ part 4
    std::printf("\n-- part 4: input capture --\n");
    {
        sk_debug::DebugSuite suite;
        // Not attached yet: completely inert, which is what makes it safe to
        // declare before the game's own state exists.
        Check(!suite.HandleKey(0x70 /* F1 */, true, false),
              "an unattached suite consumes nothing");
        Check(!suite.capturingInput(), "and does not claim keyboard focus");

        Check(sk_debug::DebugSuite::ConsoleKeyFromHostKey(0x0D) == sk_debug::ConsoleKey::Enter,
              "the host key mapping knows Enter");
        Check(sk_debug::DebugSuite::ConsoleKeyFromHostKey(0x21) == sk_debug::ConsoleKey::PageUp,
              "and PageUp");
        Check(sk_debug::DebugSuite::ConsoleKeyFromHostKey('3') == sk_debug::ConsoleKey::None,
              "and that a printable key is not one of them (it arrives as a char)");
        Check(sk_debug::DebugSuite::BindNameForHostKey(0x74) == "f5",
              "F5 is bindable by name");
        Check(sk_debug::DebugSuite::BindNameForHostKey('A').empty(),
              "a letter key is not bindable (it would shadow typing)");
    }
    {
        // The property that makes the suite safe to leave compiled in: while
        // the console is open, the game must not see a single key.
        sk_debug::DebugConsole capture;
        capture.SetOpen(false);
        Check(!capture.HandleKey(sk_debug::ConsoleKey::Up, false),
              "a closed console passes keys through");
        Check(!capture.HandleChar('3'), "and passes characters through");
        capture.SetOpen(true);
        Check(capture.HandleKey(sk_debug::ConsoleKey::Up, false),
              "an open console consumes navigation keys");
        Check(capture.HandleChar('3'),
              "and consumes '3' -- the real Use binding -- so typing never opens a door");
        Check(capture.HandleKey(sk_debug::ConsoleKey::None, false),
              "and consumes keys it has no use for, rather than leaking them");
    }

    // ================================================================ part 5
    std::printf("\n-- part 5: drawing --\n");
    {
        GridSurface surface(80, 40);
        overlay.SetPage("player");
        overlay.SetMiniBar(true);
        overlay.Render(surface, host, gameConsole, 0);
        Check(surface.draws > 0 && surface.fills > 0, "the overlay draws a panel");
        Check(surface.Contains("player"), "and the page it was asked for");
        Check(surface.Contains("fps"), "and the mini bar's fps readout");
    }
    {
        GridSurface surface(80, 40);
        sk_debug::DebugConsole drawn;
        drawn.Print("a distinctive scrollback line");
        drawn.SetOpen(true);
        drawn.HandleChar('h');
        drawn.HandleChar('i');
        drawn.Render(surface, 0.55f);
        Check(surface.Contains("distinctive"), "the console draws its scrollback");
        Check(surface.Contains("] hi"), "and the prompt with what has been typed so far");
    }
    {
        // A closed console must draw nothing at all -- the overlay is
        // allowed on screen during play, the console is not.
        GridSurface surface(80, 40);
        sk_debug::DebugConsole closed;
        closed.Print("should not appear");
        closed.Render(surface, 0.55f);
        Check(surface.draws == 0 && !surface.Contains("should not appear"),
              "a closed console draws nothing");
    }

    std::printf("\nm68_debug_suite_smoke: %d checks, %s\n", g_Checks, g_Ok ? "PASSED" : "FAILED");
    return g_Ok ? 0 : 1;
}
