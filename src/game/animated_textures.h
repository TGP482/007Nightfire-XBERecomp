#pragma once

// the animated textures' own clock, from animated_textures.cpp
namespace game {

// while one lives, the frame count the animated textures read is their clock; one inside another changes nothing
class TextureClock {
public:
    TextureClock();
    ~TextureClock();
    TextureClock(const TextureClock&) = delete;
    TextureClock& operator=(const TextureClock&) = delete;
};

}  // namespace game
