#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iterator>

#include "common/options.h"

namespace {

// in game::Options::language order after its 0, automatic
constexpr game::Language kLanguages[] = {game::kEnglish, game::kFrench, game::kGerman, game::kItalian,
                                         game::kSpanish, game::kDutch, game::kSwedish};

// the Windows display language when the game has it, else English
game::Language SystemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_FRENCH: return game::kFrench;
        case LANG_GERMAN: return game::kGerman;
        case LANG_ITALIAN: return game::kItalian;
        case LANG_SPANISH: return game::kSpanish;
        case LANG_DUTCH: return game::kDutch;
        case LANG_SWEDISH: return game::kSwedish;
        default: return game::kEnglish;
    }
}

}  // namespace

uint32_t game::LanguageId(uint32_t setting) {
    if (setting == 0) return SystemLanguage();
    return setting <= std::size(kLanguages) ? kLanguages[setting - 1] : kEnglish;
}

uint32_t game::BootLanguage() {
    static const uint32_t language = LanguageId(Opt().language);
    return language;
}
