#include "common/game_options.h"

// the driving missions take the options default.xbe set and have no menu of their own for them
const game::GameOption* game::GameOptions(size_t* count) {
    *count = 0;
    return nullptr;
}

void game::SaveChangedOptions(X86Context&) {}
