// M81 smoke test: the loot bag a defeated creature drops.
//
// The reported symptom was "enemies are supposed to sometimes drop loot
// bags with random loot; this is missing from the port". Every part of the
// mechanism was present since M21 -- SetLoot() stored the tag, all four
// death paths in main.cpp called spawnLoot(), the bag's own OnUse() opened
// the real lootmenu.s -- and none of it ran, because spawnLoot() built the
// script path by hand as `<root>/Loot_ratseye`, without the `.s`. Simkin
// answers a file it cannot open with an *empty parse*, not an error
// (skInputFile::open leaves a null handle and the reader sees immediate
// eof), so the bag loaded "successfully" as an object with no script at
// all: no Init(), therefore no SetUsable(true) -- so findNearbyPickup()
// skipped it -- and no CreateEntity/AddObject, so it was empty anyway. It
// also carried modelArchiveIndex -1, so the renderer skipped it too. An
// invisible, unusable, empty object, once per kill.
//
// M21's own smoke test could not catch this: it appends the ".s" itself
// (`scriptRoot + "/" + rat->lootTag() + ".s"`) and so exercises a path
// main.cpp never built. That is why the load now lives in
// LevelExecutable::CreateEntityWithScript(), which main.cpp and this test
// both call, rather than being open-coded in main().
//
// Decompiled for this milestone:
//   FUN_10083c04  the creature death handler -- runs OnKilled, then gates
//                 the drop on `monster+0x2f0 != 0`
//   FUN_10084438  the drop itself: spawn `+0x2f0` with `+0x2f4` as the
//                 script override, position it at the creature's x/y with
//                 z + 300, floor-snap, lift 0x80, flag it a container when
//                 its entities.txt category is 8
//   FUN_100715a8  the spawn: `param_3 != 0` loads `param_4` instead of the
//                 descriptor's own name; the `+0x54` model pointer comes
//                 from `engine+0x6b38[descriptor->modelArchiveIndex]`
//                 either way
//   FUN_100686e0  the floor snap (already ported as Zone::SnapActorToGround)
//   0x10084924 case 0xd  SetLoot: arg0 -> +0x2f0, arg1 -> +0x2f4, and the
//                 rand(min,max)!=min bail that discards both
//   0x1006dbec case 0x21  Level.CreateEntityScript: the same spawn, called
//                 from a creature's own OnKilled(), with its own placement
//                 arm (no 0x80 lift; the storey probe is the tile's own
//                 floor-band threshold, not the caller's z)
//
// Part 1  entities.txt row 212, and the model M21 said did not exist
// Part 2  the regression: what the no-extension path actually produced
// Part 3  every SetLoot call site in the shipped corpus, resolved for real
// Part 4  SetLoot stores the typeId too, and the drop chance clears both
// Part 5  the real drop, end to end, through the same call main.cpp makes
// Part 5b the *other* drop: Level.CreateEntityScript, placed by the
//         creature's own OnKilled() rather than by the engine
// Part 6  where the bag lands
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/level_executable.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/monster_executable.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"
#include "world/entity_types.h"
#include "world/zone.h"

namespace {

int g_Checks = 0;
int g_Failed = 0;

void Check(bool cond, const char* what) {
    ++g_Checks;
    if (!cond) ++g_Failed;
    std::printf("  [%s] %s\n", cond ? "OK" : "FAILED", what);
}

// entities.txt line 212: `300 30 8 !bag_loot`.
constexpr int kBagLootTypeId = 300;
constexpr int kContainerCategory = 8;
constexpr int kBagModelIndex = 30;

// The zones that ship a `<zone>_models.txt`, minus the `menu` pseudo-level
// (engine/screen_mode.h -- it has a models.txt but no .zon/.ent, and
// nothing dies there).
const char* const kPlayableZones[] = {
    "azra",         "broken1", "broken2", "crypt1",   "crypt2",   "crypt3",  "delfhide",
    "drgnfld",      "dstar_e", "dstar_w", "erthcave", "fearfrst", "ffarena", "ghstpass",
    "glaciercrawl", "lakvan",  "lothcav", "raiders",  "snowline", "stouttp", "twilite",
};
constexpr int kPlayableZoneCount = static_cast<int>(sizeof(kPlayableZones) / sizeof(*kPlayableZones));

// Column 5 of `<zone>_models.txt` for a given archive index, or "" if the
// row is missing. The file's rows are `index solid halfX halfY name.bin`.
std::string ModelNameAt(const std::string& scriptRoot, const std::string& zone, int index) {
    std::ifstream in(scriptRoot + "/" + zone + "_models.txt");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        int idx = -1, solid = 0, halfX = 0, halfY = 0;
        std::string name;
        if (!(row >> idx >> solid >> halfX >> halfY >> name)) continue;
        if (idx == index) return name;
    }
    return std::string();
}

struct LootCallSite {
    std::string file;  // relative to scriptRoot, for the failure message
    int typeId = 0;
    std::string tag;  // as the interpreter sees it: `\\` already halved
};

// Simkin's own double-quoted-string escape handling (skParser.cpp: `\\`
// collapses, `\n`/`\t` become the control character, anything else after a
// backslash is taken literally). The corpus's subdirectory tags are
// written with four backslashes and arrive here with two.
std::string UnescapeSimkinString(const std::string& raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 == raw.size()) {
            out += raw[i];
            continue;
        }
        char next = raw[++i];
        if (next == 'n') {
            out += '\n';
        } else if (next == 't') {
            out += '\t';
        } else {
            out += next;
        }
    }
    return out;
}

// Every `SetLoot(typeId, "tag", ...)` in the shipped .s corpus. A plain
// scan rather than a transcribed list, so the check keeps meaning if the
// corpus is ever re-extracted.
std::vector<LootCallSite> CollectSetLootCalls(const std::string& scriptRoot) {
    std::vector<LootCallSite> sites;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(scriptRoot, ec);
    if (ec) return sites;
    for (const std::filesystem::directory_entry& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".s") continue;
        std::ifstream in(entry.path());
        std::stringstream buffer;
        buffer << in.rdbuf();
        const std::string text = buffer.str();
        for (size_t at = text.find("SetLoot"); at != std::string::npos;
             at = text.find("SetLoot", at + 1)) {
            size_t open = text.find('(', at);
            size_t close = text.find(')', at);
            if (open == std::string::npos || close == std::string::npos || close < open) continue;
            const std::string args = text.substr(open + 1, close - open - 1);
            size_t q1 = args.find('"');
            size_t q2 = q1 == std::string::npos ? std::string::npos : args.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            LootCallSite site;
            site.file = std::filesystem::relative(entry.path(), scriptRoot, ec).generic_string();
            site.typeId = std::atoi(args.c_str());
            site.tag = UnescapeSimkinString(args.substr(q1 + 1, q2 - q1 - 1));
            sites.push_back(site);
        }
    }
    return sites;
}

bool FileExists(const std::string& path) {
    std::ifstream probe(path);
    return probe.good();
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m81_loot_drop_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }
    sk::EntityTypeTable entityTypes;
    if (!entityTypes.Load(scriptRoot)) {
        std::printf("m81_loot_drop_smoke: FAILED to load entities.txt\n");
        return 1;
    }
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    stack.level().SetEntityTypes(&entityTypes);

    // ---- Part 1: entities.txt row 212, and the model M21 said did not exist ----
    std::printf("\nPart 1 -- typeId 300, the thing a creature actually drops\n");
    {
        const sk::EntityTypeDescriptor* desc = entityTypes.Lookup(kBagLootTypeId);
        Check(desc != nullptr, "entities.txt has a row for typeId 300");
        if (desc) {
            std::printf("    row: %d %d %d %s\n", kBagLootTypeId, desc->modelArchiveIndex,
                        desc->category, desc->name.c_str());
            Check(desc->category == kContainerCategory,
                  "typeId 300's category is 8 (container) -- what FUN_10084438 re-derives before "
                  "flagging the spawned object");
            Check(desc->name == "!bag_loot",
                  "typeId 300's name column is the label-only `!bag_loot` -- there is no script "
                  "here, which is exactly why the spawn takes a script override");
            Check(desc->modelArchiveIndex == kBagModelIndex,
                  "typeId 300's model index is 30 -- M21 recorded this entity as having no model "
                  "at all, reading the `!` name as if it covered the model column too");
        }
        Check(stack.level().EntityCategoryOf(kBagLootTypeId) == kContainerCategory,
              "LevelExecutable::EntityCategoryOf(300) == 8");
        Check(stack.level().EntityModelIndexOf(kBagLootTypeId) == kBagModelIndex,
              "LevelExecutable::EntityModelIndexOf(300) == 30");

        int zonesWithBag = 0, zonesChecked = 0;
        std::string mismatch;
        for (const char* zone : kPlayableZones) {
            std::string name = ModelNameAt(scriptRoot, zone, kBagModelIndex);
            if (name.empty()) continue;
            ++zonesChecked;
            if (name == "bag_dropped.bin") {
                ++zonesWithBag;
            } else if (mismatch.empty()) {
                mismatch = std::string(zone) + " -> " + name;
            }
        }
        std::printf("    models.txt slot 30: %d of %d playable zones name bag_dropped.bin%s%s\n",
                    zonesWithBag, zonesChecked, mismatch.empty() ? "" : ", first mismatch: ",
                    mismatch.c_str());
        Check(zonesChecked == kPlayableZoneCount && zonesWithBag == zonesChecked,
              "every playable zone's models.txt resolves archive index 30 to bag_dropped.bin -- "
              "the bag is a real, loadable model in every zone a creature can die in");
    }

    // ---- Part 2: the regression -- what the no-extension path produced ----
    std::printf("\nPart 2 -- the path main.cpp used to build\n");
    {
        // Exactly what spawnLoot() did before this milestone: concatenate
        // the tag onto the script root and load it.
        std::string handBuilt = std::string(scriptRoot) + "/Loot_ratseye";
        std::unique_ptr<sk_bindings::ItemExecutable> ghost;
        bool threw = false;
        try {
            skExecutableContext loadCtxt(&interpreter);
            ghost = std::make_unique<sk_bindings::ItemExecutable>(skString(handBuilt.c_str()),
                                                                   loadCtxt, stack);
            skRValueArray initArgs;
            initArgs.append(skRValue(0));
            skRValue initRet;
            skExecutableContext callCtxt(&interpreter);
            ghost->method(skString("Init"), initArgs, initRet, callCtxt);
        } catch (skParseException&) {
            threw = true;
        } catch (skRuntimeException&) {
            threw = true;
        }
        Check(!threw && ghost != nullptr,
              "loading `<root>/Loot_ratseye` (no .s) does NOT throw -- Simkin treats an unopenable "
              "file as an empty script, which is why this failed silently");
        if (ghost) {
            Check(!ghost->usable(),
                  "...and the object it produces is not usable: SetUsable(true) lives in an Init() "
                  "that never ran, so findNearbyPickup() skipped every dropped bag");
            Check(ghost->contents().empty(),
                  "...and it is empty: no Level.CreateEntity(704), no AddObject()");
        }
    }

    // ---- Part 3: every SetLoot call site in the corpus, resolved for real ----
    std::printf("\nPart 3 -- the shipped SetLoot call sites, resolved for real\n");
    {
        std::vector<LootCallSite> sites = CollectSetLootCalls(scriptRoot);
        std::printf("    %zu SetLoot call site(s) found in the .s corpus\n", sites.size());
        Check(sites.size() == 134,
              "134 SetLoot call sites, the count the corpus actually has -- if this number moves, "
              "the scan below is no longer looking at the same data");

        int badTypeId = 0, resolvedNew = 0, resolvedOld = 0;
        std::vector<std::string> unresolved;
        std::map<std::string, int> distinctTags;
        for (const LootCallSite& site : sites) {
            if (site.typeId != kBagLootTypeId) ++badTypeId;
            ++distinctTags[site.tag];
            if (FileExists(sk_bindings::ResolveScriptPath(scriptRoot, site.tag))) {
                ++resolvedNew;
            } else {
                unresolved.push_back(site.file + ": \"" + site.tag + "\"");
            }
            // The old rule, for the same tag: slashes swapped, no extension.
            std::string relPath = site.tag;
            for (char& c : relPath) {
                if (c == '\\') c = '/';
            }
            if (FileExists(std::string(scriptRoot) + "/" + relPath)) ++resolvedOld;
        }
        std::printf("    %zu distinct tags; %d of %zu sites resolve now, %d resolved before\n",
                    distinctTags.size(), resolvedNew, sites.size(), resolvedOld);
        for (const std::string& u : unresolved) std::printf("    unresolved: %s\n", u.c_str());
        Check(badTypeId == 0,
              "every SetLoot call site in the game passes typeId 300 -- the container the drop "
              "creates is always the same one, and it is read back rather than assumed");
        // One shipped call site does not resolve, and it is a typo in the
        // game's own data rather than anything this port can look up:
        // dstar_e/dse_skyrim_soldier.s asks for "Bounder_Skin", which names
        // the *item* (items/bounder_skin.s, entities.txt typeId 4738) where
        // a bag wrapper belongs. That item's real wrapper does exist and is
        // called loot_sbskin.s (`Item = Level.CreateEntity(4738);
        // AddObject(Item);`) -- the author wrote the contents' name instead
        // of the bag's. The engine has no fallback either: FUN_100715a8
        // sprintfs the tag straight into a path and loads it, so in the
        // original this soldier drops a bag whose script did not load --
        // present and drawn (the model comes from typeId 300's descriptor,
        // not from the script) but unopenable, because SetUsable(true)
        // lives in the Init() that never ran. The port reproduces that
        // rather than second-guessing the shipped data.
        Check(unresolved.size() == 1 &&
                  unresolved[0].find("dse_skyrim_soldier.s") != std::string::npos,
              "every SetLoot tag but one resolves to a real script through ResolveScriptPath -- "
              "including the subdirectory forms, whose backslashes the old hand-built "
              "concatenation left as a doubled separator with no extension; the single exception "
              "is a typo in the shipped data, see the comment above");
        Check(resolvedOld == 0,
              "...and not one of them resolved under the old rule: no loot bag in the game had "
              "ever loaded in this port");
    }

    // ---- Part 4: SetLoot stores the typeId, and the roll clears both ----
    std::printf("\nPart 4 -- what SetLoot leaves on the creature\n");
    {
        auto loadRat = [&]() -> std::unique_ptr<sk_bindings::MonsterExecutable> {
            skExecutableContext loadCtxt(&interpreter);
            auto rat = std::make_unique<sk_bindings::MonsterExecutable>(
                skString((std::string(scriptRoot) + "/monsters/Azra_Rat.s").c_str()), loadCtxt,
                &strings, stack.player(), stack);
            skRValueArray args;
            args.append(skRValue(0));
            skRValue ret;
            skExecutableContext callCtxt(&interpreter);
            rat->method(skString("Init"), args, ret, callCtxt);
            return rat;
        };
        // `SetLoot(300, "Loot_ratseye", 1, 8)` -- one rat in eight carries
        // it. Both fields move together: the engine writes `+0x2f0` and
        // `+0x2f4` only after the roll, so a rat that lost it has neither.
        int withLoot = 0, bothFields = 0, neitherField = 0;
        const int kTrials = 4000;
        for (int i = 0; i < kTrials; ++i) {
            std::unique_ptr<sk_bindings::MonsterExecutable> rat = loadRat();
            const bool hasTag = !rat->lootTag().empty();
            const bool hasType = rat->lootTypeId() != 0;
            if (hasTag) ++withLoot;
            if (hasTag && hasType && rat->lootTypeId() == kBagLootTypeId &&
                rat->lootTag() == "Loot_ratseye") {
                ++bothFields;
            }
            if (!hasTag && !hasType) ++neitherField;
        }
        std::printf("    %d of %d rats carried loot (1-in-8 would be %d)\n", withLoot, kTrials,
                    kTrials / 8);
        Check(bothFields == withLoot,
              "a rat that carries loot carries both fields: typeId 300 and tag \"Loot_ratseye\"");
        Check(neitherField == kTrials - withLoot,
              "a rat that lost the drop-chance roll carries neither -- lootTypeId() == 0 is what "
              "FUN_10083c04's `+0x2f0 != 0` gate reads, so it drops nothing");
        Check(withLoot > kTrials / 16 && withLoot < kTrials / 4,
              "the 1-in-8 rate holds (loose bounds -- this is a live rand)");
    }

    // ---- Part 5: the real drop, through the call main.cpp makes ----
    std::printf("\nPart 5 -- the drop itself\n");
    {
        std::unique_ptr<sk_bindings::ItemExecutable> bag =
            stack.level().CreateEntityWithScript(kBagLootTypeId, "Loot_ratseye");
        Check(bag != nullptr, "CreateEntityWithScript(300, \"Loot_ratseye\") returns a real object");
        if (bag) {
            Check(bag->usable(),
                  "the bag is usable -- loot_ratseye.s's real SetUsable(true) ran, so the Use "
                  "prompt can find it");
            Check(bag->contents().size() == 1,
                  "the bag holds one real item -- Level.CreateEntity(704) + AddObject()");
            Check(bag->templateId() == kBagLootTypeId,
                  "the bag's templateId is 300, not 704 -- the typeId is what the bag *is*, the "
                  "tag is only the script it runs");
            Check(bag->entityCategory() == kContainerCategory,
                  "the bag's entities.txt category is 8, taken from typeId 300's row before its "
                  "Init() ran (the M74 ordering)");
            // The payoff: its real OnUse() opens the real lootmenu.s over
            // itself, with the real item in it.
            bag->InvokeOnUse();
            sk_bindings::MenuExecutable* menu = stack.currentMenu();
            Check(menu != nullptr, "bag.OnUse() opened the real LootMenu");
            bool rowFound = false;
            if (menu && bag->contents().size() == 1) {
                for (const auto& row : menu->rows()) {
                    if (row.associatedObject ==
                        static_cast<skiExecutable*>(bag->contents()[0].get())) {
                        rowFound = true;
                        break;
                    }
                }
            }
            Check(rowFound, "the real ratseye is a real row in it");
        }

        // A subdirectory tag, and a gold bag whose contents are rolled --
        // "random loot" in the reported sense.
        std::unique_ptr<sk_bindings::ItemExecutable> subdir =
            stack.level().CreateEntityWithScript(kBagLootTypeId, "broken1\\loot2");
        Check(subdir != nullptr && subdir->usable(),
              "a subdirectory tag works too: broken1/bled.s's SetLoot(300, \"broken1\\\\loot2\")");

        // And the one shipped tag that names nothing (Part 3): the drop
        // still happens, exactly as it does in the engine -- a bag that is
        // there and drawn but cannot be opened, because its script did not
        // load. Pinned here so a later change cannot quietly turn a
        // faithful reproduction of the game's own typo into a crash, a
        // skipped drop, or a "helpful" substitution.
        std::unique_ptr<sk_bindings::ItemExecutable> typo =
            stack.level().CreateEntityWithScript(kBagLootTypeId, "Bounder_Skin");
        Check(typo != nullptr && !typo->usable() && typo->contents().empty(),
              "dse_skyrim_soldier.s's dangling \"Bounder_Skin\" tag still drops a bag -- present "
              "and drawn, empty and unopenable, which is what the engine does with it");

        int lo = 0x7fffffff, hi = 0;
        bool everyGoldBagFilled = true;
        for (int i = 0; i < 64; ++i) {
            std::unique_ptr<sk_bindings::ItemExecutable> gold =
                stack.level().CreateEntityWithScript(kBagLootTypeId, "Loot_Gold25-35");
            if (!gold || gold->contents().size() != 1) {
                everyGoldBagFilled = false;
                break;
            }
            const int qty = gold->contents()[0]->quantity();
            // Parenthesised so <windows.h>'s min/max macros (pulled in by
            // this port's platform headers) cannot swallow the call.
            lo = (std::min)(lo, qty);
            hi = (std::max)(hi, qty);
        }
        std::printf("    Loot_Gold25-35 over 64 drops: quantity %d..%d\n", lo, hi);
        Check(everyGoldBagFilled && lo >= 25 && hi <= 35 && lo != hi,
              "the gold bag's contents are genuinely random per drop -- loot_gold25-35.s's real "
              "Item.SetQuantity(Random(25,35))");
    }

    // ---- Part 5b: the other drop, the one a creature's script places ----
    std::printf("\nPart 5b -- Level.CreateEntityScript, the script-driven drop\n");
    {
        // Found by running the game rather than by reading it: with every
        // death finally reaching InvokeOnKilled(), the log filled with
        // `[soft-fail] ZoneScript: CreateEntityScript(300, Loot_ratseye,
        // ...) -- not implemented`. `monsters/arat.s`'s own OnKilled()
        // rolls `Random(1,12)=12` and places its own bag, entirely in
        // script, on top of whatever SetLoot already arranged;
        // `monsters/spiderqueen.s` drops "Loot_ShadowKey" the same way.
        // Five live call sites in the corpus, and every one of them called
        // a method on the result, so the object has to come back from the
        // native immediately even though its placement is deferred.
        auto callCreateEntityScript = [&](int typeId, const char* tag, bool withPosition,
                                           int x, int y, int z) -> skRValue {
            skRValueArray args;
            args.append(skRValue(typeId));
            args.append(skRValue(skString(tag)));
            if (withPosition) {
                args.append(skRValue(x));
                args.append(skRValue(y));
                args.append(skRValue(z));
            }
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            stack.level().method(skString("CreateEntityScript"), args, ret, ctxt);
            return ret;
        };

        skRValue ret = callCreateEntityScript(kBagLootTypeId, "Loot_ratseye", true, 30179, 11540,
                                               -4031);
        Check(ret.type() == skRValue::T_Object,
              "Level.CreateEntityScript(300, \"Loot_ratseye\", x, y, z) returns the bag object -- "
              "arat.s calls Loot.SetDestroy(true) on it on the very next line");

        sk_bindings::LevelExecutable::PendingPlacedItem req;
        std::unique_ptr<sk_bindings::ItemExecutable> placed;
        const bool took = stack.level().TakePendingPlacedItem(req, placed);
        Check(took && placed != nullptr,
              "...and parks the world placement for the host, the same split the four-argument "
              "CreateEntity already uses for a creature");
        if (took && placed) {
            Check(static_cast<skiExecutable*>(placed.get()) == ret.obj(),
                  "the parked object is the same one the script was handed");
            Check(placed->usable() && placed->contents().size() == 1,
                  "it is a real, loaded, filled loot bag -- not the empty shell an unresolvable "
                  "path would produce");
            Check(req.typeId == kBagLootTypeId && req.hasPosition && req.snapToGround &&
                      req.x == 30179 && req.y == 11540 && req.z == -4031,
                  "the placement carries the creature's own position and asks for the surface "
                  "snap the five-argument arm does");
            // SetDestroy(true) is the line every one of these callers runs
            // next; it is what makes a scripted bag vanish once emptied.
            skRValueArray destroyArgs;
            destroyArgs.append(skRValue(true));
            skRValue destroyRet;
            skExecutableContext destroyCtxt(&interpreter);
            placed->method(skString("SetDestroy"), destroyArgs, destroyRet, destroyCtxt);
            Check(placed->destroyWhenEmpty(),
                  "Loot.SetDestroy(true) reaches the returned object");
        }
        // Nothing must be left parked afterwards.
        sk_bindings::LevelExecutable::PendingPlacedItem again;
        std::unique_ptr<sk_bindings::ItemExecutable> againScript;
        Check(!stack.level().TakePendingPlacedItem(again, againScript),
              "the pending slot clears on read -- one call, one placement");

        // crypt1/shadowkeygate.s's form: typeId 301 (`301 15 8 !chest_loot`),
        // a different container with a different model, at a literal
        // position. Same native, and the descriptor is what tells the two
        // apart.
        skRValue chestRet = callCreateEntityScript(301, "crypt1\\skchest1a", true, 8376, 3200, 300);
        sk_bindings::LevelExecutable::PendingPlacedItem chestReq;
        std::unique_ptr<sk_bindings::ItemExecutable> chest;
        const bool tookChest = stack.level().TakePendingPlacedItem(chestReq, chest);
        Check(chestRet.type() == skRValue::T_Object && tookChest && chest != nullptr,
              "the same native builds shadowkeygate.s's typeId-301 chest from a subdirectory tag");
        Check(entityTypes.Lookup(301) != nullptr &&
                  entityTypes.Lookup(301)->modelArchiveIndex == 15 &&
                  entityTypes.Lookup(301)->category == kContainerCategory,
              "...and typeId 301 is its own container with its own model (chest_closed.bin, "
              "archive index 15) -- which is why the model comes off the descriptor rather than "
              "being hardcoded per drop");
    }

    // ---- Part 6: where the bag lands ----
    std::printf("\nPart 6 -- the drop position\n");
    {
        sk::Zone zone;
        if (!zone.Load(scriptRoot, "azra")) {
            std::printf("  [FAILED] could not load azra for the position check\n");
            ++g_Checks;
            ++g_Failed;
        } else {
            // `FUN_10084438`: the bag takes the creature's x/y and its
            // z + 300, then the floor snap and the 0x80 lift -- which
            // together are Zone::SnapActorToGround.
            const float x = 118.5f * sk::kTileScale, y = 46.5f * sk::kTileScale;
            const float floor = zone.CollisionFloorHeightAt(x, y, 0.0f);
            const float dropped = zone.SnapActorToGround(x, y, floor + 300.0f);
            std::printf("    floor %.1f -> bag z %.1f (delta %.1f)\n", floor, dropped,
                        dropped - floor);
            Check(dropped > floor,
                  "a bag dropped on the floor ends up above it, not inside it (the 0x80 lift)");
            Check(dropped - floor < 512.0f,
                  "...and not far above it -- one tile is 256 units, so the bag stays within reach "
                  "of the Use prompt rather than floating");
            // The snap is absolute, so a creature killed above the floor
            // still drops its bag onto the floor.
            // The scripted drop's own snap is a different one: no lift, and
            // the storey probe is the tile's authored floor-band threshold
            // rather than anything the caller passed.
            const float scripted = zone.SnapSpawnedObjectToGround(x, y);
            std::printf("    CreateEntityScript snap at the same spot: %.1f\n", scripted);
            Check(scripted == dropped - 128.0f,
                  "Level.CreateEntityScript's placement is the same surface without the 0x80 "
                  "lift -- the two drops resolve to the same floor and sit at different heights "
                  "on it, which is what the two decompiled arms do");
            const float fromHigh = zone.SnapActorToGround(x, y, floor + 2000.0f + 300.0f);
            Check(fromHigh == dropped,
                  "the snap is absolute: a creature killed 2000 units up drops its bag in the "
                  "same place as one killed standing on the floor");
        }
    }

    std::printf("\nm81_loot_drop_smoke: %d check(s), %d failed -- %s\n", g_Checks, g_Failed,
                g_Failed == 0 ? "OK" : "FAILED");
    return g_Failed == 0 ? 0 : 1;
}
