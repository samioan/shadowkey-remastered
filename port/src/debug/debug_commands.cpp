#include "debug/debug_commands.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>

#include "debug/debug_console.h"
#include "debug/debug_host.h"
#include "debug/debug_metrics.h"
#include "debug/debug_overlay.h"
#include "debug/script_tracer.h"

namespace sk_debug {

namespace {

// ---------------------------------------------------------------- formatting

std::string Join(const std::vector<std::string>& items, const char* separator) {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += separator;
        out += items[i];
    }
    return out;
}

std::string Lines(const std::vector<std::string>& items) {
    return Join(items, "\n");
}

// `args` from index `first` onward, joined with spaces -- the "rest of the
// line" every command that takes free text needs.
std::string Rest(const std::vector<std::string>& args, size_t first) {
    if (first >= args.size()) return std::string();
    return Join(std::vector<std::string>(args.begin() + static_cast<std::ptrdiff_t>(first),
                                          args.end()),
                 " ");
}

std::string Number(double value, int decimals = 1) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

bool ParseFloat(const std::string& text, float& out) {
    try {
        size_t consumed = 0;
        const float value = std::stof(text, &consumed);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseInt(const std::string& text, int& out) {
    try {
        size_t consumed = 0;
        const int value = std::stoi(text, &consumed, 0);
        if (consumed != text.size()) return false;
        out = value;
        return true;
    } catch (...) {
        return false;
    }
}

std::string RenderGroups(const std::vector<StatGroup>& groups) {
    if (groups.empty()) return "(no data)";
    std::string out;
    for (const StatGroup& group : groups) {
        if (!out.empty()) out += "\n";
        out += "-- " + group.title + " --";
        size_t widest = 0;
        for (const auto& row : group.rows) widest = (std::max)(widest, row.first.size());
        for (const auto& row : group.rows) {
            out += "\n  " + row.first + std::string(widest - row.first.size() + 2, ' ') +
                    row.second;
        }
    }
    return out;
}

// Wraps a long list of short items into fixed-width columns, so `zones` or
// `items` does not produce 400 one-word lines of scrollback.
std::string Columnize(const std::vector<std::string>& items, size_t perRow = 4) {
    if (items.empty()) return "(none)";
    size_t widest = 0;
    for (const std::string& item : items) widest = (std::max)(widest, item.size());
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i && i % perRow == 0) out += "\n";
        out += items[i];
        if ((i + 1) % perRow != 0 && i + 1 < items.size()) {
            out += std::string(widest - items[i].size() + 2, ' ');
        }
    }
    return out;
}

std::string EntityLine(const EntityRow& row) {
    std::string out = "[" + std::to_string(row.index) + "] " + row.kind + " " + row.name;
    if (row.health >= 0) {
        out += "  hp " + std::to_string(row.health) + "/" + std::to_string(row.maxHealth);
    }
    if (!row.alive) out += "  DEAD";
    if (!row.state.empty()) out += "  " + row.state;
    out += "  @(" + Number(row.x, 0) + "," + Number(row.y, 0) + "," + Number(row.z, 0) + ")";
    out += "  d=" + Number(row.distance, 0);
    if (row.typeId >= 0) out += "  type " + std::to_string(row.typeId);
    if (row.modelIndex >= 0) out += "  model " + std::to_string(row.modelIndex);
    return out;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

// ==========================================================================

void RegisterDebugCommands(CommandContext& context) {
    DebugConsole& console = *context.console;
    DebugHost& host = *context.host;
    DebugOverlay& overlay = *context.overlay;
    ScriptTracer& tracer = *context.tracer;
    CommandContext* ctx = &context;

    auto add = [&console](const char* name, const char* usage, const char* group, const char* help,
                          CommandHandler handler, CompletionProvider completer = nullptr) {
        Command command;
        command.name = name;
        command.usage = usage;
        command.group = group;
        command.help = help;
        command.handler = std::move(handler);
        command.completer = std::move(completer);
        console.Register(std::move(command));
    };

    // A completer that offers one catalog's contents for argument 0.
    auto catalogCompleter = [&host](const char* kind) {
        std::string kindName = kind;
        return [&host, kindName](int argIndex,
                                  const std::vector<std::string>& args) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            return host.Catalog(kindName, args.empty() ? std::string() : args[0]);
        };
    };

    // ---------------------------------------------------------------- meta

    add("help", "help [command|group]", "meta",
        "list commands, or explain one command / one group",
        [&console](const std::vector<std::string>& args) -> std::string {
            const auto& commands = console.commands();
            if (!args.empty()) {
                auto it = commands.find(Lower(args[0]));
                if (it != commands.end()) {
                    return it->second.usage + "\n    " + it->second.help + "\n    group: " +
                            it->second.group;
                }
                // Not a command -- try it as a group name.
                std::vector<std::string> matches;
                for (const auto& entry : commands) {
                    if (entry.second.group == Lower(args[0])) {
                        matches.push_back(entry.second.usage + "  -- " + entry.second.help);
                    }
                }
                if (matches.empty()) return "no command or group named `" + args[0] + "`";
                return Lines(matches);
            }
            // Grouped index. Usage lines only -- `help <cmd>` has the detail.
            std::map<std::string, std::vector<std::string>> byGroup;
            for (const auto& entry : commands) {
                byGroup[entry.second.group].push_back(entry.second.name);
            }
            std::vector<std::string> out;
            for (auto& group : byGroup) {
                std::sort(group.second.begin(), group.second.end());
                out.push_back(group.first + ":  " + Join(group.second, " "));
            }
            out.push_back("");
            out.push_back("`help <command>` for usage. Tab completes. Ctrl+Up/Down scrolls.");
            return Lines(out);
        },
        [&console](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            std::vector<std::string> names;
            for (const auto& entry : console.commands()) names.push_back(entry.second.name);
            return names;
        });

    add("find", "find <substring>", "meta", "search command names and help text",
        [&console](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: find <substring>";
            const std::string needle = Lower(args[0]);
            std::vector<std::string> hits;
            for (const auto& entry : console.commands()) {
                if (Lower(entry.second.name).find(needle) != std::string::npos ||
                    Lower(entry.second.help).find(needle) != std::string::npos) {
                    hits.push_back(entry.second.usage + "  -- " + entry.second.help);
                }
            }
            return hits.empty() ? "no match" : Lines(hits);
        });

    add("clear", "clear", "meta", "clear the console scrollback",
        [&console](const std::vector<std::string>&) -> std::string {
            console.Clear();
            return std::string();
        });

    add("echo", "echo <text...>", "meta", "print its arguments (useful inside an exec script)",
        [](const std::vector<std::string>& args) -> std::string { return Join(args, " "); });

    add("alias", "alias <name> <command> [args...]", "meta", "define a new name for a command",
        [&console](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) return "usage: alias <name> <command> [args...]";
            console.RegisterAlias(args[0], args[1],
                                   std::vector<std::string>(args.begin() + 2, args.end()));
            return "alias `" + args[0] + "` -> `" + Rest(args, 1) + "`";
        });

    add("bind", "bind <key> \"<command>\"", "meta",
        "run a command when a key is pressed with the console closed (F1-F12, F5-F12 are free)",
        [&console](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                if (console.bindings().empty()) return "(no bindings)";
                std::vector<std::string> out;
                for (const auto& entry : console.bindings()) {
                    out.push_back(entry.first + "  " + entry.second);
                }
                return Lines(out);
            }
            if (args.size() == 1) {
                console.Unbind(args[0]);
                return "unbound " + args[0];
            }
            console.Bind(args[0], Rest(args, 1));
            return "bound " + args[0];
        });

    add("exec", "exec <file>", "meta",
        "run a file of console commands from port/debug/ -- a saved repro",
        [&console, ctx](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: exec <file>";
            std::string path = args[0];
            if (path.find('/') == std::string::npos && path.find('\\') == std::string::npos) {
                if (path.find('.') == std::string::npos) path += ".cfg";
                path = ctx->configDirectory + "/" + path;
            }
            std::ifstream file(path);
            if (!file) return "cannot open " + path;
            std::vector<std::string> output;
            std::string line;
            int count = 0;
            while (std::getline(file, line)) {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const size_t first = line.find_first_not_of(" \t");
                if (first == std::string::npos) continue;
                if (line[first] == '#' || line.compare(first, 2, "//") == 0) continue;
                ++count;
                // Executed immediately rather than queued: an exec file is a
                // sequence, and half of it running next tick would break any
                // script that sets something up and then reads it back.
                const std::string result = console.Execute(line.substr(first));
                output.push_back("> " + line.substr(first));
                if (!result.empty()) output.push_back(result);
            }
            output.push_back("(" + std::to_string(count) + " commands from " + path + ")");
            return Lines(output);
        });

    add("quit", "quit", "meta", "close the game",
        [ctx](const std::vector<std::string>&) -> std::string {
            ctx->quitRequested = true;
            return "quitting";
        });

    // ------------------------------------------------------------- world

    add("where", "where", "world", "print the full world/player position readout",
        [&host](const std::vector<std::string>&) -> std::string {
            std::vector<StatGroup> groups;
            host.Inspect("world", groups);
            return RenderGroups(groups);
        });

    add("tp", "tp <x> <y>", "world", "teleport to world coordinates",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) return "usage: tp <x> <y>   (world units; see `tpt` for tiles)";
            float x = 0.0f, y = 0.0f;
            if (!ParseFloat(args[0], x) || !ParseFloat(args[1], y)) return "bad coordinates";
            std::string message;
            host.Teleport(x, y, false, message);
            return message;
        });

    add("tpt", "tpt <tileX> <tileY>", "world", "teleport to the centre of a tile",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) return "usage: tpt <tileX> <tileY>";
            float x = 0.0f, y = 0.0f;
            if (!ParseFloat(args[0], x) || !ParseFloat(args[1], y)) return "bad tile coordinates";
            std::string message;
            host.Teleport(x, y, true, message);
            return message;
        });

    add("tpe", "tpe <entity index>", "world", "teleport next to a live entity (see `ents`)",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: tpe <index>   (indices come from `ents`)";
            int index = 0;
            if (!ParseInt(args[0], index)) return "bad index";
            std::string message;
            host.TeleportToEntity(index, message);
            return message;
        });

    add("zone", "zone [name]", "world",
        "print the current zone, or travel to another (a real Level.LoadLevel transition)",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                std::vector<StatGroup> groups;
                host.Inspect("zone", groups);
                return RenderGroups(groups);
            }
            std::string message;
            host.LoadZone(args[0], message);
            return message;
        },
        catalogCompleter("zones"));

    add("zones", "zones [filter]", "world", "list every zone the game ships",
        [&host](const std::vector<std::string>& args) -> std::string {
            return Columnize(host.Catalog("zones", args.empty() ? std::string() : args[0]), 6);
        });

    add("face", "face <degrees>", "world", "turn to an absolute compass heading (0 = +x)",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: face <degrees>";
            float degrees = 0.0f;
            if (!ParseFloat(args[0], degrees)) return "bad angle";
            std::string message;
            host.Face(degrees, message);
            return message;
        });

    add("tile", "tile [tileX tileY]", "world",
        "dump one tile's flags, heights and light -- defaults to the tile you are standing on",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::vector<StatGroup> groups;
            if (args.size() >= 2) {
                host.Inspect("tile:" + args[0] + "," + args[1], groups);
            } else {
                host.Inspect("tile", groups);
            }
            return RenderGroups(groups);
        });

    // ------------------------------------------------------------ entities

    add("ents", "ents [filter]", "entities", "list live entities, nearest first",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::vector<EntityRow> rows;
            host.Entities(rows);
            const std::string filter = args.empty() ? std::string() : Lower(args[0]);
            std::vector<std::string> out;
            for (const EntityRow& row : rows) {
                if (!filter.empty() && Lower(row.name).find(filter) == std::string::npos &&
                    Lower(row.kind).find(filter) == std::string::npos) {
                    continue;
                }
                out.push_back(EntityLine(row));
            }
            if (out.empty()) return rows.empty() ? "no live entities" : "no entity matches";
            out.push_back("(" + std::to_string(out.size()) + " shown of " +
                           std::to_string(rows.size()) + ")");
            return Lines(out);
        });

    add("entinfo", "entinfo <index>", "entities", "everything known about one live entity",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: entinfo <index>";
            std::vector<StatGroup> groups;
            host.Inspect("entity:" + args[0], groups);
            return RenderGroups(groups);
        });

    add("spawn", "spawn <script|typeId> [count] [distance]", "entities",
        "spawn creatures in front of you through the real Level.CreateEntity path",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                return "usage: spawn <script.s|typeId> [count] [distance]\n"
                       "  e.g. spawn arat.s 3        spawn 1013 1 600";
            }
            int count = 1;
            float distance = 300.0f;
            if (args.size() >= 2 && !ParseInt(args[1], count)) return "bad count";
            if (args.size() >= 3 && !ParseFloat(args[2], distance)) return "bad distance";
            std::string message;
            host.Spawn(args[0], count, distance, message);
            return message;
        },
        catalogCompleter("scripts"));

    add("killall", "killall [filter]", "entities", "kill every live creature (or those matching)",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::string message;
            host.KillAll(args.empty() ? std::string() : args[0], message);
            return message;
        });

    // -------------------------------------------------------------- items

    add("give", "give <script|typeId> [count]", "items",
        "add an item to the inventory through the real item-creation path",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: give <item script|typeId> [count]";
            int count = 1;
            if (args.size() >= 2 && !ParseInt(args[1], count)) return "bad count";
            std::string message;
            host.Give(args[0], count, message);
            return message;
        },
        catalogCompleter("items"));

    add("equip", "equip <name> [l|r]", "items", "equip a carried item into a hand",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: equip <name substring> [l|r]";
            int hand = 0;
            if (args.size() >= 2) hand = Lower(args[1]) == "l" ? 1 : 2;
            std::string message;
            host.Equip(args[0], hand, message);
            return message;
        });

    add("unequip", "unequip <name>", "items", "take an item out of its hand",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: unequip <name substring>";
            std::string message;
            host.Equip(args[0], -1, message);
            return message;
        });

    add("inv", "inv [filter]", "items", "list the inventory with equip state",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::vector<StatGroup> groups;
            host.Inspect(args.empty() ? "inventory" : "inventory:" + args[0], groups);
            return RenderGroups(groups);
        });

    add("items", "items [filter]", "items", "list every item script the game ships",
        [&host](const std::vector<std::string>& args) -> std::string {
            return Columnize(host.Catalog("items", args.empty() ? std::string() : args[0]), 4);
        });

    add("catalog", "catalog <kind> [filter]", "items",
        "search any static data table (zones, items, entities, scripts, models, sounds)",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                return "kinds: " + Join(host.CatalogKinds(), " ");
            }
            return Columnize(host.Catalog(args[0], args.size() > 1 ? args[1] : std::string()), 3);
        },
        [&host](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            return argIndex == 0 ? host.CatalogKinds() : std::vector<std::string>();
        });

    // ---------------------------------------------------------- character

    add("stats", "stats", "character", "the full player readout: attributes, vitals, derived",
        [&host](const std::vector<std::string>&) -> std::string {
            std::vector<StatGroup> groups;
            host.Inspect("player", groups);
            return RenderGroups(groups);
        });

    // `stat` is the one place a curated command earns its keep over the raw
    // native bridge: the eight attributes have paired Get/Set natives whose
    // names are not quite mechanical (GetWil vs SetWillpower), so mapping a
    // friendly name onto the right pair here saves looking it up every time.
    add("stat", "stat <name> [value]", "character",
        "read or set an attribute (str int wil agi spd end per luck) via the real natives",
        [&host](const std::vector<std::string>& args) -> std::string {
            static const struct {
                const char* alias;
                const char* getter;
                const char* setter;
            } kAttributes[] = {
                {"str", "GetStrength", "SetStrength"},
                {"int", "GetIntelligence", "SetIntelligence"},
                {"wil", "GetWil", "SetWillpower"},
                {"agi", "GetAgility", "SetAgility"},
                {"spd", "GetSpeed", "SetSpeed"},
                {"end", "GetEndurance", "SetEndurance"},
                {"per", "GetPersonality", "SetPersonality"},
                {"luck", "GetLuck", "SetLuck"},
            };
            if (args.empty()) {
                std::vector<std::string> out;
                for (const auto& attribute : kAttributes) {
                    std::string message;
                    host.CallNative("player", attribute.getter, {}, message);
                    out.push_back(std::string(attribute.alias) + " = " + message);
                }
                return Lines(out);
            }
            const std::string wanted = Lower(args[0]);
            for (const auto& attribute : kAttributes) {
                if (wanted != attribute.alias) continue;
                std::string message;
                if (args.size() >= 2) {
                    std::string before;
                    host.CallNative("player", attribute.getter, {}, before);
                    host.CallNative("player", attribute.setter, {args[1]}, message);
                    // The engine recomputes derived stats (max health,
                    // magicka, attack, defense) in UpdateAttributes, not in
                    // the eight setters -- so a raw SetStrength leaves them
                    // stale. But UpdateAttributes has *two* behaviours
                    // selected by the character's current level
                    // (character_progression.h / FUN_1001fc24), and at level
                    // 1 it **reseeds all eight attributes from race and
                    // sex** -- which silently undoes the value just set.
                    // Found by running exactly this command: `stat str 90`
                    // on a fresh level-1 character reported 50 -> 40. So the
                    // recompute happens only where the game itself does it,
                    // and the level-1 case says plainly what it skipped.
                    std::string level;
                    host.CallNative("player", "GetLevel", {}, level);
                    const bool recompute = level != "1";
                    if (recompute) {
                        std::string ignored;
                        host.CallNative("player", "UpdateAttributes", {"false"}, ignored);
                    }
                    // Read it back rather than trusting the setter: several
                    // real setters clamp, and a silently clamped value is
                    // exactly the kind of thing a debug tool must not hide.
                    std::string after;
                    host.CallNative("player", attribute.getter, {}, after);
                    return std::string(attribute.alias) + ": " + before + " -> " + after +
                            (recompute
                                  ? "  (" + std::string(attribute.setter) + " + UpdateAttributes)"
                                  : "  (" + std::string(attribute.setter) +
                                        " only -- at level 1 UpdateAttributes reseeds every "
                                        "attribute from race and sex, so the derived stats are "
                                        "left stale rather than undoing this)");
                }
                host.CallNative("player", attribute.getter, {}, message);
                return std::string(attribute.alias) + " = " + message;
            }
            return "unknown attribute `" + args[0] +
                    "` -- one of: str int wil agi spd end per luck";
        },
        [](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            return {"str", "int", "wil", "agi", "spd", "end", "per", "luck"};
        });

    add("quests", "quests", "character", "every quest id the player has touched, with its state",
        [&host](const std::vector<std::string>&) -> std::string {
            std::vector<StatGroup> groups;
            host.Inspect("quests", groups);
            return RenderGroups(groups);
        });

    add("quest", "quest <id> [assigned|solved|completed] [0|1]", "character",
        "read or set one quest's three real state flags",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: quest <id> [assigned|solved|completed] [0|1]";
            int id = 0;
            if (!ParseInt(args[0], id)) return "bad quest id";
            if (args.size() == 1) {
                std::vector<std::string> out;
                for (const char* state : {"Assigned", "Solved", "Completed"}) {
                    std::string message;
                    host.CallNative("player", std::string("Quest") + state, {args[0]}, message);
                    out.push_back(std::string(state) + ": " + message);
                }
                return Lines(out);
            }
            const std::string which = Lower(args[1]);
            std::string setter;
            if (which == "assigned") setter = "SetQuestAssigned";
            else if (which == "solved") setter = "SetQuestSolved";
            else if (which == "completed") setter = "SetQuestCompleted";
            else return "unknown state `" + args[1] + "` (assigned|solved|completed)";
            const std::string value = args.size() >= 3 ? args[2] : "1";
            std::string message;
            host.CallNative("player", setter, {args[0], value}, message);
            std::vector<std::string> out;
            for (const char* state : {"Assigned", "Solved", "Completed"}) {
                std::string current;
                host.CallNative("player", std::string("Quest") + state, {args[0]}, current);
                out.push_back(std::string(state) + ": " + current);
            }
            return "quest " + args[0] + " -- " + Join(out, "  ");
        },
        [](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 1) return {};
            return {"assigned", "solved", "completed"};
        });

    add("race", "race [id]", "character", "read or set the race (ChooseRace, the real native)",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::string message;
            if (args.empty()) {
                host.CallNative("player", "GetRace", {}, message);
                return "race = " + message + "   (see `catalog races`)";
            }
            host.CallNative("player", "ChooseRace", {args[0]}, message);
            std::string recompute;
            host.CallNative("player", "UpdateAttributes", {"true"}, recompute);
            std::string after;
            host.CallNative("player", "GetRace", {}, after);
            return "race = " + after + "  (ChooseRace + UpdateAttributes -- note the level-1 "
                    "path reseeds all eight attributes from race and sex)";
        },
        catalogCompleter("races"));

    add("class", "class [id]", "character",
        "read or set the character class (ChooseCharacter, the real native)",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::string message;
            if (args.empty()) {
                host.CallNative("player", "GetCharacter", {}, message);
                return "class = " + message + "   (see `catalog classes`)";
            }
            host.CallNative("player", "ChooseCharacter", {args[0]}, message);
            std::string recompute;
            host.CallNative("player", "UpdateAttributes", {"true"}, recompute);
            std::string after;
            host.CallNative("player", "GetCharacter", {}, after);
            return "class = " + after + "  (ChooseCharacter + UpdateAttributes)";
        },
        catalogCompleter("classes"));

    add("level", "level [n]", "character", "read or set the character level",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::string message;
            if (args.empty()) {
                host.CallNative("player", "GetLevel", {}, message);
                return "level = " + message;
            }
            host.CallNative("player", "SetLevel", {args[0]}, message);
            std::string recompute;
            host.CallNative("player", "UpdateAttributes", {"true"}, recompute);
            std::string after;
            host.CallNative("player", "GetLevel", {}, after);
            return "level = " + after + "  (SetLevel + UpdateAttributes)";
        });

    add("xp", "xp [amount]", "character", "read experience, or grant some (AddExperience)",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::string message;
            if (args.empty()) {
                host.CallNative("player", "GetExperience", {}, message);
                std::string toNext;
                host.CallNative("player", "GetExpToNextLevel", {}, toNext);
                return "experience = " + message + ", to next level = " + toNext;
            }
            host.CallNative("player", "AddExperience", {args[0]}, message);
            std::string after, toNext, level;
            host.CallNative("player", "GetExperience", {}, after);
            host.CallNative("player", "GetExpToNextLevel", {}, toNext);
            host.CallNative("player", "GetLevel", {}, level);
            return "experience = " + after + ", to next level = " + toNext + ", level = " + level;
        });

    add("gold", "gold [amount]", "character", "read gold, or set it (SetGold)",
        [&host](const std::vector<std::string>& args) -> std::string {
            std::string message;
            if (args.empty()) {
                host.CallNative("player", "GetGold", {}, message);
                return "gold = " + message;
            }
            host.CallNative("player", "SetGold", {args[0]}, message);
            std::string after;
            host.CallNative("player", "GetGold", {}, after);
            return "gold = " + after;
        });

    add("heal", "heal", "character", "restore health, magicka and fatigue to their maxima",
        [&host](const std::vector<std::string>&) -> std::string {
            // Read each maximum through its real getter and write it back
            // through its real setter, rather than reaching into the vitals
            // directly: SetHealth/SetMagicka/SetFatigue are the natives the
            // game's own rest and potion scripts use, clamping included.
            static const struct {
                const char* getter;
                const char* setter;
            } kVitals[] = {
                {"GetMaxHealth", "SetHealth"},
                {"GetMaxMagicka", "SetMagicka"},
                {"GetMaxFatigue", "SetFatigue"},
            };
            std::vector<std::string> out;
            for (const auto& vital : kVitals) {
                std::string maximum;
                host.CallNative("player", vital.getter, {}, maximum);
                std::string message;
                host.CallNative("player", vital.setter, {maximum}, message);
                out.push_back(std::string(vital.setter) + "(" + maximum + ")");
            }
            return Lines(out);
        });

    // ------------------------------------------------------------- toggles

    add("flags", "flags", "toggles", "list every debug toggle and its value",
        [&host](const std::vector<std::string>&) -> std::string {
            std::vector<std::string> out;
            for (const DebugFlag& flag : host.Flags()) {
                out.push_back(flag.name + " = " + std::to_string(flag.value) + "   " + flag.help);
            }
            return out.empty() ? "(no flags)" : Lines(out);
        });

    add("set", "set <flag> [value]", "toggles", "set a debug toggle (no value = flip it)",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "usage: set <flag> [value]   (`flags` lists them)";
            int value = -1;  // -1 = toggle
            if (args.size() >= 2 && !ParseInt(args[1], value)) return "bad value";
            if (args.size() < 2) {
                for (const DebugFlag& flag : host.Flags()) {
                    if (Lower(flag.name) == Lower(args[0])) {
                        value = flag.value ? 0 : 1;
                        break;
                    }
                }
                if (value < 0) value = 1;
            }
            std::string message;
            host.SetFlag(args[0], value, message);
            return message;
        },
        [&host](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            std::vector<std::string> names;
            for (const DebugFlag& flag : host.Flags()) names.push_back(flag.name);
            return names;
        });

    // ---------------------------------------------------- the native bridge

    add("recv", "recv", "native", "list the receivers `call` and `sk` accept",
        [&host](const std::vector<std::string>&) -> std::string {
            std::vector<std::string> out;
            for (const auto& entry : host.Receivers()) {
                out.push_back(entry.first + "   " + entry.second);
            }
            return Lines(out);
        });

    add("call", "call <receiver> <Method> [args...]", "native",
        "invoke any real native binding -- the same call path the shipped scripts take",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) {
                return "usage: call <receiver> <Method> [args...]\n"
                       "  e.g. call player SetGold 5000\n"
                       "       call player SetQuestAssigned 12 1\n"
                       "       call level CreateEntity 1013\n"
                       "  `recv` lists receivers; docs/SIMKIN_NATIVE_API.md lists the 702 natives.";
            }
            std::string message;
            host.CallNative(args[0], args[1],
                             std::vector<std::string>(args.begin() + 2, args.end()), message);
            return message;
        },
        [&host](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            std::vector<std::string> names;
            for (const auto& entry : host.Receivers()) names.push_back(entry.first);
            return names;
        });

    add("sk", "sk <receiver> <simkin code...>", "native",
        "run a fragment of real Simkin against a receiver, on the game's own interpreter",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.size() < 2) {
                return "usage: sk <receiver> <code>\n"
                       "  e.g. sk player  SetGold(GetGold() + 1000);\n"
                       "  A bare expression works too: sk player GetStrength()";
            }
            std::string message;
            host.ScriptEval(args[0], Rest(args, 1), message);
            return message;
        },
        [&host](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            std::vector<std::string> names;
            for (const auto& entry : host.Receivers()) names.push_back(entry.first);
            return names;
        });

    // ------------------------------------------------------- diagnostics

    add("page", "page [name]", "diagnostics", "choose the on-screen stat panel (empty = off)",
        [&host, &overlay](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                overlay.SetPage(std::string());
                std::vector<std::string> pages = host.Pages();
                for (const std::string& page : DebugOverlay::BuiltInPages()) pages.push_back(page);
                return "overlay off. pages: " + Join(pages, " ");
            }
            overlay.SetPage(args[0]);
            return "page " + args[0];
        },
        [&host](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            std::vector<std::string> pages = host.Pages();
            for (const std::string& page : DebugOverlay::BuiltInPages()) pages.push_back(page);
            return pages;
        });

    add("inspect", "inspect <page>", "diagnostics", "print one stat page into the console",
        [&host](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) return "pages: " + Join(host.Pages(), " ");
            std::vector<StatGroup> groups;
            host.Inspect(args[0], groups);
            return RenderGroups(groups);
        },
        [&host](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            return argIndex == 0 ? host.Pages() : std::vector<std::string>();
        });

    add("watch", "watch [command...]", "diagnostics",
        "re-run a command every frame and show its first line on the watch page",
        [&overlay](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                if (overlay.watches().empty()) return "(no watches)";
                std::vector<std::string> out;
                for (size_t i = 0; i < overlay.watches().size(); ++i) {
                    out.push_back(std::to_string(i) + "  " + overlay.watches()[i]);
                }
                return Lines(out);
            }
            overlay.AddWatch(Join(args, " "));
            overlay.SetPage("watch");
            return "watching `" + Join(args, " ") + "`";
        });

    add("unwatch", "unwatch [index]", "diagnostics", "remove one watch, or all of them",
        [&overlay](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                overlay.ClearWatches();
                return "all watches cleared";
            }
            int index = 0;
            if (!ParseInt(args[0], index)) return "bad index";
            return overlay.RemoveWatch(static_cast<size_t>(index)) ? "removed" : "no such watch";
        });

    add("mark", "mark", "diagnostics",
        "snapshot every counter -- then do the thing, then run `diff`",
        [](const std::vector<std::string>&) -> std::string {
            Metrics::Get().Mark();
            return "marked at frame " + std::to_string(Metrics::Get().frame()) +
                    ". Do the thing, then `diff`.";
        });

    add("diff", "diff", "diagnostics", "every counter that moved since `mark`, largest first",
        [](const std::vector<std::string>&) -> std::string {
            const Metrics& metrics = Metrics::Get();
            const std::vector<CounterDelta> deltas = metrics.Diff();
            if (deltas.empty()) {
                return "nothing moved since the mark (frame " +
                        std::to_string(metrics.markFrame()) + ", " +
                        std::to_string(metrics.frame() - metrics.markFrame()) + " frames ago)";
            }
            std::vector<std::string> out;
            out.push_back(std::to_string(metrics.frame() - metrics.markFrame()) +
                           " frames since mark:");
            size_t widest = 0;
            for (const CounterDelta& delta : deltas) {
                widest = (std::max)(widest, delta.name.size());
            }
            for (const CounterDelta& delta : deltas) {
                out.push_back("  " + delta.name + std::string(widest - delta.name.size() + 2, ' ') +
                               "+" + std::to_string(delta.delta()) + "   (total " +
                               std::to_string(delta.after) + ")");
            }
            return Lines(out);
        });

    add("metrics", "metrics [filter]", "diagnostics", "every counter and timing sample",
        [](const std::vector<std::string>& args) -> std::string {
            const Metrics& metrics = Metrics::Get();
            const std::string filter = args.empty() ? std::string() : Lower(args[0]);
            std::vector<std::string> out;
            out.push_back("frame " + std::to_string(metrics.frame()) + "   fps " +
                           Number(metrics.framesPerSecond(), 2) + "   frame ms " +
                           Number(metrics.frameMs(), 2) + "   uptime " +
                           Number(metrics.elapsedSeconds(), 1) + "s");
            for (const std::string& name : metrics.sampleNames()) {
                if (!filter.empty() && Lower(name).find(filter) == std::string::npos) continue;
                out.push_back("  " + name + " ms  avg " + Number(metrics.averageMs(name), 3));
            }
            std::vector<std::string> names = metrics.counterNames();
            size_t widest = 0;
            for (const std::string& name : names) widest = (std::max)(widest, name.size());
            for (const std::string& name : names) {
                if (!filter.empty() && Lower(name).find(filter) == std::string::npos) continue;
                out.push_back("  " + name + std::string(widest - name.size() + 2, ' ') +
                               "total " + std::to_string(metrics.total(name)) + "   last frame " +
                               std::to_string(metrics.lastFrame(name)));
            }
            return Lines(out);
        });

    add("events", "events [count] [category]", "diagnostics",
        "the event ring, newest first (categories: script, script-error, softfail, world, debug)",
        [](const std::vector<std::string>& args) -> std::string {
            int count = 30;
            if (!args.empty() && !ParseInt(args[0], count)) return "bad count";
            const std::string category = args.size() > 1 ? args[1] : std::string();
            const std::vector<LogEvent> events =
                Metrics::Get().RecentEvents(static_cast<size_t>((std::max)(1, count)), category);
            if (events.empty()) return "(no events)";
            std::vector<std::string> out;
            for (const LogEvent& event : events) {
                out.push_back("f" + std::to_string(event.frame) + "  " + event.category + "  " +
                               event.text);
            }
            return Lines(out);
        });

    add("categories", "categories", "diagnostics", "how many events each category has produced",
        [](const std::vector<std::string>&) -> std::string {
            std::vector<std::string> out;
            for (const auto& entry : Metrics::Get().categoryCounts()) {
                out.push_back(entry.first + "  " + std::to_string(entry.second));
            }
            return out.empty() ? "(none)" : Lines(out);
        });

    add("eventfilter", "eventfilter [substring]", "diagnostics",
        "filter what the events overlay page shows (no argument clears it)",
        [&overlay](const std::vector<std::string>& args) -> std::string {
            overlay.SetEventFilter(args.empty() ? std::string() : args[0]);
            return args.empty() ? "event filter cleared" : "event filter: " + args[0];
        });

    add("clearevents", "clearevents", "diagnostics", "empty the event ring",
        [](const std::vector<std::string>&) -> std::string {
            Metrics::Get().ClearEvents();
            return "events cleared";
        });

    add("trace", "trace [off|statements|methods] [filter]", "diagnostics",
        "script tracing: count statements, or log every script method call as it runs",
        [&tracer](const std::vector<std::string>& args) -> std::string {
            if (args.empty()) {
                return std::string("script tracing is ") + ScriptTracer::LevelName(tracer.level()) +
                        (tracer.filter().empty() ? std::string()
                                                  : "  filter `" + tracer.filter() + "`") +
                        "\n  levels: off | statements | methods\n"
                        "  `methods` logs every script method call -- use a filter, it is loud.";
            }
            const std::string wanted = Lower(args[0]);
            if (wanted == "off") tracer.SetLevel(ScriptTracer::Level::Off);
            else if (wanted == "statements") tracer.SetLevel(ScriptTracer::Level::Statements);
            else if (wanted == "methods") tracer.SetLevel(ScriptTracer::Level::MethodCalls);
            else return "unknown level `" + args[0] + "` (off|statements|methods)";
            tracer.SetFilter(args.size() > 1 ? args[1] : std::string());
            return std::string("script tracing: ") + ScriptTracer::LevelName(tracer.level()) +
                    (tracer.filter().empty() ? std::string() : "  filter `" + tracer.filter() + "`");
        },
        [](int argIndex, const std::vector<std::string>&) -> std::vector<std::string> {
            if (argIndex != 0) return {};
            return {"off", "statements", "methods"};
        });

    add("softfails", "softfails [count]", "diagnostics",
        "recent soft-failed native calls -- the natives this port has not implemented yet",
        [](const std::vector<std::string>& args) -> std::string {
            int count = 30;
            if (!args.empty() && !ParseInt(args[0], count)) return "bad count";
            const std::vector<LogEvent> events =
                Metrics::Get().RecentEvents(static_cast<size_t>((std::max)(1, count)), "softfail");
            if (events.empty()) {
                return "no soft-fails recorded (total " +
                        std::to_string(Metrics::Get().total("script.softfails")) + ")";
            }
            std::vector<std::string> out;
            for (const LogEvent& event : events) {
                out.push_back("f" + std::to_string(event.frame) + "  " + event.text);
            }
            out.push_back("(total soft-fails this session: " +
                           std::to_string(Metrics::Get().total("script.softfails")) + ")");
            return Lines(out);
        });

    add("mini", "mini [0|1]", "diagnostics", "toggle the one-line fps/position readout",
        [&overlay](const std::vector<std::string>& args) -> std::string {
            int value = overlay.miniBar() ? 0 : 1;
            if (!args.empty() && !ParseInt(args[0], value)) return "bad value";
            overlay.SetMiniBar(value != 0);
            return overlay.miniBar() ? "mini bar on" : "mini bar off";
        });
}

}  // namespace sk_debug
