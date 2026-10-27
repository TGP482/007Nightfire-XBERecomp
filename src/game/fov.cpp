#include <array>
#include <bit>
#include <cmath>

#include "common/options.h"
#include "d3d/occlusion.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// Field of view in first person play, and the first person model's own (the gun and arms): cutscenes, the other
// cameras and the zoom keep the game's angle. The model is drawn with its own projection, and where something asks
// for one of its bones (the shells, the tracers) it gets the bone where that projection shows it.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kViews = 0x1F661C;  // the players' first
constexpr uint32_t kPlayers = 4;
// view: 0x25190 sets the angle and the world to screen scales from it, 0xDD950 projects with angle / zoom
constexpr uint32_t kViewScreenX = 0xEC;  // both by the cotangent of the half angle
constexpr uint32_t kViewScreenY = 0xF0;
constexpr uint32_t kViewZoom = 0xF4;
constexpr uint32_t kViewAspect = 0x100;
constexpr uint32_t kViewAngle = 0x104;  // vertical, radians

constexpr uint32_t kPlayerController = 0xBC;
constexpr uint32_t kPlayerMatrix = 0x70;
// controller
constexpr uint32_t kControllerModel = 0x778;  // the first person model
constexpr uint32_t kControllerPitch = 0x838;  // quarter turns
constexpr uint32_t kControllerPlayer = 0x8DE;
constexpr uint32_t kControllerCamera = 0x8E0;  // 0xA8A60's camera: 0 and 0xE the player's own eyes
constexpr uint8_t kCameraEyes = 0;
constexpr uint8_t kCameraEyesToo = 0xE;
// object: its animation, which holds the parts carried with it (the gun in the hand), as 0xDB320 walks them
constexpr uint32_t kObjectAnimation = 0xB8;
constexpr uint32_t kAnimationRig = 0x74;
constexpr uint32_t kAnimationParts = 0xA0;
constexpr uint32_t kRigPartCount = 0x29;  // byte

// 0xE84A0 (out, aspect, 0, vertical degrees, near, far) makes it, 0xE3AA0 (matrix) sets it into the globals below;
// the draws read the projection times the view, which 0xE4E80 (view matrix) remakes
constexpr uint32_t kMakeProjection = 0xE84A0;
constexpr uint32_t kSetProjection = 0xE3AA0;
constexpr uint32_t kSetView = 0xE4E80;
constexpr uint32_t kViewMatrix = 0x2FF16C;
constexpr float kNear = 0.01f;  // as 0xDD950
constexpr float kFar = 3000.0f;
constexpr uint32_t kProjectionSize = 64;
constexpr uint32_t kProjection = 0x2FF0EC;  // the matrix, then a scaled copy
constexpr uint32_t kProjectionBytes = 2 * kProjectionSize;
constexpr uint32_t kProjectionScale = 0x2FF26C;

// a world matrix, 0x3C bytes: rows 0 to 2 the axes (right, up, forward), 4 floats apart, then the position
constexpr uint32_t kMatrixSize = 0x3C;
constexpr uint32_t kMatrixPosition = 0x30;
constexpr uint32_t kRowStride = 16;
// the eyes as 0xB7C00 makes them: (angle, out) about the side axis, then (by, in out) in out times by
constexpr uint32_t kPitchMatrix = 0xD67B0;
constexpr uint32_t kMultiply = 0xD6D20;
constexpr uint32_t kEyePosition = 0xA89A0;  // (out, player, 0)
constexpr float kRadiansPerQuarter = 1.5707964f;

constexpr float kDegrees = 57.29578f;
constexpr float kHalfGameFov = float(game::kGameFov) / 2 / kDegrees;

struct ViewFov {
    float game_angle = 0;  // as 0x25190 last set it, 0 until then
    uint32_t player = 0;   // while in first person play
    uint32_t model = 0;
};
std::array<ViewFov, kPlayers> g_views;
bool g_drawing = false;  // a first person model draws with its own projection

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

// the game's vertical angle widened as the setting's 4:3 horizontal one is from the game's
float Widen(float angle, uint32_t fov) {
    float scale = std::tan(float(fov) / 2 / kDegrees) / std::tan(kHalfGameFov);
    return 2 * std::atan(std::tan(angle / 2) * scale);
}

void ApplyAngle(uint32_t view, float angle) {
    float now = G<float>(view + kViewAngle);
    if (now == angle) return;
    float scale = std::tan(now / 2) / std::tan(angle / 2);
    G<float>(view + kViewScreenX) *= scale;
    G<float>(view + kViewScreenY) *= scale;
    G<float>(view + kViewAngle) = angle;
}

bool FirstPersonPlay(uint32_t controller, uint32_t view) {
    uint8_t camera = G<uint8_t>(controller + kControllerCamera);
    bool eyes = camera == kCameraEyes || camera == kCameraEyesToo;
    return eyes && G<float>(view + kViewZoom) == 1.0f;
}

void UpdateView(uint32_t player) {
    uint32_t controller = R32(player + kPlayerController);
    int index = G<int8_t>(controller + kControllerPlayer);
    if (index < 0 || index >= int(kPlayers)) return;
    uint32_t view = R32(kViews + uint32_t(index) * 4);
    ViewFov& fov = g_views[size_t(index)];
    if (!view || fov.game_angle == 0) return;
    bool play = FirstPersonPlay(controller, view);
    fov.player = play ? player : 0;
    fov.model = play ? R32(controller + kControllerModel) : 0;
    ApplyAngle(view, play ? Widen(fov.game_angle, game::Opt().fov) : fov.game_angle);
}

bool PartOf(uint32_t model, uint32_t object) {
    if (!model) return false;
    if (object == model) return true;
    uint32_t animation = R32(model + kObjectAnimation);
    uint32_t rig = animation ? R32(animation + kAnimationRig) : 0;
    if (!rig) return false;
    uint32_t parts = R32(animation + kAnimationParts);
    uint32_t count = G<uint8_t>(rig + kRigPartCount);
    for (uint32_t i = 0; i < count; i++) {
        if (R32(parts + i * 4) == object) return true;
    }
    return false;
}

// the view whose first person model, or a part of it, this is while it has an angle of its own; -1 otherwise
int ModelView(uint32_t object) {
    for (uint32_t i = 0; i < kPlayers; i++) {
        const ViewFov& fov = g_views[i];
        if (!object || !PartOf(fov.model, object)) continue;
        uint32_t view = R32(kViews + i * 4);
        float own = Widen(fov.game_angle, game::Opt().viewmodel_fov);
        return own != G<float>(view + kViewAngle) ? int(i) : -1;
    }
    return -1;
}

float ModelAngle(int index) { return Widen(g_views[size_t(index)].game_angle, game::Opt().viewmodel_fov); }

// Drawing the model with its own projection

struct Projection {
    uint8_t matrices[kProjectionBytes];
    float scale;
};

Projection SaveProjection() {
    Projection p;
    for (uint32_t i = 0; i < kProjectionBytes; i++) p.matrices[i] = G<uint8_t>(kProjection + i);
    p.scale = G<float>(kProjectionScale);
    return p;
}

void RestoreProjection(X86Context& ctx, const Projection& p) {
    for (uint32_t i = 0; i < kProjectionBytes; i++) G<uint8_t>(kProjection + i) = p.matrices[i];
    G<float>(kProjectionScale) = p.scale;
    kernel::CallGuest(ctx, kSetView, {kViewMatrix});
}

void SetProjection(X86Context& ctx, uint32_t view, float angle) {
    static const uint32_t matrix = mem::AllocSystem(kProjectionSize);
    float degrees = angle / G<float>(view + kViewZoom) * kDegrees;
    uint32_t aspect = R32(view + kViewAspect);
    kernel::CallGuest(ctx, kMakeProjection, {matrix, aspect, 0, Bits(degrees), Bits(kNear), Bits(kFar)});
    kernel::CallGuest(ctx, kSetProjection, {matrix});
    kernel::CallGuest(ctx, kSetView, {kViewMatrix});
}

// Bones where the model's projection shows them

struct Vec {
    float x;
    float y;
    float z;
};

Vec Read(uint32_t at) { return {G<float>(at), G<float>(at + 4), G<float>(at + 8)}; }

void Write(uint32_t at, Vec v) {
    G<float>(at) = v.x;
    G<float>(at + 4) = v.y;
    G<float>(at + 8) = v.z;
}

float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// the eyes' axes and position; a point's sides and height in the eyes' space grow by scale
struct Squash {
    Vec right;
    Vec up;
    Vec forward;
    Vec eye;
    float scale;

    Vec Direction(Vec v) const {
        float x = Dot(v, right) * scale;
        float y = Dot(v, up) * scale;
        float z = Dot(v, forward);
        return {x * right.x + y * up.x + z * forward.x, x * right.y + y * up.y + z * forward.y,
                x * right.z + y * up.z + z * forward.z};
    }

    Vec Point(Vec p) const {
        Vec moved = Direction({p.x - eye.x, p.y - eye.y, p.z - eye.z});
        return {eye.x + moved.x, eye.y + moved.y, eye.z + moved.z};
    }
};

Squash MakeSquash(X86Context& ctx, int index) {
    static const uint32_t eyes = mem::AllocSystem(kMatrixSize);
    static const uint32_t eye = mem::AllocSystem(sizeof(Vec));
    uint32_t player = g_views[size_t(index)].player;
    uint32_t controller = R32(player + kPlayerController);
    float pitch = -G<float>(controller + kControllerPitch) * kRadiansPerQuarter;
    kernel::CallGuest(ctx, kPitchMatrix, {Bits(pitch), eyes});
    kernel::CallGuest(ctx, kMultiply, {player + kPlayerMatrix, eyes});
    kernel::CallGuest(ctx, kEyePosition, {eye, player, 0});
    float scene = G<float>(R32(kViews + uint32_t(index) * 4) + kViewAngle);
    float scale = std::tan(scene / 2) / std::tan(ModelAngle(index) / 2);
    return {Read(eyes), Read(eyes + kRowStride), Read(eyes + 2 * kRowStride), Read(eye), scale};
}

// a bone's position, and its matrix when asked for, as the model's projection shows them
void SquashBone(const Squash& squash, uint32_t position, uint32_t matrix) {
    Write(position, squash.Point(Read(position)));
    if (!matrix) return;
    for (uint32_t row = 0; row < 3; row++) {
        uint32_t axis = matrix + row * kRowStride;
        Write(axis, squash.Direction(Read(axis)));
    }
    Write(matrix + kMatrixPosition, squash.Point(Read(matrix + kMatrixPosition)));
}

}  // namespace

namespace game {
void ShapeView(X86Context& ctx, uint8_t* base, void (*shape)(X86Context&, uint8_t*));  // widescreen.cpp
}

// a view's angle (view index, radians), cdecl
X86_EXTERN_FUNC(__imp__sub_00025190);
X86_FUNC(sub_00025190) {
    uint16_t index = kernel::R16(ctx.esp + 4);
    float angle = G<float>(ctx.esp + 8);
    game::ShapeView(ctx, base, __imp__sub_00025190);
    if (index >= kPlayers) return;
    uint32_t view = R32(kViews + index * 4u);
    if (view && G<float>(view + kViewAngle) == angle) g_views[index].game_angle = angle;
}

// the map loader starts the game's heap over (0x70AE0 allocates from it), so the models in it are gone
X86_EXTERN_FUNC(__imp__sub_00070A40);
X86_FUNC(sub_00070A40) {
    for (ViewFov& fov : g_views) {
        fov.player = 0;
        fov.model = 0;
    }
    __imp__sub_00070A40(ctx, base);
}

// a player's camera each frame (player)
X86_EXTERN_FUNC(__imp__sub_000A8A60);
X86_FUNC(sub_000A8A60) {
    uint32_t player = R32(ctx.esp + 4);
    __imp__sub_000A8A60(ctx, base);
    UpdateView(player);
}

namespace {

using GuestFn = void (*)(X86Context&, uint8_t*);

// a model draw (object, view, ...) from a view's object lists (0xDA890)
void DrawModel(X86Context& ctx, uint8_t* base, GuestFn original) {
    int index = ModelView(R32(ctx.esp + 4));
    uint32_t view = R32(ctx.esp + 8);
    if (index < 0 || view != R32(kViews + uint32_t(index) * 4)) return original(ctx, base);
    Projection scene = SaveProjection();
    SetProjection(ctx, view, ModelAngle(index));
    g_drawing = true;
    original(ctx, base);
    g_drawing = false;
    RestoreProjection(ctx, scene);
}

}  // namespace

// an actor's render, every character: also kept out of ambient occlusion so their low poly corners stay lit
X86_EXTERN_FUNC(__imp__sub_00016C20);
X86_FUNC(sub_00016C20) {
    bool marked = game::Opt().ambient_occlusion != 0;
    if (marked) d3d::MarkCharacters(true);
    DrawModel(ctx, base, __imp__sub_00016C20);
    if (marked) d3d::MarkCharacters(false);
}

X86_EXTERN_FUNC(__imp__sub_000DA750);
X86_FUNC(sub_000DA750) { DrawModel(ctx, base, __imp__sub_000DA750); }

X86_EXTERN_FUNC(__imp__sub_00068930);
X86_FUNC(sub_00068930) { DrawModel(ctx, base, __imp__sub_00068930); }

// a bone of an animated model (object, bone, part, position out, matrix out or 0), cdecl
X86_EXTERN_FUNC(__imp__sub_00013070);
X86_FUNC(sub_00013070) {
    uint32_t object = R32(ctx.esp + 4);
    uint32_t position = R32(ctx.esp + 16);
    uint32_t matrix = R32(ctx.esp + 20);
    __imp__sub_00013070(ctx, base);
    int index = g_drawing ? -1 : ModelView(object);
    if (index >= 0) SquashBone(MakeSquash(ctx, index), position, matrix);
}
