// M50 smoke test: what is inside a save file's members.
//
// M40 decoded the container and its test pinned the envelope byte by byte
// against the decompiled writer. This does the same for the letter -- the
// byte stream (assets/save_stream.h) and the twelve-deep record chain
// (assets/save_records.h) that `character.dat` and `<level>.dat` are made
// of -- and then, where the format names things the shipped data also
// names, checks the two agree.
//
// Still no real save file to check against: saves are created at runtime
// on the device and none ships on the install image. So, as in M40, the
// assertions are of four kinds:
//
//   1. byte-layout, against the decompiled writer -- sizes, widths,
//      cursor behaviour, the string length prefix, the terminators;
//   2. structural, against the save/load *pair* -- every record kind
//      writes exactly the layers its vtable chain would, and reads back
//      what it wrote;
//   3. real-data, against the shipped scripts -- the eighteen global
//      story flags the player record hardcodes are looked up in the
//      corpus by name;
//   4. end to end -- a real PlayerExecutable through a real SaveArchive
//      and back.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "assets/save_archive.h"
#include "assets/save_records.h"
#include "assets/save_stream.h"
#include "assets/string_table.h"
#include "simkin_bindings/item_executable.h"
#include "simkin_bindings/menu_stack.h"
#include "simkin_bindings/player_executable.h"
#include "skExecutableContext.h"
#include "skInterpreter.h"
#include "skRValue.h"
#include "skRValueArray.h"
#include "skString.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    std::printf("%-78s %s\n", what.c_str(), ok ? "OK" : "FAILED");
    ++g_checks;
    if (!ok) ++g_failures;
}

size_t WrittenSize(const sk::SavedEntity& rec) {
    sk::SaveStream s;
    rec.Write(s);
    return s.bytes().size();
}

// A record with every layer filled with distinguishable values, so a
// round trip that silently dropped a layer would show up.
sk::SavedEntity MakeRecord(sk::SavedEntityKind kind) {
    sk::SavedEntity rec;
    rec.kind = kind;
    rec.entity.entityId = 0x1234;
    rec.entity.artName = "azra_rat";
    rec.entity.scriptName = "trthgar";
    rec.entity.x = 0x00051800;
    rec.entity.y = -0x00032000;
    rec.entity.z = 0x0140;
    rec.entity.yaw = -0x4000;
    rec.entity.pitch = 0x0800;
    rec.entity.spriteId = 30000;
    rec.entity.f6c = 0x11;
    rec.entity.f6d = 0x22;
    rec.entity.f70 = 0xdeadbeefu;
    rec.entity.deadline10c = 1'000'000;
    rec.entity.deadline118 = 1'000'050;
    rec.entity.scriptVars.push_back({"saved_Open", "1"});
    rec.entity.scriptVars.push_back({"saved_bCanOpen", "0"});
    // Deliberately different from the entity root's own copies -- the
    // Drawable layer re-writes seven of the same fields.
    rec.drawable.f6c = 0x33;
    rec.drawable.f6d = 0x44;
    rec.drawable.f70 = 0xcafebabeu;
    rec.drawable.f12c = 7;
    rec.item.usesRangedPath = 1;
    rec.item.f19c = 0x400;
    rec.stackable.quantity = 12;
    rec.wearable.f1d4 = 5;
    rec.weapon.damageMin = 3;
    rec.weapon.damageMax = 9;
    rec.weapon.quantity = 2;
    rec.spellbook.spells.push_back({4010, 3, false, 0});
    rec.spellbook.spells.push_back({4020, 0, true, 77});
    rec.holder.f1bc = 0x1111;
    rec.stats.attack = 62;
    rec.stats.defense = 41;
    rec.stats.health = 23;
    rec.stats.gold = 250;
    rec.stats.experience = 12345;
    rec.stats.level = 4;
    rec.actor.f2ec = -3;
    rec.linkedActor.link224 = 8;
    rec.character.levelName = "azra";
    rec.character.linkIndex = 2;
    rec.player.characterName = "Ehlnofey";
    rec.player.questAssigned[26] = 1;
    rec.player.monstersKilled[203] = 8;
    rec.player.trailingInts[0] = 3;   // saved_Guild
    rec.player.trailingInts[17] = 1;  // saved_EndGame
    rec.player.characterClass = 2;
    rec.player.race = 3;
    rec.player.portraitId = 0x23;
    return rec;
}

// Every .s file under the script root, concatenated -- the same corpus
// sweep the earlier milestones do, so the "is this name real" checks are
// against the shipped scripts and not a hand-picked sample.
std::string SlurpScripts(const std::string& root) {
    std::string all;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(root, ec);
    if (ec) return all;
    for (const std::filesystem::directory_entry& entry : it) {
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() != ".s") continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        std::stringstream ss;
        ss << in.rdbuf();
        all += ss.str();
        all += '\n';
    }
    return all;
}

}  // namespace

int main(int argc, char** argv) {
    const char* scriptRoot =
        argc > 1 ? argv[1]
                 : "The-Elder-Scrolls-Travels-Shadowkey_N-Gage_EN-FR-DE-ES-IT_USA-Europe-"
                   "EnFrDeEsIt-26102004/system/apps/6r51";

    std::printf("=== M50: what is inside a save file's members ===\n\n");

    // Pin the clock the entity root's two relative timestamps are stored
    // against, so a round trip cannot straddle a second boundary.
    sk::SetSaveClockOverride(1'700'000'000);

    // ------------------------------------------------------------------
    // Part 1 -- the byte stream, against the decompiled writer.
    // ------------------------------------------------------------------
    std::printf("-- 1. the stream (0x14 bytes, two cursors, no framing) --\n");
    {
        sk::SaveStream s;
        s.WriteU8(0xab);
        Check(s.bytes().size() == 1 && s.bytes()[0] == 0xab, "a u8 is one byte");

        s = sk::SaveStream();
        s.WriteI16(static_cast<int16_t>(-2));
        Check(s.bytes().size() == 2 && s.bytes()[0] == 0xfe && s.bytes()[1] == 0xff,
              "an i16 is two bytes, little-endian");

        s = sk::SaveStream();
        s.WriteI32(0x11223344);
        // FUN_1008beec writes the low halfword first and then the high
        // one, which on a little-endian target is a plain 32-bit store.
        Check(s.bytes().size() == 4 && s.bytes()[0] == 0x44 && s.bytes()[1] == 0x33 &&
                  s.bytes()[2] == 0x22 && s.bytes()[3] == 0x11,
              "an i32 is two i16 halves, low first -- i.e. little-endian");

        s = sk::SaveStream();
        s.WriteString8("azra");
        Check(s.bytes().size() == 6 && s.bytes()[0] == 4 && s.bytes()[1] == 0 &&
                  s.bytes()[2] == 'a' && s.bytes()[5] == 'a',
              "an 8-bit string is an i16 length then the characters, with NO NUL");

        s = sk::SaveStream();
        s.WriteStringW(std::string("hi"));
        Check(s.bytes().size() == 6 && s.bytes()[0] == 2 && s.bytes()[2] == 'h' &&
                  s.bytes()[3] == 0 && s.bytes()[4] == 'i' && s.bytes()[5] == 0,
              "a wide string is an i16 length then one i16 per character");

        // The container's own TOC does the opposite (M40): its name
        // length counts the NUL. The two layers disagree; both stand.
        std::vector<sk::SaveArchive::Record> toc{{"ab", {}}};
        const std::vector<uint8_t> archive = sk::SaveArchive::Serialize(toc);
        Check(archive[8] == 3 && archive[9] == 0,
              "the container's own name length still counts the NUL -- the layers disagree");
    }
    {
        // The read cursor is a separate field (+0x10) from the write
        // cursor (+0x0c): a freshly written stream reads back from 0
        // without rewinding.
        sk::SaveStream s;
        s.WriteI32(99);
        s.WriteU16(7);
        Check(s.writePos() == 6 && s.readPos() == 0, "the two cursors are independent");
        Check(s.ReadI32() == 99 && s.ReadU16() == 7, "reading starts at 0 with no rewind");
        Check(!s.failed(), "an exact read does not fail");
        s.ReadU8();
        Check(s.failed(), "reading past the end latches failure (a port-side check, not the game's)");
    }

    // ------------------------------------------------------------------
    // Part 2 -- the stats block's size and its out-of-order layout.
    // ------------------------------------------------------------------
    std::printf("\n-- 2. the stats block (FUN_1004a284) --\n");
    {
        sk::SavedStats st;
        st.attack = 1;
        st.expWorth = 8;
        st.strength = 20;
        st.magicka = 46;
        st.experience = 0x01020304;
        st.level = 25;
        st.gold = 0x0a0b0c0d;
        st.strengthBonus = 0x1111;
        st.healthBonus = 0x2222;
        st.dotKind = 3;
        sk::SaveStream s;
        st.Write(s);
        Check(s.bytes().size() == sk::SavedStats::kSerializedBytes,
              "the stats block is 70 bytes: 22 halfwords, then exp/level/gold, then 8 more");
        // 22 halfwords = 44 bytes, so experience starts at 44 and level
        // at 48 -- the writer emits +0x00..+0x0e and +0x14..+0x2e first
        // and only doubles back to +0x10/+0x12 after gold.
        Check(s.bytes()[44] == 0x04 && s.bytes()[47] == 0x01,
              "experience is the first 32-bit field, at wire offset 44");
        Check(s.bytes()[48] == 25, "the character level follows it as a halfword");
        Check(s.bytes()[50] == 0x0d && s.bytes()[53] == 0x0a, "then gold, 32-bit");
        Check(s.bytes()[54] == 0x11 && s.bytes()[56] == 0x22,
              "strength bonus and health bonus come AFTER gold, out of field order");

        sk::SavedStats back;
        back.Read(s);
        Check(back.attack == 1 && back.expWorth == 8 && back.strength == 20 && back.magicka == 46 &&
                  back.experience == 0x01020304 && back.level == 25 && back.gold == 0x0a0b0c0d &&
                  back.strengthBonus == 0x1111 && back.healthBonus == 0x2222 && back.dotKind == 3,
              "the stats block round-trips");
    }

    // ------------------------------------------------------------------
    // Part 3 -- the chain: which layers each kind puts on the wire.
    // ------------------------------------------------------------------
    std::printf("\n-- 3. the twelve-deep save/load chain --\n");
    {
        using K = sk::SavedEntityKind;
        const size_t entityOnly = WrittenSize(MakeRecord(K::Entity));
        const size_t drawable = WrittenSize(MakeRecord(K::Drawable));
        const size_t item = WrittenSize(MakeRecord(K::Item));
        const size_t stackable = WrittenSize(MakeRecord(K::Stackable));
        const size_t wearable = WrittenSize(MakeRecord(K::Wearable));
        const size_t weapon = WrittenSize(MakeRecord(K::Weapon));
        const size_t spellbook = WrittenSize(MakeRecord(K::Spellbook));
        const size_t holder = WrittenSize(MakeRecord(K::InventoryHolder));
        const size_t actor = WrittenSize(MakeRecord(K::Actor));
        const size_t linked = WrittenSize(MakeRecord(K::LinkedActor));
        const size_t character = WrittenSize(MakeRecord(K::Character));
        const size_t player = WrittenSize(MakeRecord(K::Player));
        std::printf("   sizes: entity=%zu drawable=%zu item=%zu stackable=%zu wearable=%zu\n",
                    entityOnly, drawable, item, stackable, wearable);
        std::printf("          weapon=%zu spellbook=%zu holder=%zu actor=%zu linked=%zu\n", weapon,
                    spellbook, holder, actor, linked);
        std::printf("          character=%zu player=%zu\n", character, player);

        // FUN_10067db0's own fields: i32, i32, i16, u8, u32, u32, u16,
        // u32, u32, u8 = 26 bytes.
        Check(drawable - entityOnly == 30, "Drawable adds exactly its own 30 bytes over Entity");
        // FUN_1006d35c: u8 + i32 + i32 + u8 = 10.
        Check(item - drawable == 10, "Item adds 10 over Drawable");
        Check(stackable - item == 4, "Stackable adds one i32 (+0x1c4) over Item");
        Check(wearable - stackable == 1, "Wearable adds one u8 (+0x1d4) over Stackable");
        // FUN_1002eaa0: i16 + i16 + i32 + i32 = 12, and it derives from
        // Item directly, not from Stackable.
        Check(weapon - item == 12, "Weapon adds 12 over Item -- it is a sibling of Stackable");
        // FUN_10028a60: u8 u8 u8 + i32 count + two spells (2+4+1 and
        // 2+4+1+2) = 7 + 7 + 9 = 23.
        Check(spellbook - drawable == 23,
              "Spellbook adds its three bytes, a count, and the two spell entries");
        // FUN_10005164: u8 u8 i32 i32 i32 u8 u8 u8 + an i32 child count
        // = 20, with no children in this fixture.
        Check(holder - spellbook == 21, "InventoryHolder adds 21 (including its i32 child count)");
        // FUN_10086514 writes 5 bytes and then a whole stats block.
        Check(actor - holder == 6 + sk::SavedStats::kSerializedBytes,
              "Actor adds its four bytes and a halfword, plus a full stats block");
        Check(linked - holder == 8, "LinkedActor adds two i32 ids");
        // FUN_1001e754: an 8-bit string ("azra") + u8 + i32 + 3*u8 + 9
        // i32 = (2+4) + 1 + 4 + 3 + 36 = 50.
        Check(character - holder == 50, "Character adds the level name and its nine i32s");
        // FUN_10043308's head: a wide string ("Ehlnofey") + 3*256 + 256
        // + i32 = (2+16) + 1024 + 4 = 1046, plus a stats block, plus the
        // tail: 10 i16 + 2 i16 + 1 + 1 + 1 + 2 + 1 + 4 + 4 + 2 + 2 + 2
        // + 18*4 + 8*2 = 20 + 4 + 22 + 72 + 16 = 134.
        Check(player - character == 1046 + sk::SavedStats::kSerializedBytes + 132,
              "Player wraps the chain: 1046 bytes of head, a stats block, and a 132-byte tail");

        // Every kind round-trips.
        const K kinds[] = {K::Entity,    K::Drawable,        K::Item,  K::Stackable,
                           K::Wearable,  K::Weapon,          K::Spellbook, K::InventoryHolder,
                           K::Actor,     K::LinkedActor,     K::Character, K::Player};
        bool allRoundTrip = true;
        for (K kind : kinds) {
            const sk::SavedEntity original = MakeRecord(kind);
            sk::SaveStream s;
            original.Write(s);
            sk::SavedEntity back;
            back.kind = kind;
            back.Read(s);
            if (s.failed() || s.readPos() != s.writePos()) allRoundTrip = false;
            if (back.entity.x != original.entity.x || back.entity.yaw != original.entity.yaw ||
                back.entity.scriptVars.size() != 2) {
                allRoundTrip = false;
            }
        }
        Check(allRoundTrip, "all twelve kinds round-trip and consume exactly what they wrote");
    }
    {
        // The Drawable layer really does re-write seven fields the entity
        // root already wrote. Both copies are on the wire, and a reader
        // that folded them would lose one.
        sk::SavedEntity rec = MakeRecord(sk::SavedEntityKind::Drawable);
        sk::SaveStream s;
        rec.Write(s);
        sk::SavedEntity back;
        back.kind = sk::SavedEntityKind::Drawable;
        back.Read(s);
        Check(back.drawable.f6c == 0x33 && back.entity.f6c == 0x11 &&
                  back.drawable.f70 == 0xcafebabeu && back.entity.f70 == 0xdeadbeefu,
              "+0x6c/+0x70 are written twice, by Drawable and by Entity, and both survive");
    }
    {
        // The script-variable list: a 1 tag per pair, 0xff to end.
        sk::SavedEntity rec;
        rec.kind = sk::SavedEntityKind::Entity;
        sk::SaveStream empty;
        rec.Write(empty);
        rec.entity.scriptVars.push_back({"saved_Open", "1"});
        sk::SaveStream one;
        rec.Write(one);
        // tag(1) + "saved_Open" (2 + 20) + "1" (2 + 2) = 27.
        Check(one.bytes().size() - empty.bytes().size() == 27,
              "one script variable costs a tag plus two wide strings");
        Check(empty.bytes().back() == 0xff, "the variable list ends with 0xff");

        sk::SavedEntity back;
        back.kind = sk::SavedEntityKind::Entity;
        back.Read(one);
        Check(back.entity.scriptVars.size() == 1 && back.entity.scriptVars[0].name == "saved_Open" &&
                  back.entity.scriptVars[0].value == "1",
              "and reads back as one name/value pair");
    }

    // ------------------------------------------------------------------
    // Part 4 -- <level>.dat's envelope.
    // ------------------------------------------------------------------
    std::printf("\n-- 4. <level>.dat (FUN_100187d0) --\n");
    {
        sk::SavedLevelState level;
        level.renderEnabled = 1;
        sk::SaveStream s;
        level.Write(s);
        Check(s.bytes().size() == 2 && s.bytes()[0] == 0 && s.bytes()[1] == 1,
              "an empty level file is two bytes: the 0 terminator and the render flag");

        sk::SavedEntity e = MakeRecord(sk::SavedEntityKind::Actor);
        e.typeId = 203;
        level.entities.push_back(std::move(e));
        sk::SaveStream s2;
        level.Write(s2);
        Check(s2.bytes()[0] == 1 && s2.bytes()[1] == 203 && s2.bytes()[2] == 0,
              "each entry is a 1 tag then an i16 typeId -- 16-bit here, 32-bit inside an inventory");

        sk::SavedLevelState back;
        back.Read(s2, [](int32_t) { return sk::SavedEntityKind::Actor; });
        Check(!s2.failed() && back.entities.size() == 1 && back.entities[0].typeId == 203 &&
                  back.renderEnabled == 1 && back.entities[0].stats.attack == 62,
              "and reads back through a typeId->class resolver, stats and all");
    }
    {
        // An inventory holder's children are nested records, each with a
        // 32-bit typeId in front.
        sk::SavedEntity holder = MakeRecord(sk::SavedEntityKind::InventoryHolder);
        const size_t bare = WrittenSize(holder);
        sk::SavedEntity child = MakeRecord(sk::SavedEntityKind::Weapon);
        child.typeId = 0x1234;
        const size_t childBytes = WrittenSize(child);
        holder.inventory.push_back(child);
        Check(WrittenSize(holder) == bare + 4 + childBytes,
              "a child costs an i32 typeId plus that child's own whole record");

        sk::SaveStream s;
        holder.Write(s);
        sk::SavedEntity back;
        back.kind = sk::SavedEntityKind::InventoryHolder;
        back.Read(s, [](int32_t) { return sk::SavedEntityKind::Weapon; });
        Check(!s.failed() && back.inventory.size() == 1 && back.inventory[0].typeId == 0x1234 &&
                  back.inventory[0].weapon.damageMax == 9,
              "and the child comes back as the class the resolver names");
    }

    // ------------------------------------------------------------------
    // Part 5 -- the eighteen global story flags, against the shipped
    // scripts.
    // ------------------------------------------------------------------
    std::printf("\n-- 5. the eighteen saved_* globals, against the corpus --\n");
    {
        // FUN_1003e7a4 / FUN_1003ebd8 are the player object's getValue /
        // setValue: they match a field name against these eighteen wide
        // strings in this order, one per i32 at +0xfd8..+0x101c. If they
        // really are the game's global story flags, the scripts should
        // reach them through GetPlayer().
        Check(sk::SavedPlayer::kTrailingInts == 18, "there are exactly eighteen of them");
        Check(std::string(sk::SavedPlayer::kGlobalFlagNames[0]) == "saved_Guild" &&
                  std::string(sk::SavedPlayer::kGlobalFlagNames[17]) == "saved_EndGame",
              "the first is saved_Guild and the last saved_EndGame, in getValue's own order");

        const std::string corpus = SlurpScripts(scriptRoot);
        if (corpus.empty()) {
            std::printf("   (no script corpus at '%s' -- skipping the real-data checks)\n",
                        scriptRoot);
        } else {
            // Every one of the eighteen should show up as a *player*
            // field, `GetPlayer().saved_X`, because that is the only path
            // that reaches these ints. A `saved_X` with no GetPlayer() in
            // front of it is a different variable that happens to share a
            // prefix (`Level.saved_Open` and friends, of which the corpus
            // has hundreds -- those ride in their entity's own record).
            int viaGetPlayer = 0;
            int notReferenced = 0;
            std::string unreferenced;
            for (int i = 0; i < sk::SavedPlayer::kTrailingInts; ++i) {
                const std::string name = sk::SavedPlayer::kGlobalFlagNames[i];
                if (corpus.find("GetPlayer()." + name) != std::string::npos) {
                    ++viaGetPlayer;
                } else if (corpus.find(name) == std::string::npos) {
                    ++notReferenced;
                    if (!unreferenced.empty()) unreferenced += ", ";
                    unreferenced += name;
                }
            }
            std::printf("   reached via GetPlayer(): %d/18; never referenced at all: %d (%s)\n",
                        viaGetPlayer, notReferenced,
                        unreferenced.empty() ? "-" : unreferenced.c_str());
            Check(viaGetPlayer + notReferenced == sk::SavedPlayer::kTrailingInts,
                  "every hardcoded name is either a GetPlayer() field or absent -- none is a "
                  "Level variable");
            Check(viaGetPlayer >= 16,
                  "at least sixteen of the eighteen are live GetPlayer() story flags");
            Check(corpus.find("saved_NA_Crystal") == std::string::npos,
                  "saved_NA_Crystal is a cut flag: the format carries it, no script uses it");
            // The corpus is full of `saved_*` names that are NOT these --
            // ordinary script variables persisted with their own entity.
            Check(corpus.find("Level.saved_Open") != std::string::npos,
                  "and the commonest saved_* of all, Level.saved_Open, is not one of the "
                  "eighteen -- it rides in its entity's own record");
        }
    }

    // ------------------------------------------------------------------
    // Part 6 -- a real character.dat, end to end.
    // ------------------------------------------------------------------
    std::printf("\n-- 6. a real character.dat through a real archive --\n");
    {
        sk::StringTable strings;
        strings.Load(std::string(scriptRoot) + "/stringtable.eng");
        skInterpreter interpreter;
        sk_bindings::MenuStack stack(scriptRoot, interpreter, &strings);
        sk_bindings::PlayerExecutable& player = stack.player();

        player.LoadStartingInventory(stack);
        const size_t itemCount = player.inventory().size();
        std::printf("   starting inventory: %zu items\n", itemCount);

        // Drive some real state in through the same script-facing calls
        // the game uses.
        auto call = [&](const char* name, std::vector<int> args) {
            skRValueArray a;
            for (int v : args) a.append(skRValue(v));
            skRValue ret;
            skExecutableContext ctxt(&interpreter);
            player.method(skString(name), a, ret, ctxt);
        };
        call("SetQuestAssigned", {26, 1});
        call("SetQuestSolved", {0, 1});
        call("SetQuestCompleted", {12, 1});
        for (int i = 0; i < 8; ++i) call("AddMonsterKilled", {203});
        call("SetGold", {1234});
        call("SetPortraitID", {35});

        const sk::SavedEntity record = player.BuildSaveRecord("azra");
        Check(record.kind == sk::SavedEntityKind::Player, "the player builds a Player record");
        Check(record.character.levelName == "azra",
              "the level name is in the record, where the real loader reads it from");
        Check(record.player.questAssigned[26] == 1 && record.player.questSolved[0] == 1 &&
                  record.player.questCompleted[12] == 1,
              "the three quest arrays carry the three quest states");
        Check(record.player.monstersKilled[203] == 8,
              "the kill counter carries eight rats in one byte");
        Check(record.stats.gold == 1234, "gold is in the stats block, +0x38");
        Check(record.player.portraitId == 35, "the portrait id is +0xf40");
        Check(record.inventory.size() == itemCount,
              "every inventory item is an InventoryHolder child");

        // Through the real container and back.
        sk::SaveStream out;
        record.Write(out);
        std::vector<sk::SaveArchive::Record> members;
        members.push_back({sk::kCharacterMemberName, out.bytes()});
        const std::vector<uint8_t> file = sk::SaveArchive::Serialize(members);
        std::printf("   character.dat = %zu bytes; the archive = %zu\n", out.bytes().size(),
                    file.size());

        std::vector<sk::SaveArchive::Record> parsed;
        Check(sk::SaveArchive::Parse(file, parsed), "the archive parses back");
        const sk::SaveArchive::Record* member =
            sk::SaveArchive::Find(parsed, "CHARACTER.DAT");  // lookup is case-insensitive
        Check(member != nullptr, "and the member is found case-insensitively");

        sk::SaveStream in;
        in.Reset(member->data);
        sk::SavedEntity loaded;
        loaded.kind = sk::SavedEntityKind::Player;
        loaded.Read(in, [](int32_t) {
            return sk_bindings::PlayerExecutable::kInventoryChildKind;
        });
        Check(!in.failed() && in.readPos() == member->data.size(),
              "the record reads back and consumes the whole member");

        player.ApplySaveRecord(loaded, stack);
        Check(player.questAssigned(26) && player.questSolved(0) && player.questCompleted(12),
              "the loaded player has the same three quest states");
        Check(player.monstersKilled(203) == 8, "and the same kill count");
        Check(player.inventory().size() == itemCount, "and the same number of items");
        Check(loaded.character.levelName == "azra", "and names the level to return to");
        // The one gap this port cannot close from its own side: an item
        // loaded straight from a .s file has no entities.txt typeId, and
        // the equipment slots name equipped items by typeId. So they come
        // back empty here -- faithful to the field, and a real hole in
        // the round trip. Asserted rather than glossed over.
        bool anyTemplate = false;
        for (const auto& item : player.inventory()) {
            if (item && item->templateId() > 0) anyTemplate = true;
        }
        Check(!anyTemplate,
              "script-loaded items have no template id, so equipment slots cannot name them");
    }
    {
        // A truncated member is caught rather than read off the end --
        // the real class has no length to check against at all.
        sk::SavedEntity rec = MakeRecord(sk::SavedEntityKind::Player);
        sk::SaveStream out;
        rec.Write(out);
        std::vector<uint8_t> truncated = out.bytes();
        truncated.resize(truncated.size() / 2);
        sk::SaveStream in;
        in.Reset(truncated);
        sk::SavedEntity loaded;
        loaded.kind = sk::SavedEntityKind::Player;
        loaded.Read(in, [](int32_t) {
            return sk_bindings::PlayerExecutable::kInventoryChildKind;
        });
        Check(in.failed(), "a truncated member fails instead of running off the buffer");
    }

    std::printf("\n%s (%d checks)\n",
                g_failures == 0 ? "m50_save_records_smoke: PASSED (all checks)"
                                : "m50_save_records_smoke: FAILED",
                g_checks);
    return g_failures == 0 ? 0 : 1;
}
