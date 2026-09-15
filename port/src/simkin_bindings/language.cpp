#include "simkin_bindings/language.h"

namespace sk_bindings {

const char* LanguageFileSuffix(int language) {
    switch (language) {
        case 1: return "spa";
        case 2: return "ger";
        case 3: return "fre";
        case 4: return "ita";
        case 5: return "euk";
        default: return "eng";
    }
}

const char* LanguageName(int language) {
    switch (language) {
        case 1: return "Espa\xF1ol";
        case 2: return "Deutsch";
        case 3: return "Fran\xE7" "ais";
        case 4: return "Italiano";
        case 5: return "UK English";
        default: return "English";
    }
}

std::string LanguageDisplayString(int language) {
    const char* prefix = "";
    switch (language) {
        case 0:
        case 5: prefix = "Language: "; break;
        case 1: prefix = "Idioma: "; break;
        case 2: prefix = "Sprache: "; break;
        case 3: prefix = "Langue: "; break;
        case 4: prefix = "Lingua: "; break;
        default: break;
    }
    return std::string(prefix) + LanguageName(language);
}

}  // namespace sk_bindings
