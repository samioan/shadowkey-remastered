#pragma once

// M88: the game's fixed per-quest string table -- the half of the quest
// system that lives in the engine rather than in the scripts.
//
// A script only ever names a quest by number (`SetQuestAssigned(6)`,
// `QuestSolved(0)`); nothing in the whole `.s` corpus carries a quest's
// title or its objective text. Those come from `FUN_10045334`, an
// override of the player's own vtable slot at +0x10 (vtable 0x100fd044,
// entry 0x100fd054) that runs once at player construction, right after
// chaining to the base class's Init:
//
//     psVar2 = (short *)(player + 0xb34);      // title id
//     psVar4 = (short *)(player + 0xb36);      // description id
//     iVar3 = 0;
//     do {
//       *psVar2 = (short)iVar3 + 0x45;
//       *psVar4 = (short)iVar3 + 0x77;
//       iVar3 = iVar3 + 1;
//       psVar2 = psVar2 + 2;                   // +4 bytes: one record
//       psVar4 = psVar4 + 2;
//     } while (iVar3 < 0x32);
//
// So the table is 0x32 == 50 four-byte records at `player+0xb34`, and it
// is pure arithmetic: quest `i`'s title is stringtable id `0x45 + i` and
// its objective line is `0x77 + i`. Confirmed against a real
// `stringtable.eng`: 0x45 is "Rat Quest" and 0x77 is "Kill 8 rats, return
// to Gravel Trothgar" (quest 0, the one `trothgarconvo.s`/`arat2.s`
// drive), and the two runs stay in lockstep for all fifty, ending at 0x76
// "Caretaker Rescue" / 0xa8 "Clear the entrance, and help the Caretaker
// escape".
//
// The three state arrays are 256 entries wide (`player+0x430`/`+0x530`/
// `+0x630`, see player_executable.h) but this metadata table is only 50,
// and the rest of `player+0xb34..+0xf33` is memset to 0 by the
// constructor. That difference is load-bearing rather than sloppy: the
// quest log's own loop runs over all 256 ids and skips any whose
// *description* id is zero (`FUN_100347c8`'s `local_2e != 0` test), so an
// id at or above 50 can be assigned by a script and simply never appears
// on the screen. Reproduced exactly here rather than clamping the loop,
// because that is the behaviour a script relies on.

namespace sk_bindings {

// `player+0xb34`'s record count -- ids 0..49 have text, 50..255 do not.
constexpr int kQuestTextCount = 0x32;
// The width of the three state arrays, which is *not* the same number.
constexpr int kQuestStateSlots = 0x100;
// FUN_10045334's two base ids.
constexpr int kQuestTitleStringBase = 0x45;
constexpr int kQuestDescriptionStringBase = 0x77;

// One `player+0xb34 + id*4` record. Zero/zero for an id with no text,
// which is how the quest log knows to skip it.
struct QuestText {
    int titleId = 0;
    int descriptionId = 0;

    bool valid() const { return descriptionId != 0; }
};

inline QuestText QuestTextFor(int id) {
    if (id < 0 || id >= kQuestTextCount) return QuestText{};
    QuestText out;
    out.titleId = kQuestTitleStringBase + id;
    // The engine stores this as a 16-bit field but the quest log reads it
    // back as the record's byte 2 (`local_2e`, the low half of the short
    // at +0xb36) -- see FUN_100347c8. Masked here so the two agree, even
    // though no real id gets anywhere near 0xff.
    out.descriptionId = (kQuestDescriptionStringBase + id) & 0xff;
    return out;
}

}  // namespace sk_bindings
