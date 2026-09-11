// M92 smoke test: the `0x14d08` Object/Entity base, on every receiver.
//
// The milestone has two halves and they are the same defect seen twice.
// Six bindings of the base class were unimplemented anywhere (`ShowEntity`
// 40 sites, `MirrorMethod` 33, `GetID` 9, `RunScript` 7, `SetModel` 3,
// `SetRotationTurn` 2); three more existed but only on some of the classes
// that inherit them (`SetName` on Item and Monster but not Door,
// `SetPassable` on Door but not Monster, `PlaySound` on Monster and Player
// but not Item). Both halves are answered by putting the engine's one
// implementation on this port's one mixin.
//
// What each part checks, and why it is the check that could fail:
//
//   1. The shipped corpus, counted from the .s files themselves: how many
//      sites each of the nine names has and which receivers they land on.
//      This is the number the census in docs/PORT_ROADMAP.md quotes, so a
//      drift in either direction is worth knowing about.
//   2. Every receiver answers every name. A real door, a real world item, a
//      real creature and the player are each handed all nine natives with
//      the soft-fail observer armed; nothing may soft-fail. This part is
//      the milestone -- a name implemented on three classes out of four is
//      the bug, not a partial fix.
//   3. A real scripted set-piece end to end: `stouttp.s` hides Old Trinket
//      on zone Init() and his own `ShowOG[]` handler brings him back with
//      ShowEntity + SetUsable + MirrorMethod. Plus crypt2's seven-crystal
//      puzzle, which is GetID and the same three again at scale.
//   4. `lakvan/sdoor_trapa.s`, one script shared by four placements that
//      tells them apart with GetID() -- checked against the four names in
//      the real `lakvan.ent`.
//   5. `RunScript`: `spells/u_blaze_lvl10.s` really becomes `blaze.s`,
//      keeping what its own Init() set and gaining what the base script's
//      Init() sets.
//   6. `SetModel` against the real `models.txt`: Volstok turns into a
//      zombie and Ivgrizt is a goblin.
//   7. `ShowEntity` and `SetPassable` as the corpus actually pairs them,
//      and `SetName`'s string form being rejected the way the engine
//      rejects it.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/door_executable.h"
#include "simkin_bindings/entity_base_ref.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/native_binding_common.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "skTreeNode.h"

namespace sk_b = sk_bindings;

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("  %-86s %s\n", what.c_str(), ok ? "ok" : "FAILED");
    if (!ok) ++g_failures;
}

// The nine names this milestone is about.
const char* kBaseNames[] = {"ShowEntity", "MirrorMethod", "GetID",     "RunScript", "SetModel",
                            "SetRotationTurn", "SetName",  "SetPassable", "PlaySound"};

// ---- the soft-fail observer, so Part 2 can prove a name stopped missing.
std::vector<std::string> g_softFails;
void RecordSoftFail(const char* object, const char* methodName, const char* /*args*/) {
    g_softFails.push_back(std::string(object ? object : "?") + "." +
                          (methodName ? methodName : "?"));
}

// ---- corpus scanning ---------------------------------------------------

// One call site: which of the nine, and what was written before the dot.
// "" means a bare call inside the entity's own script (an entity acting on
// itself), which is the shape most of these have.
struct Site {
    std::string name;
    std::string receiver;
};

bool IsIdentChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

void CollectSites(const std::filesystem::path& file, std::vector<Site>& out) {
    std::ifstream in(file);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        // Comment-stripped: the corpus has ~20 commented-out `//SetModel(202)`
        // lines and five commented-out `//MirrorMethod("ReallyPicked")`, and
        // a dead line is not a call site.
        size_t comment = line.find("//");
        if (comment != std::string::npos) line = line.substr(0, comment);
        for (const char* name : kBaseNames) {
            const size_t len = std::strlen(name);
            size_t from = 0;
            while (true) {
                size_t at = line.find(name, from);
                if (at == std::string::npos) break;
                from = at + 1;
                // Whole-word, and followed by '(' (possibly after spaces --
                // `umbra_keth.s` writes `ShowEntity( false )`).
                if (at > 0 && IsIdentChar(line[at - 1]) && line[at - 1] != '.') continue;
                size_t open = at + len;
                while (open < line.size() && line[open] == ' ') ++open;
                if (open >= line.size() || line[open] != '(') continue;

                Site site;
                site.name = name;
                if (at > 0 && line[at - 1] == '.') {
                    size_t end = at - 1;
                    size_t start = end;
                    // Walk back over `Level.GetEntity("x")` / `GetOpener()`
                    // / a plain local, whichever it is.
                    while (start > 0 && (IsIdentChar(line[start - 1]) || line[start - 1] == '.' ||
                                          line[start - 1] == ')' || line[start - 1] == '(' ||
                                          line[start - 1] == '"' || line[start - 1] == '\\')) {
                        --start;
                    }
                    site.receiver = line.substr(start, end - start);
                }
                out.push_back(std::move(site));
            }
        }
    }
}

// Loads every .s file under the script root.
std::vector<std::filesystem::path> AllScripts(const std::string& root) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(root, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && it->path().extension() == ".s") files.push_back(it->path());
    }
    return files;
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// models.txt row -> model file name, for Part 6.
std::map<int, std::string> LoadModelNames(const std::string& root) {
    std::map<int, std::string> names;
    std::ifstream in(root + "/models.txt");
    std::string line;
    while (std::getline(in, line)) {
        int index = 0, blocks = 0, hx = 0, hy = 0;
        char name[128] = {};
        if (std::sscanf(line.c_str(), "%d %d %d %d %127s", &index, &blocks, &hx, &hy, name) == 5) {
            names[index] = name;
        }
    }
    return names;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";
    const std::string root = scriptRoot;

    sk::StringTable strings;
    if (!strings.Load(root + "/stringtable.eng")) {
        std::printf("m92_entity_base_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1: the shipped corpus.
    // ---------------------------------------------------------------
    std::printf("\n== Part 1: what the shipped scripts actually ask for ==\n");
    std::vector<Site> sites;
    {
        const std::vector<std::filesystem::path> files = AllScripts(root);
        Check(files.size() > 1400,
              "the whole script corpus is readable (" + std::to_string(files.size()) + " .s files)");
        for (const std::filesystem::path& f : files) CollectSites(f, sites);

        std::map<std::string, int> perName;
        for (const Site& s : sites) ++perName[s.name];
        for (const char* name : kBaseNames) {
            std::printf("     %-16s %3d sites\n", name, perName[name]);
        }

        // The six that were unimplemented anywhere. These are the census
        // numbers in docs/PORT_ROADMAP.md, and the tool that produced them
        // counts the same way.
        Check(perName["ShowEntity"] == 40, "ShowEntity has 40 live call sites, the most of the six");
        Check(perName["MirrorMethod"] == 33, "MirrorMethod has 33 (five more are commented out)");
        Check(perName["GetID"] == 9, "GetID has 9");
        Check(perName["RunScript"] == 7, "RunScript has 7, every one of them a spell");
        Check(perName["SetModel"] == 3, "SetModel has 3 live sites (~20 more are commented out)");
        Check(perName["SetRotationTurn"] == 2, "SetRotationTurn has 2, both aimed at Trothgar");
        // And the three that existed on the wrong receivers.
        Check(perName["SetPassable"] > 40,
              "SetPassable is everywhere (" + std::to_string(perName["SetPassable"]) + ")");
        Check(perName["PlaySound"] > 100,
              "PlaySound is the busiest of the lot (" + std::to_string(perName["PlaySound"]) + ")");

        // A majority being bare self-calls is exactly why this has to live
        // on the base: a bare call is dispatched on whatever class the
        // script happens to be attached to, which is all four of them.
        int bare = 0;
        std::set<std::string> receivers;
        for (const Site& s : sites) {
            if (s.receiver.empty()) {
                ++bare;
            } else {
                receivers.insert(s.receiver);
            }
        }
        std::printf("     %d bare self-calls, %zu distinct explicit receivers\n", bare,
                    receivers.size());
        Check(bare > 100, "most sites are bare self-calls -- the receiver is whatever the script is");
        Check(receivers.count("Level.GetEntity(\"crys1\")") == 1,
              "a named prop is reached with Level.GetEntity(...) (crypt2's crystals)");
        Check(receivers.count("GetOpener()") == 1,
              "a chest reaches its opener with GetOpener() (delfhide/chest_locket_convo.s)");
    }

    // ---------------------------------------------------------------
    // Part 2: every receiver answers every name.
    //
    // This is the milestone. Before it, each of these four classes had its
    // own private subset and the gaps were invisible to both coverage
    // tools -- only the soft-fail log could see them, which is how
    // `Door.SetName` (23 lines), `Monster.SetPassable` (12) and
    // `Item.PlaySound` (7) were found.
    // ---------------------------------------------------------------
    std::printf("\n== Part 2: all nine bindings, on all four receivers ==\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);

        std::unique_ptr<sk_b::DoorExecutable> door;
        std::unique_ptr<sk_b::ItemExecutable> item;
        std::unique_ptr<sk_b::MonsterExecutable> monster;
        try {
            door = std::make_unique<sk_b::DoorExecutable>(skString((root + "/door.s").c_str()),
                                                           loadCtxt, stack.player());
            item = std::make_unique<sk_b::ItemExecutable>(
                skString((root + "/items/bread.s").c_str()), loadCtxt, stack);
            monster = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/monsters/azra_rat.s").c_str()), loadCtxt, &strings,
                stack.player(), stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        } catch (skRuntimeException& e) {
            std::printf("   (RUNTIME ERROR: %s)\n", e.toString().ptr());
        }
        Check(door && item && monster, "a real door, item and creature all load");

        if (door && item && monster) {
            struct Receiver {
                const char* label;
                skiExecutable* object;
            };
            const Receiver receivers[] = {
                {"Door", door.get()},
                {"Item", item.get()},
                {"Monster", monster.get()},
                {"Player", &stack.player()},
            };

            // An argument list that is valid for each name, so nothing is
            // rejected on arity instead of on the name.
            auto argsFor = [](const std::string& name, skRValueArray& args) {
                if (name == "GetID") return;
                if (name == "ShowEntity" || name == "SetPassable") {
                    args.append(skRValue(true));
                } else if (name == "MirrorMethod") {
                    args.append(skRValue(skString("ClientShowCrystal1")));
                } else if (name == "RunScript") {
                    args.append(skRValue(skString("Blaze")));
                } else {
                    args.append(skRValue(1));
                }
            };

            for (const Receiver& r : receivers) {
                std::vector<std::string> missing;
                for (const char* name : kBaseNames) {
                    g_softFails.clear();
                    sk_b::SetSoftFailObserver(&RecordSoftFail);
                    skRValueArray args;
                    argsFor(name, args);
                    skRValue ret;
                    skExecutableContext ctxt(&interpreter);
                    bool handled = false;
                    try {
                        handled = r.object->method(skString(name), args, ret, ctxt);
                    } catch (skRuntimeException& e) {
                        std::printf("     (RUNTIME ERROR in %s.%s: %s)\n", r.label, name,
                                    e.toString().ptr());
                    }
                    sk_b::SetSoftFailObserver(nullptr);
                    if (!handled || !g_softFails.empty()) missing.push_back(name);
                }
                std::string detail;
                for (const std::string& m : missing) detail += " " + m;
                Check(missing.empty(), std::string(r.label) +
                                           " answers all nine base bindings" +
                                           (missing.empty() ? "" : " -- missing:" + detail));
            }

            // The RunScript() the loop just made on each receiver is a
            // *recorded request*, not an applied swap -- deliberately, see
            // entity_base_ref.h. Drain them so nothing below inherits one.
            std::string pending;
            Check(item->TakePendingScript(pending) && pending == "Blaze",
                  "RunScript records the request rather than swapping under the interpreter");
            door->TakePendingScript(pending);
            monster->TakePendingScript(pending);
            stack.player().TakePendingScript(pending);

            // MirrorMethod is a no-op here and that is correct -- but it is
            // a no-op that *ran*, which is the distinction the counter
            // exists to make.
            Check(door->mirroredMethodCount() == 1 && item->mirroredMethodCount() == 1 &&
                      monster->mirroredMethodCount() == 1,
                  "MirrorMethod ran on each of them and correctly did nothing (single player)");
        }
    }

    // ---------------------------------------------------------------
    // Part 3: a real scripted set-piece, end to end.
    // ---------------------------------------------------------------
    std::printf("\n== Part 3: a real scripted set-piece, end to end ==\n");
    {
        // `stouttp.s`'s Init() ends with
        //     GetEntity("trinket").ShowEntity(false);
        // and the NPC's own `ShowOG[]` handler is
        //     ShowEntity(true); SetUsable(true); MirrorMethod("ShowOG");
        // -- three of this milestone's bindings in one three-line handler,
        // and the reason the census called ShowEntity "the one with visible
        // consequences": until now Old Trinket stood in the Stout Trapping
        // Post from the moment the zone loaded, offering his conversation,
        // and the line that is supposed to reveal him did nothing.
        const std::string zone = ReadFile(root + "/stouttp.s");
        Check(zone.find("GetEntity(\"trinket\").ShowEntity(false);") != std::string::npos,
              "stouttp.s hides \"trinket\" on zone Init()");

        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::MonsterExecutable> trinket;
        try {
            trinket = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/stouttp/oldtrinket.s").c_str()), loadCtxt, &strings,
                stack.player(), stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(trinket != nullptr, "stouttp/oldtrinket.s loads");
        if (trinket) {
            skExecutableContext ctxt(&interpreter);
            trinket->SetEntityId("trinket");
            sk_b::RunEntityInit(*trinket, stack.scriptRoot(), ctxt);
            Check(!trinket->usable(), "his Init() turns himself off -- SetUsable(false)");
            Check(!trinket->entityHidden(), "  but a placement is visible until something hides it");

            // What the zone script does.
            skRValue ret;
            skRValueArray hide;
            hide.append(skRValue(false));
            trinket->method(skString("ShowEntity"), hide, ret, ctxt);
            Check(trinket->entityHidden() && trinket->outOfWorld(),
                  "ShowEntity(false) takes him out of the world -- vtable[0x58](this, 1)");

            // And what the conversation does.
            skRValueArray none;
            try {
                trinket->method(skString("ShowOG"), none, ret, ctxt);
            } catch (skRuntimeException& e) {
                std::printf("   (RUNTIME ERROR in ShowOG: %s)\n", e.toString().ptr());
            }
            Check(!trinket->entityHidden() && !trinket->outOfWorld(),
                  "ShowOG's ShowEntity(true) puts him back -- the argument is inverted");
            Check(trinket->usable(), "  and its SetUsable(true) lets him be talked to");
            Check(trinket->mirroredMethodCount() == 1,
                  "  and its MirrorMethod(\"ShowOG\") ran and correctly did nothing");
        }

        // crypt2's seven crystals are the same three bindings again, at
        // scale -- and the one place GetID() and ShowEntity share a handler.
        const std::string pedestal = ReadFile(root + "/crypt2/pedestal_entity.s");
        Check(pedestal.find("who = GetID();") != std::string::npos,
              "crypt2/pedestal_entity.s opens by reading its own placement name");
        Check(pedestal.find("Level.GetEntity(\"crys1\").ShowEntity(true);") != std::string::npos,
              "  and shows a crystal by name once the matching pedestal is used");
        Check(pedestal.find("MirrorMethod(\"ClientShowCrystal1\");") != std::string::npos,
              "  and mirrors the reveal, which is a no-op in single player");
    }

    // ---------------------------------------------------------------
    // Part 4: GetID is the .ent placement name.
    // ---------------------------------------------------------------
    std::printf("\n== Part 4: GetID, against the real lakvan.ent ==\n");
    {
        const std::string script = ReadFile(root + "/lakvan/sdoor_trapa.s");
        const std::string ent = ReadFile(root + "/lakvan.ent");
        Check(!ent.empty(), "lakvan.ent is readable");

        // One script, four placements, told apart by GetID().
        const char* kPlacements[] = {"11s", "gg5", "sdoor1", "hh6"};
        int inScript = 0, inEnt = 0;
        for (const char* name : kPlacements) {
            if (script.find(std::string("GetID() = \"") + name + "\"") != std::string::npos) {
                ++inScript;
            }
            if (ent.find(name) != std::string::npos) ++inEnt;
        }
        Check(inScript == 4, "sdoor_trapa.s branches on four different GetID() values");
        Check(inEnt == 4, "  and all four are placement names in lakvan.ent");

        // Which is what the host seeds, before Init().
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::DoorExecutable> sdoor;
        try {
            sdoor = std::make_unique<sk_b::DoorExecutable>(
                skString((root + "/lakvan/sdoor_trapa.s").c_str()), loadCtxt, stack.player());
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(sdoor != nullptr, "sdoor_trapa.s loads as a door");
        if (sdoor) {
            sdoor->SetEntityId("sdoor1");
            skRValueArray none;
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            sdoor->method(skString("GetID"), none, ret, ctxt);
            Check(std::string(ret.str().ptr()) == "sdoor1",
                  "GetID() answers the placement name the host seeded");

            // SetID overrides it, which is the other half of the pair.
            skRValueArray setId;
            setId.append(skRValue(skString("gg5")));
            sdoor->method(skString("SetID"), setId, ret, ctxt);
            sdoor->method(skString("GetID"), none, ret, ctxt);
            Check(std::string(ret.str().ptr()) == "gg5", "  and SetID(name) replaces it");
        }
    }

    // ---------------------------------------------------------------
    // Part 5: RunScript really switches the script.
    // ---------------------------------------------------------------
    std::printf("\n== Part 5: RunScript, on a real upgraded spell ==\n");
    {
        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::ItemExecutable> spell;
        try {
            spell = std::make_unique<sk_b::ItemExecutable>(
                skString((root + "/spells/u_blaze_lvl10.s").c_str()), loadCtxt, stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(spell != nullptr, "spells/u_blaze_lvl10.s loads");
        if (spell) {
            skExecutableContext ctxt(&interpreter);
            sk_b::RunEntityInit(*spell, stack.scriptRoot(), ctxt);

            // u_blaze_lvl10.s's own Init(): SetLevel(10), SetSpellType(50),
            // SetCost(756), SetMarketValue(265), RunScript("Blaze").
            Check(spell->spellLevel() == 10, "the variant's own SetLevel(10) survives the swap");
            Check(spell->spellType() == 50, "  and its SetSpellType(50)");

            // blaze.s's Init(), which only runs if the swap happened:
            // SetName(403), SetIcon(209), SetMarketValue(112), SetRating(2).
            Check(spell->icon() == 209, "blaze.s's own SetIcon(209) ran -- the swap happened");
            Check(spell->rating() == 2, "  and its SetRating(2)");
            Check(spell->marketValue() == 112,
                  "  and its SetMarketValue(112) overwrote the variant's 265, as it does in the "
                  "shipped game");
            Check(spell->name() == strings.Get(403),
                  "  and the item is named by blaze.s's SetName(403): \"" + strings.Get(403) + "\"");

            // The swapped-in tree is the one the object answers from now:
            // blaze.s declares HitTarget, u_blaze_lvl10.s does not.
            skTreeNode* node = spell->getNode();
            Check(node && node->findChild(skString("HitTarget")) != nullptr,
                  "  and blaze.s's HitTarget handler is now reachable on this object");
        }

        // All seven sites, and where each lands.
        int runScriptSites = 0;
        for (const Site& s : sites) {
            if (s.name == "RunScript") ++runScriptSites;
        }
        Check(runScriptSites == 7, "all seven RunScript sites are accounted for");
    }

    // ---------------------------------------------------------------
    // Part 6: SetModel names a models.txt row.
    // ---------------------------------------------------------------
    std::printf("\n== Part 6: SetModel, against the real models.txt ==\n");
    {
        const std::map<int, std::string> models = LoadModelNames(root);
        Check(models.size() == 237, "models.txt has 237 rows (" +
                                        std::to_string(models.size()) + ")");
        Check(models.count(61) && models.at(61) == "goblin.bin",
              "monsters/ivgrizt.s's SetModel(61) is goblin.bin");
        Check(models.count(69) && models.at(69) == "zombie.bin",
              "twilite/volstok_violet.s's SetModel(69) is zombie.bin -- Volstok turns");
        Check(models.count(23) && models.at(23) == "male_short_tunic.bin",
              "twilite/volstok_convo.s's SetModel(23) is male_short_tunic.bin -- and turns back");

        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::MonsterExecutable> ivgrizt;
        try {
            ivgrizt = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/monsters/ivgrizt.s").c_str()), loadCtxt, &strings,
                stack.player(), stack);
        } catch (skParseException& e) {
            std::printf("   (PARSE ERROR: %s)\n", e.toString().ptr());
        }
        Check(ivgrizt != nullptr, "monsters/ivgrizt.s loads");
        if (ivgrizt) {
            Check(ivgrizt->modelOverride() == -1,
                  "before Init() there is no override and the placement's model stands");
            skExecutableContext ctxt(&interpreter);
            sk_b::RunEntityInit(*ivgrizt, stack.scriptRoot(), ctxt);
            Check(ivgrizt->modelOverride() == 61,
                  "after Init() the creature is drawn as models.txt row 61");
        }
    }

    // ---------------------------------------------------------------
    // Part 7: the pairings, and SetName's rejected form.
    // ---------------------------------------------------------------
    std::printf("\n== Part 7: how the corpus actually uses these together ==\n");
    {
        // dstar_w.s and fearfrst.s hide an NPC and make it walk-through in
        // the same breath. That pairing is the whole argument for
        // SetPassable existing on a creature.
        for (const char* rel : {"dstar_w.s", "fearfrst.s"}) {
            const std::string text = ReadFile(root + "/" + rel);
            size_t hides = 0, passables = 0, at = 0;
            while ((at = text.find("ShowEntity(", at)) != std::string::npos) {
                ++hides;
                ++at;
            }
            at = 0;
            while ((at = text.find("SetPassable(", at)) != std::string::npos) {
                ++passables;
                ++at;
            }
            Check(hides > 0 && hides == passables,
                  std::string(rel) + " pairs every ShowEntity with a SetPassable (" +
                      std::to_string(hides) + " each)");
        }

        skInterpreter interpreter;
        sk_b::MenuStack stack(root, interpreter, &strings);
        skExecutableContext loadCtxt(&interpreter);
        std::unique_ptr<sk_b::MonsterExecutable> porliss;
        try {
            porliss = std::make_unique<sk_b::MonsterExecutable>(
                skString((root + "/monsters/azra_rat.s").c_str()), loadCtxt, &strings,
                stack.player(), stack);
        } catch (skParseException&) {
        }
        Check(porliss != nullptr, "a creature loads to drive the pairing");
        if (porliss) {
            skExecutableContext ctxt(&interpreter);
            skRValue ret;
            Check(!porliss->entityPassable(), "a creature starts solid, like a closed door");

            skRValueArray yes;
            yes.append(skRValue(true));
            porliss->method(skString("SetPassable"), yes, ret, ctxt);
            Check(porliss->entityPassable(),
                  "SetPassable(true) makes it walk-through (entity+0xd5)");
            Check(!porliss->outOfWorld(), "  on its own that does not remove it from the world");

            skRValueArray hide;
            hide.append(skRValue(false));
            porliss->method(skString("ShowEntity"), hide, ret, ctxt);
            Check(porliss->outOfWorld(),
                  "ShowEntity(false) does -- outOfWorld() is what main.cpp asks");
            Check(!porliss->destroyed(),
                  "  without being `destroyed`, which is the irreversible one");

            // SetRotationTurn, the scripted facing.
            skRValueArray turn;
            turn.append(skRValue(-26414));
            porliss->method(skString("SetRotationTurn"), turn, ret, ctxt);
            Check(porliss->rotationRaw() == -26414,
                  "SetRotationTurn(-26414) assigns entity+0xb6 (ratherb.s's Trothgar)");
            int drained = 0;
            Check(porliss->TakePendingRotation(drained) && drained == -26414,
                  "  and the host drains it once, so the AI tick does not fight it");
            Check(!porliss->TakePendingRotation(drained), "  and only once");

            // AddRotationTurn accumulates into the same field.
            skRValueArray add;
            add.append(skRValue(16384));
            porliss->method(skString("AddRotationTurn"), add, ret, ctxt);
            Check(porliss->rotationRaw() == -26414 + 16384,
                  "AddRotationTurn accumulates into the same field, as the engine does");

            // SetName's rejected form: the real case puts up "SetName()
            // invalid argument / You must pass in an ID# now" and sets
            // nothing.
            skRValueArray byId;
            byId.append(skRValue(2095));
            porliss->method(skString("SetName"), byId, ret, ctxt);
            Check(porliss->entityNameId() == 2095, "SetName(2095) stores the stringtable id");
            skRValueArray byString;
            byString.append(skRValue(skString("Ivgrizt")));
            porliss->method(skString("SetName"), byString, ret, ctxt);
            Check(porliss->entityNameId() == 2095,
                  "  and SetName(\"...\") is rejected without changing it, as the engine does");
        }
    }

    std::printf("\n%d checks, %d failures -- %s\n", g_checks, g_failures,
                g_failures == 0 ? "OK" : "FAILED");
    return g_failures == 0 ? 0 : 1;
}
