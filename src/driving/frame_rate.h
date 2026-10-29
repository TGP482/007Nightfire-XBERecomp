#pragma once

#include <cstdint>

struct X86Context;

// Driving.xbe's real time updates, from frame_rate.cpp, for the hooks that scale what the game does per console tick
namespace driving {

double UpdateTicks();   // console ticks the running update covers, at most one
bool UpdateTicked();    // the running update crossed a console tick
double FrameTicks();    // console ticks this frame covers
uint32_t FrameTicksCrossed();
uint32_t UpdateCount();
uint32_t TickUpdate();  // the UpdateCount of the update that crossed the last console tick
double TickCarry();  // of a tick, real time the game clock has not counted yet
double Between();    // in a cutscene, the share of the next tick the frame is drawn at, else 1
uint32_t FrameCount();  // of the game loop

double Ease(double gain);      // a share taken once per tick, for this update
double Decay(double factor);   // a factor applied once per tick, for this update

// st0 after the game took one tick's worth, made this update's worth
void AddSteps(X86Context& ctx, double step);       // st0 = value + step
void ApplyFactor(X86Context& ctx, double factor);  // st0 = value * factor
void ScaleShare(X86Context& ctx, double share);    // st0 = difference, about to be multiplied by share

bool Cutscene();  // fov.cpp: a cutscene's camera, the opening level's sniper or a scene the level's script plays

}  // namespace driving
