#pragma once
#include <cstdint>

struct X86Context;

// the codename keyboard, from name_entry.cpp
namespace game {

void UpdateNameEntry(X86Context& ctx, uint32_t page);  // each frame the page takes input: what was typed presses its keys

}  // namespace game
