#pragma once
#include <cstdint>

// the games' texts as on PC, from pc_wording.cpp; each image's text lookup hands its texts through
namespace game {

uint32_t PcWording(uint32_t text);  // a guest text: itself, or a copy that names the PC where it named the console

}  // namespace game
