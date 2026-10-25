#pragma once

// what src/audio/xaudio2.cpp adds to the SDK's audio output
namespace audio {

// any thread: paused, the output stops and the mixer is not run, so every voice and stream keeps its place
void SetPaused(bool paused);

}  // namespace audio
