// Ambient occlusion goes in once a view's opaque list is drawn: the world and its models are lit and in the depth
// buffer, and the sky's front layer, the additive and last lists, the effects and the HUD land on top.
#include "common/options.h"
#include "d3d/occlusion.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

using kernel::G;

constexpr uint16_t kOpaqueList = 0;  // 0xDA890's lists: opaque, additive, then the last ones
constexpr uint32_t kGtao = 2;        // game::Options::ambient_occlusion

// the projection as 0xE84A0 makes it, its columns 16 bytes apart; the draws use it times 0x2FF26C
constexpr uint32_t kProjection = 0x2FF0EC;
constexpr uint32_t kProjectionScale = 0x2FF26C;
constexpr uint32_t kScaleX = 0;
constexpr uint32_t kScaleY = 5;
constexpr uint32_t kCentreX = 2;
constexpr uint32_t kCentreY = 6;
constexpr uint32_t kDepthScale = 10;   // far / (far - near)
constexpr uint32_t kDepthOffset = 11;  // its negative times near
constexpr uint32_t kWFromZ = 14;  // 1: clip w is view z
constexpr uint32_t kWConst = 15;

// the level's linear fog as 0xE3DF0, 0xE3E60 and 0xE3F00 set it, over the scaled view z
constexpr uint32_t kFogOn = 0x2C6FBC;
constexpr uint32_t kFogSlope = 0x2FF33C;   // 1 / (end - start)
constexpr uint32_t kFogOffset = 0x2FF340;  // start / (end - start)

float Projection(uint32_t i) { return G<float>(kProjection + i * 4); }

bool Perspective() {
    bool w_is_z = Projection(kWFromZ) == 1.0f && Projection(kWConst) == 0.0f;
    return w_is_z && Projection(kScaleX) != 0 && Projection(kScaleY) != 0;
}

void ApplyOcclusion(bool gtao) {
    float fog_on = G<uint32_t>(kFogOn) ? 1.0f : 0.0f;
    float fog_slope = G<float>(kFogSlope) * G<float>(kProjectionScale);
    const float p[gpu::kOcclusionParams] = {
        1 / Projection(kScaleX), 1 / Projection(kScaleY), Projection(kCentreX), Projection(kCentreY),
        Projection(kDepthOffset), Projection(kDepthScale), fog_slope, -G<float>(kFogOffset), fog_on,
        gtao ? 1.0f : 0.0f};
    d3d::Occlusion(p);
}

}  // namespace

// one of a view's sorted lists (view, list), cdecl
X86_EXTERN_FUNC(__imp__sub_000DA890);
X86_FUNC(sub_000DA890) {
    uint16_t list = kernel::R16(ctx.esp + 8);
    __imp__sub_000DA890(ctx, base);
    uint32_t mode = game::Opt().ambient_occlusion;
    if (mode && list == kOpaqueList && Perspective()) ApplyOcclusion(mode == kGtao);
}
