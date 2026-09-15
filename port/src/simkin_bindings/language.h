#pragma once

// M99: the six languages, `engine+0x14a4c`.
//
// The shipped game carries six string tables and picks one by an integer
// index. Three functions decode that index, each a six-arm switch on the same
// field:
//
//   FUN_1001b708  the file suffix, formatted into
//                 `z:\system\apps\6R51\StringTable.%s`
//   FUN_1001b680  the language's own name, as the language popup shows it
//   case 6 of the GameEngine dispatcher, `GetLanguageStr`: a per-language
//                 "Language: " prefix, then that name
//
// | n | suffix | name        | prefix      |
// |---|--------|-------------|-------------|
// | 0 | eng    | English     | Language:   |
// | 1 | spa    | Español     | Idioma:     |
// | 2 | ger    | Deutsch     | Sprache:    |
// | 3 | fre    | Français    | Langue:     |
// | 4 | ita    | Italiano    | Lingua:     |
// | 5 | euk    | UK English  | Language:   |
//
// Out of range, the suffix and name switches fall to their `default` (eng,
// English) and the prefix switch to its empty initial value. options.s's
// popup lists them in exactly this order, so `SetEnglish` is `SetLanguage(0)`
// through `SetEnglishUK`'s `SetLanguage(5)`.
//
// `stringtable.euk` is byte-identical to `stringtable.eng` in this build.

#include <string>

namespace sk_bindings {

constexpr int kLanguageCount = 6;

// FUN_1001b708. Out of range is "eng".
const char* LanguageFileSuffix(int language);

// FUN_1001b680, Latin-1 (the string table's own encoding, see string_table.h).
const char* LanguageName(int language);

// GetLanguageStr: prefix + name, e.g. "Language: English". **Not "ENGLISH".**
// options.s compares the result against "ENGLISH" to decide whether to offer
// its God Mode row, so in the shipped game that row never appears -- the
// comparison is a leftover from a build whose string differed.
std::string LanguageDisplayString(int language);

}  // namespace sk_bindings
