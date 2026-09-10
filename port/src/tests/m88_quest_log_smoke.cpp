// M88 (quest tracking) smoke test.
//
// M17 gave the port the three quest *flags* and proved a real dialogue
// tree could branch on them. What it explicitly left out -- and what made
// the whole system invisible in a real play session -- is the other half:
// the engine's own per-quest text table, and the quest log that reads it.
// `DisplayObjectives` was a stub that wrote one fixed row, so a player who
// accepted every quest in the game saw "No active quests." forever.
//
// Five parts, all against real shipped data:
//
//   1. The metadata table (`FUN_10045334`) against a real stringtable.eng:
//      fifty quests, titles 0x45.. and objectives 0x77.. in lockstep, and
//      nothing outside that range.
//   2. Every quest id the whole `.s` corpus actually uses, checked against
//      that range -- the ids a script can set but the log can never show.
//   3. The real `questlog.s`, opened through a real MenuStack, with quest
//      state driven underneath it: empty, one quest, two quests, solved,
//      completed, an id with no text, and `AllQuests()`.
//   4. The row *shape* the engine builds -- title / objective / blank, in
//      threes, with the title centred (cell+0x2c bit 1) and the blank one
//      flagged as a continuation (cell+0x34).
//   5. End to end through a real conversation: `trothgarconvo.s`'s own
//      YesResponse and MoreResponse handlers, which is how quest 0 is
//      actually taken and closed in the shipped game.
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "assets/string_table.h"
#include "simkin_bindings/menu_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "simkin_bindings/quest_table.h"
#include "simkin_bindings/table_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skParseException.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skRuntimeException.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void Check(bool condition, const std::string& what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::printf("  FAILED: %s\n", what.c_str());
    }
}

void CheckEq(int got, int want, const std::string& what) {
    ++gChecks;
    if (got != want) {
        ++gFailures;
        std::printf("  FAILED: %s -- got %d, want %d\n", what.c_str(), got, want);
    }
}

void CheckStr(const std::string& got, const std::string& want, const std::string& what) {
    ++gChecks;
    if (got != want) {
        ++gFailures;
        std::printf("  FAILED: %s -- got [%s], want [%s]\n", what.c_str(), got.c_str(),
                    want.c_str());
    }
}

// The quest-log table on whichever menu is current -- questlog.s builds
// exactly one, via `questTable=AddTable(6, 10, 45, 150, 110)`.
sk_bindings::TableExecutable* QuestTable(sk_bindings::MenuStack& stack) {
    sk_bindings::MenuExecutable* menu = stack.currentMenu();
    if (!menu) return nullptr;
    for (const auto& row : menu->rows()) {
        if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) {
            return static_cast<sk_bindings::TableExecutable*>(row.widget.get());
        }
    }
    return nullptr;
}

// Reopen the real questlog.s (its OnDisplay is what calls
// DisplayObjectives) and hand back its table.
sk_bindings::TableExecutable* ReopenQuestLog(sk_bindings::MenuStack& stack) {
    stack.ReopenMenu("questlog");
    return QuestTable(stack);
}

// Is the quest-log table currently focusable? The engine clears
// `table+0x5c` when there is nothing to scroll; here that lands on the
// owning menu row's `selectable`.
bool TableSelectable(sk_bindings::MenuStack& stack) {
    sk_bindings::MenuExecutable* menu = stack.currentMenu();
    if (!menu) return false;
    for (const auto& row : menu->rows()) {
        if (row.kind == sk_bindings::MenuExecutable::RowKind::Table) return row.selectable;
    }
    return false;
}

int RowCountOf(sk_bindings::TableExecutable* table) { return table ? table->rowCount() : -1; }

// Drive quest state the way a script does -- through the real native, not
// through a test-only setter, so this exercises the same path
// trothgarconvo.s takes.
void SetQuest(sk_bindings::PlayerExecutable& player, skInterpreter& interpreter,
               const char* native, int id, bool value) {
    skRValueArray args;
    args.append(skRValue(id));
    args.append(skRValue(value));
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    player.method(skString(native), args, ret, ctxt);
}

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// Every integer literal passed as the first argument of a
// SetQuest*/Quest* call anywhere in one script's text.
void CollectQuestIds(const std::string& text, std::set<int>& out) {
    static const char* const kNames[] = {"SetQuestAssigned", "SetQuestSolved",
                                          "SetQuestCompleted", "QuestAssigned",
                                          "QuestSolved",       "QuestCompleted"};
    for (const char* name : kNames) {
        const size_t nameLen = std::string(name).size();
        size_t at = 0;
        while ((at = text.find(name, at)) != std::string::npos) {
            size_t p = at + nameLen;
            at = p;
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
            if (p >= text.size() || text[p] != '(') continue;
            ++p;
            while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
            size_t start = p;
            while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
            if (p == start) continue;
            out.insert(std::stoi(text.substr(start, p - start)));
        }
    }
}

// Run one real handler on the current menu, the way a player choosing a
// dialogue row does.
bool RunHandler(sk_bindings::MenuStack& stack, skInterpreter& interpreter, const char* handler) {
    sk_bindings::MenuExecutable* menu = stack.currentMenu();
    if (!menu) return false;
    skRValueArray args;
    args.append(skRValue(0));  // the "(s)" parameter every convo handler declares
    skRValue ret;
    skExecutableContext ctxt(&interpreter);
    try {
        return menu->method(skString(handler), args, ret, ctxt);
    } catch (skRuntimeException& e) {
        std::printf("  (handler %s threw: %s)\n", handler, e.toString().ptr());
        return false;
    }
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                  : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                    "EnFrDeEsIt-26102004/system/apps/6r51";

    sk::StringTable strings;
    if (!strings.Load(std::string(scriptRoot) + "/stringtable.eng")) {
        std::printf("m88_quest_log_smoke: FAILED to load stringtable.eng\n");
        return 1;
    }

    // ---------------------------------------------------------------
    // Part 1 -- FUN_10045334's table, against the real string table.
    // ---------------------------------------------------------------
    std::printf("Part 1: the per-quest text table (FUN_10045334)\n");
    CheckEq(sk_bindings::kQuestTextCount, 0x32, "the table holds 0x32 records");
    CheckEq(sk_bindings::kQuestStateSlots, 0x100, "the state arrays are 0x100 wide");
    Check(!sk_bindings::QuestTextFor(-1).valid(), "id -1 has no text");
    Check(sk_bindings::QuestTextFor(0).valid(), "id 0 has text");
    Check(sk_bindings::QuestTextFor(49).valid(), "id 49 has text");
    Check(!sk_bindings::QuestTextFor(50).valid(), "id 50 has no text");
    Check(!sk_bindings::QuestTextFor(255).valid(), "id 255 has no text");

    for (int id = 0; id < sk_bindings::kQuestTextCount; ++id) {
        const sk_bindings::QuestText text = sk_bindings::QuestTextFor(id);
        CheckEq(text.titleId, 0x45 + id, "quest " + std::to_string(id) + " title id");
        CheckEq(text.descriptionId, 0x77 + id, "quest " + std::to_string(id) + " objective id");
        const std::string title = strings.Get(text.titleId);
        const std::string objective = strings.Get(text.descriptionId);
        Check(!title.empty() && title != "?", "quest " + std::to_string(id) + " has a real title");
        Check(!objective.empty() && objective != "?",
              "quest " + std::to_string(id) + " has a real objective");
    }
    // The two runs must not overlap: the last title is one below the first
    // objective, which is what makes 0x45+i / 0x77+i two clean 50-long
    // blocks rather than a coincidence.
    CheckEq(0x45 + sk_bindings::kQuestTextCount, 0x77,
            "the title block ends where objectives start");
    CheckStr(strings.Get(0x45), "Rat Quest", "quest 0's title");
    CheckStr(strings.Get(0x77), "Kill 8 rats, return to Gravel Trothgar", "quest 0's objective");
    CheckStr(strings.Get(0x76), "Caretaker Rescue", "quest 49's title");
    CheckStr(strings.Get(0xa8), "Clear the entrance, and help the Caretaker escape",
             "quest 49's objective");
    // The id right past the block is not more quest text -- it is the
    // start of unrelated UI strings, which is the check that the table
    // really is 50 long and not "at least 50".
    CheckStr(strings.Get(0xa9), "None", "0xa9 is not a 51st objective");
    std::printf("  quest 0  = [%s] / [%s]\n", strings.Get(0x45).c_str(), strings.Get(0x77).c_str());
    std::printf("  quest 49 = [%s] / [%s]\n", strings.Get(0x76).c_str(), strings.Get(0xa8).c_str());

    // ---------------------------------------------------------------
    // Part 2 -- the ids the shipped corpus actually uses.
    // ---------------------------------------------------------------
    std::printf("\nPart 2: quest ids used by the shipped scripts\n");
    static const char* const kQuestScripts[] = {
        "almatheaconvo", "arat2",         "azra",       "azra_convo2", "azraskelosconvo",
        "azratanyinconvo", "broken1",     "cheatmenu",  "crypt1",      "crypt2",
        "crypt3",        "delfhide",      "dstar_e",    "dstar_w",     "erthcave",
        "fearfrst",      "ffarena",       "ghstpass",   "heather_locket_convo",
        "herbhurrah",    "junction",      "loot_skyrim", "lothcav",    "menlinconvo",
        "ratherb",       "rathurrah",     "snowline",   "stouttp",     "temple",
        "trinketconvo",  "trothgarconvo", "twilite"};
    std::set<int> usedIds;
    int scriptsRead = 0;
    for (const char* name : kQuestScripts) {
        const std::string text = ReadFile(std::string(scriptRoot) + "/" + name + ".s");
        if (text.empty()) {
            std::printf("  FAILED: could not read %s.s\n", name);
            ++gFailures;
            ++gChecks;
            continue;
        }
        ++scriptsRead;
        CollectQuestIds(text, usedIds);
    }
    CheckEq(scriptsRead, 32, "all 32 quest-using scripts read");
    Check(!usedIds.empty(), "the corpus names at least one quest id");
    int outsideTable = 0;
    for (int id : usedIds) {
        if (!sk_bindings::QuestTextFor(id).valid()) ++outsideTable;
    }
    std::printf("  %d distinct quest ids used, highest %d, %d of them outside the text table\n",
                static_cast<int>(usedIds.size()), usedIds.empty() ? -1 : *usedIds.rbegin(),
                outsideTable);
    // Three of them do NOT, and that is the interesting result -- it makes
    // the log's `descriptionId != 0` guard load-bearing in the shipped
    // game rather than merely defensive. The three are 88, 233 and 240,
    // and reading their call sites says what they are:
    //
    //   240  crypt3.s sets it (`SetQuestSolved(240)`) and crypt1/2/3 read
    //        it back to decide whether the crypt has been opened. A real
    //        hidden world flag, stored in the quest array because that is
    //        the only 256-wide persistent bit array a script can reach.
    //   233  read by broken1.s and delfhide.s; **never written anywhere**.
    //   88   read by dstar_w.s; never written either.
    //
    // So the two dead ones are permanently-false conditions in the
    // shipped game, and the live one is deliberately invisible: assigning
    // text to id 240 would put "the crypt is open" in the player's quest
    // log. Pinning the exact set here means a future change to either the
    // table's width or the log's filter has to come back and re-argue it.
    CheckEq(outsideTable, 3, "exactly three corpus quest ids have no text");
    Check(usedIds.count(88) == 1 && !sk_bindings::QuestTextFor(88).valid(),
          "id 88 is used and has no text");
    Check(usedIds.count(233) == 1 && !sk_bindings::QuestTextFor(233).valid(),
          "id 233 is used and has no text");
    Check(usedIds.count(240) == 1 && !sk_bindings::QuestTextFor(240).valid(),
          "id 240 is used and has no text");
    for (int id : usedIds) {
        if (id == 88 || id == 233 || id == 240) continue;
        Check(sk_bindings::QuestTextFor(id).valid(),
              "quest id " + std::to_string(id) + " (used by a script) has real text");
    }

    // ---------------------------------------------------------------
    // Part 3/4 -- the real questlog.s screen.
    // ---------------------------------------------------------------
    std::printf("\nPart 3: the real questlog.s, driven through a real MenuStack\n");
    skInterpreter interpreter;
    sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
    sk_bindings::PlayerExecutable& player = stack.player();

    sk_bindings::TableExecutable* table = ReopenQuestLog(stack);
    if (!table) {
        std::printf("m88_quest_log_smoke: FAILED -- questlog.s built no table\n");
        return 1;
    }
    Check(table->lineWrap(), "questlog.s's SetLineWrap(true) reached the table");
    CheckEq(table->width(), 150, "AddTable's real width");
    CheckEq(table->height(), 110, "AddTable's real height");
    CheckEq(table->rowCount(), 0, "an untouched player's log is empty");
    Check(!TableSelectable(stack), "an empty log's table takes no focus");

    // One quest, assigned.
    SetQuest(player, interpreter, "SetQuestAssigned", 0, true);
    table = ReopenQuestLog(stack);
    CheckEq(RowCountOf(table), 3, "one quest is three rows");
    CheckStr(table->CellText(0, 0), "Rat Quest", "row 0 is the title");
    CheckStr(table->CellText(1, 0), "Kill 8 rats, return to Gravel Trothgar",
             "row 1 is the objective");
    CheckStr(table->CellText(2, 0), "", "row 2 is the blank separator");
    Check(!TableSelectable(stack), "one quest still does not fill the six-row box");

    std::printf("\nPart 4: the row shape the engine builds\n");
    const sk_bindings::TableCell* titleCell = table->PeekCell(0, 0);
    const sk_bindings::TableCell* objectiveCell = table->PeekCell(1, 0);
    const sk_bindings::TableCell* spacerCell = table->PeekCell(2, 0);
    Check(titleCell != nullptr && objectiveCell != nullptr && spacerCell != nullptr,
          "all three cells exist");
    if (titleCell && objectiveCell && spacerCell) {
        CheckEq(static_cast<int>(titleCell->displayFlags),
                static_cast<int>(sk_bindings::kCellFlagCentered), "the title cell is centred");
        CheckEq(static_cast<int>(objectiveCell->displayFlags), 0,
                "the objective cell is not centred");
        Check(!titleCell->continuation, "the title cell is not a continuation");
        Check(!objectiveCell->continuation, "the objective cell is not a continuation");
        Check(spacerCell->continuation, "the blank cell is a continuation (cell+0x34)");
    }

    // Solved is still "in the log" -- only Completed removes it.
    SetQuest(player, interpreter, "SetQuestSolved", 0, true);
    table = ReopenQuestLog(stack);
    CheckEq(RowCountOf(table), 3, "a solved-but-open quest still shows");

    // A second quest, and now the table is worth focusing.
    SetQuest(player, interpreter, "SetQuestAssigned", 6, true);
    table = ReopenQuestLog(stack);
    CheckEq(RowCountOf(table), 6, "two quests are six rows");
    CheckStr(table->CellText(0, 0), "Rat Quest", "quests are listed in id order (0 first)");
    CheckStr(table->CellText(3, 0), strings.Get(0x45 + 6), "quest 6's title follows");
    CheckStr(table->CellText(4, 0), strings.Get(0x77 + 6), "quest 6's objective follows");
    Check(TableSelectable(stack), "six rows fill the box, so the table takes focus");

    // Completing removes it.
    SetQuest(player, interpreter, "SetQuestCompleted", 0, true);
    table = ReopenQuestLog(stack);
    CheckEq(RowCountOf(table), 3, "completing quest 0 removes it from the log");
    CheckStr(table->CellText(0, 0), strings.Get(0x45 + 6), "quest 6 is now the only entry");

    // An assigned id with no text is silently invisible -- the real
    // `descriptionId != 0` guard, and the reason the loop runs to 255.
    // Using 240, the crypt flag crypt3.s really writes, rather than an
    // invented id: even in the worst case where something assigns it as
    // well as solving it, the log must stay clean.
    SetQuest(player, interpreter, "SetQuestAssigned", 240, true);
    table = ReopenQuestLog(stack);
    CheckEq(RowCountOf(table), 3, "an id past the text table never reaches the log");
    Check(player.questAssigned(240), "...even though the flag itself is really set");

    // AllQuests() -- the unused debug native, at its real 256-wide reach.
    {
        skRValueArray args;
        skRValue ret;
        skExecutableContext ctxt(&interpreter);
        player.method(skString("AllQuests"), args, ret, ctxt);
    }
    Check(player.questAssigned(255), "AllQuests() assigns all 256 ids");
    table = ReopenQuestLog(stack);
    // 49 quests: all fifty minus quest 0, which is still Completed.
    CheckEq(RowCountOf(table), 49 * 3, "AllQuests() shows every quest that has text");

    // ---------------------------------------------------------------
    // Part 5 -- through the real conversation that gives quest 0.
    // ---------------------------------------------------------------
    std::printf("\nPart 5: trothgarconvo.s, the real quest 0 hand-out\n");
    sk_bindings::MenuStack fresh(scriptRoot, interpreter, &strings);
    sk_bindings::PlayerExecutable& freshPlayer = fresh.player();
    Check(!freshPlayer.questAssigned(0), "a fresh player has not taken quest 0");
    sk_bindings::TableExecutable* freshTable = ReopenQuestLog(fresh);
    CheckEq(RowCountOf(freshTable), 0, "...and their log is empty");

    try {
        fresh.ReopenMenu("trothgarconvo");
    } catch (skParseException& e) {
        std::printf("m88_quest_log_smoke: FAILED -- PARSE ERROR in trothgarconvo.s: %s\n",
                    e.toString().ptr());
        return 1;
    }
    Check(fresh.currentMenu() != nullptr, "trothgarconvo.s opened");
    Check(RunHandler(fresh, interpreter, "YesResponse"), "its real YesResponse handler ran");
    Check(freshPlayer.questAssigned(0), "YesResponse assigned quest 0 for real");
    freshTable = ReopenQuestLog(fresh);
    CheckEq(RowCountOf(freshTable), 3, "the quest is now in the log");
    CheckStr(freshTable ? freshTable->CellText(0, 0) : std::string(), "Rat Quest",
             "and it is the right one");

    // MoreResponse is the branch trothgarconvo.s takes once the rats are
    // dead: it pays out and completes the quest, which is what should
    // clear the log again.
    const int goldBefore = freshPlayer.gold();
    fresh.ReopenMenu("trothgarconvo");
    Check(RunHandler(fresh, interpreter, "MoreResponse"), "its real MoreResponse handler ran");
    Check(freshPlayer.questCompleted(0), "MoreResponse completed quest 0");
    Check(freshPlayer.gold() > goldBefore, "...and paid the real reward");
    freshTable = ReopenQuestLog(fresh);
    CheckEq(RowCountOf(freshTable), 0, "a completed quest leaves the log");

    std::printf("\nm88_quest_log_smoke: %d checks, %d failures -- %s\n", gChecks, gFailures,
                gFailures == 0 ? "OK" : "FAILED");
    return gFailures == 0 ? 0 : 1;
}
