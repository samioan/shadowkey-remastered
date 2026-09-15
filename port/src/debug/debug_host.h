#pragma once

// M68: the seam between the debug suite and the game.
//
// `sk_debug` deliberately does not link `sk_world`, `sk_bindings` or see
// main.cpp's file-local types (MonsterInstance, DoorInstance, PickupInstance
// and friends all live in main.cpp's anonymous namespace). Everything the
// console needs from the running game comes through this interface, which
// main.cpp implements over its own locals -- the same layering the port
// already uses for `LevelExecutable::ZoneRegions` (M44) and
// `DoorExecutable::TileStamp` (M67).
//
// The interface is kept small on purpose. Two design choices do most of the
// work:
//
//   * **Generic tables instead of typed getters.** Every read-only view --
//     the player panel, the world panel, entity lists, renderer stats -- is
//     a `StatGroup` list keyed by a page name. One virtual serves every
//     overlay page *and* the `inspect` command, and adding a new panel is a
//     main.cpp-only change with no interface churn.
//
//   * **A native bridge instead of a command per feature.** `CallNative`
//     and `ScriptEval` reach the real Simkin binding objects. Because the
//     port already implements the real natives for stats, race, class,
//     quests, gold, experience, equipment and entity creation (see
//     docs/SIMKIN_NATIVE_API.md), "change a stat" is
//     `call player SetStrength 90` -- running the exact same code path the
//     shipped scripts run, which is the whole point of debugging a
//     behavioural port. A curated command that poked a C++ field directly
//     would test something the game never does.
//
// Only the handful of things with *no* script binding -- teleporting the
// camera, noclip, freezing the tick -- get their own virtual.

#include <string>
#include <utility>
#include <vector>

namespace sk_debug {

// A titled block of name/value rows. The unit of everything the suite
// displays.
struct StatGroup {
    std::string title;
    std::vector<std::pair<std::string, std::string>> rows;

    void Add(std::string name, std::string value) {
        rows.emplace_back(std::move(name), std::move(value));
    }
};

// A named boolean/integer toggle the host owns (noclip, god, freeze, ...).
struct DebugFlag {
    std::string name;
    int value = 0;
    std::string help;
};

// One live world entity, flattened for display and for `tpe`/`entinfo`.
struct EntityRow {
    int index = 0;         // stable within one Entities() call, not across ticks
    std::string kind;      // "monster", "door", "pickup", "trap", "prop", ...
    std::string name;      // the .ent placement name, or the script's own name
    std::string state;     // AI state / open-closed / whatever the kind has
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float distance = 0.0f;  // from the player, world units
    int health = -1;        // -1 = not applicable
    int maxHealth = -1;
    int typeId = -1;
    int modelIndex = -1;
    bool alive = true;
};

class DebugHost {
public:
    virtual ~DebugHost() = default;

    // ---- state ------------------------------------------------------

    // False in the menus / character creation -- most world commands
    // refuse to run and say so rather than crashing on a null zone.
    virtual bool InGame() const = 0;

    // Called once per game tick, from DebugSuite::BeginTick. Optional --
    // for hosts with work that has to be spread across ticks rather than
    // done all at once inside a command.
    //
    // There is exactly one such case so far, and it is a real engine
    // constraint rather than an implementation detail: `Level.CreateEntity`
    // parks its result in a *single* pending slot that the tick drains, so
    // two calls in one command lose the first. `spawn <typeId> <count>`
    // therefore issues one call per tick from here.
    virtual void HostTick() {}

    // Fills `out` with the panel named `page`. Unknown page = leave empty.
    virtual void Inspect(const std::string& page, std::vector<StatGroup>& out) const = 0;
    // Page names Inspect() understands, in display order.
    virtual std::vector<std::string> Pages() const = 0;

    // Live entities, nearest first.
    virtual void Entities(std::vector<EntityRow>& out) const = 0;

    // A searchable catalog of static game data: "zones", "items",
    // "entities" (entity type ids), "scripts", "models", "sounds",
    // "strings". `filter` is a case-insensitive substring; empty = all.
    virtual std::vector<std::string> Catalog(const std::string& kind,
                                              const std::string& filter) const = 0;
    virtual std::vector<std::string> CatalogKinds() const = 0;

    // ---- toggles ----------------------------------------------------

    virtual std::vector<DebugFlag> Flags() const = 0;
    virtual bool SetFlag(const std::string& name, int value, std::string& message) = 0;

    // ---- world actions ----------------------------------------------
    // Each returns false with `message` explaining why, so the console can
    // report a refusal the same way it reports success.

    // `tileCoords` interprets x/y as tile indices rather than world units.
    virtual bool Teleport(float x, float y, bool tileCoords, std::string& message) = 0;
    virtual bool TeleportToEntity(int index, std::string& message) = 0;
    virtual bool LoadZone(const std::string& zone, std::string& message) = 0;
    virtual bool Face(float degrees, std::string& message) = 0;
    // `what` is a monster script filename ("arat.s", "monsters/azra_rat.s")
    // or a decimal entity type id. `distance` is world units in front of
    // the player.
    virtual bool Spawn(const std::string& what, int count, float distance,
                       std::string& message) = 0;
    // `what` is an item script path or a decimal item type id.
    virtual bool Give(const std::string& what, int count, std::string& message) = 0;
    // `hand`: 0 = whichever is free, 1 = left, 2 = right, -1 = unequip.
    virtual bool Equip(const std::string& what, int hand, std::string& message) = 0;
    // `asPlayer` sources each killing blow to the player (M97), so the kill
    // pays its experience the way a real one does; `killall` passes false,
    // `slay` true.
    virtual bool KillAll(const std::string& filter, bool asPlayer, std::string& message) = 0;

    // ---- the native bridge -------------------------------------------

    // Receiver names `CallNative`/`ScriptEval` accept ("player", "level",
    // "menu", "zone", "ent:<n>", ...), each with a short description.
    virtual std::vector<std::pair<std::string, std::string>> Receivers() const = 0;

    // Invokes one native method on one receiver. Arguments are given as
    // text and converted by shape: a decimal integer becomes an int, "true"
    // and "false" become bools, anything else a string.
    virtual bool CallNative(const std::string& receiver, const std::string& method,
                             const std::vector<std::string>& args, std::string& message) = 0;

    // Runs a fragment of real Simkin against a receiver, through the same
    // interpreter the game's own scripts run on.
    virtual bool ScriptEval(const std::string& receiver, const std::string& code,
                             std::string& message) = 0;
};

}  // namespace sk_debug
