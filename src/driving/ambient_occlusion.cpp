// Ambient occlusion goes in once a view's opaque passes are drawn (0x8CFA0): the sky, the world's opaque lists, the
// cars and the people are lit and in the depth buffer, and the world's alpha list (0xC8370), the effects and the HUD
// land on top.
#include "common/options.h"
#include "d3d/occlusion.h"
#include "xdk/hle.h"

namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kGtao = 2;  // game::Options::ambient_occlusion

// the view (esi at 0x8D04A): its camera, whose projection 0xE46D0 makes at +0x40, D3DX's row layout
constexpr uint32_t kViewCamera = 0x18;
constexpr uint32_t kCameraProjection = 0x40;
constexpr uint32_t kScaleX = 0;
constexpr uint32_t kScaleY = 5;
constexpr uint32_t kCentreX = 8;
constexpr uint32_t kCentreY = 9;
constexpr uint32_t kDepthScale = 10;  // far / (far - near)
constexpr uint32_t kWFromZ = 11;      // 1: clip w is view z
constexpr uint32_t kDepthOffset = 14;  // its negative times near
constexpr uint32_t kWConst = 15;

// the fog render states 0xE7950 to 0xE7A20 write into D3D_RenderState (0x175628), by D3DRS index 92 to 95
constexpr uint32_t kFogEnable = 0x175798;
constexpr uint32_t kFogTableMode = 0x17579C;
constexpr uint32_t kFogStart = 0x1757A0;
constexpr uint32_t kFogEnd = 0x1757A4;
constexpr uint32_t kFogLinear = 3;  // D3DFOG_LINEAR

struct Fog {
    float slope = 0;
    float offset = 0;
    float on = 0;
};

float Projection(uint32_t camera, uint32_t i) { return G<float>(camera + kCameraProjection + i * 4); }

bool Perspective(uint32_t camera) {
    bool w_is_z = Projection(camera, kWFromZ) == 1.0f && Projection(camera, kWConst) == 0.0f;
    return w_is_z && Projection(camera, kScaleX) != 0 && Projection(camera, kScaleY) != 0;
}

// the linear fog over view z; other modes are left out
Fog LinearFog() {
    float start = G<float>(kFogStart);
    float end = G<float>(kFogEnd);
    bool linear = R32(kFogEnable) && R32(kFogTableMode) == kFogLinear && end > start;
    if (!linear) return {};
    return {1 / (end - start), start / (end - start), 1.0f};
}

void ApplyOcclusion(uint32_t camera, bool gtao) {
    Fog fog = LinearFog();
    const float p[gpu::kOcclusionParams] = {
        1 / Projection(camera, kScaleX), 1 / Projection(camera, kScaleY), Projection(camera, kCentreX),
        Projection(camera, kCentreY), Projection(camera, kDepthOffset), Projection(camera, kDepthScale), fog.slope,
        -fog.offset, fog.on, gtao ? 1.0f : 0.0f, 1.0f};
    d3d::Occlusion(p);
}

}  // namespace

// at 0x8D04A, after the cars and people and before the world's alpha list; the view in esi
void DrivingAmbientOcclusion(X86Context& ctx, uint8_t*) {
    uint32_t mode = game::Opt().ambient_occlusion;
    uint32_t camera = R32(ctx.esi + kViewCamera);
    if (mode && camera && Perspective(camera)) ApplyOcclusion(camera, mode == kGtao);
}
