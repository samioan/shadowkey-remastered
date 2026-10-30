#include "assets/zone_display_names.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace sk {

int ZoneDisplayNameStringId(const std::string& zoneName) {
    static const std::map<std::string, int> table = {
        {"azra", 3821},     {"delfhide", 3822},     {"erthcave", 3823}, {"ghstpass", 3824},
        {"twilite", 3825},  {"stouttp", 3826},      {"snowline", 3827}, {"broken1", 3828},
        {"broken2", 3829},  {"glaciercrawl", 3830}, {"crypt1", 3831},   {"crypt2", 3832},
        {"crypt3", 3833},   {"fearfrst", 3834},     {"lakvan", 3835},   {"drgnfld", 3836},
        {"lothcav", 3837},  {"dstar_w", 3838},      {"dstar_e", 3839},  {"raiders", 3840},
        // Post-M120: the two `FUN_100290a8` arms M26 never saw, read
        // straight off its literal pool. "lakvans" shares lakvan's string
        // (the function copies 0x3bec for both), and ffarena is the run's
        // last entry, 3841 "Fearfrost II" -- it was never missing.
        {"lakvans", 3835},  {"ffarena", 3841},
    };
    std::string lower = zoneName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto it = table.find(lower);
    return it != table.end() ? it->second : -1;
}

}  // namespace sk
