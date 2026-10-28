#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// In-game cutscenes run whole console ticks (frame_rate.cpp), so their scripts play as on the console, and every
// frame draws the scene between the last two ticks. Each object's frame (its matrix, at +0xC) is read the first time
// it is drawn after a tick (0x8F930); a frame the tick copies a place into (0x35610) keeps the place it had before,
// and one the draw copies a place into is drawn between that and the last. Cameras are found by the view's draw
// (0x96EB0) and drawn between their ticks for the whole draw. Characters (0x12840) are skinned from their pose's
// bones with their body's world matrix (0x129C0), both kept from before and after each character's tick and drawn
// between them. A place that jumps further than a car moves in a tick is a cut.
namespace {

using kernel::G;
using kernel::R32;

// a frame: 4 rows of 3 floats, 0x10 apart, the place in the last
constexpr uint32_t kRows = 4;
constexpr uint32_t kRowStride = 0x10;
constexpr uint32_t kPlaceRow = 3;
constexpr uint32_t kObjectFrame = 0xC;

// a camera, as the view's draw copies it (0x96980), the camera at the view's +4
constexpr uint32_t kViewCamera = 0x4;
constexpr uint32_t kCameraWorld = 0x10;  // 4x4, the place at +0x30
constexpr uint32_t kCameraAt = 0x50;     // 4 floats
constexpr uint32_t kCameraView = 0x70;   // 4x4
constexpr uint32_t kCameraAngle = 0xB4;
constexpr uint32_t kCameraSize = 0xB8;
// the player camera's aim, 3 floats along which the crosshair is projected (0x81890)
constexpr uint32_t kPlayerCameraClass = 0x1918AC;
constexpr uint32_t kCameraAim = 0x180;

// a character (0x12840): its animation (+0x4), whose pose (+0x24) holds the bones (+0x100, 4x4) and their count,
// and its body (+0xC), whose world matrix the skin is drawn with (0x14C50)
constexpr uint32_t kCharacterAnimation = 0x4;
constexpr uint32_t kCharacterBody = 0xC;
constexpr uint32_t kAnimationPose = 0x24;
constexpr uint32_t kPoseBones = 0x100;
constexpr uint32_t kPoseBoneCount = 0x108;
constexpr uint32_t kBodyWorld = 0x80;
constexpr uint32_t kBodyRange = 0x140;  // its distance picks the detail
constexpr uint32_t kMatrixFloats = 16;
constexpr uint32_t kMatrixPlace = 12;  // of its floats
constexpr uint32_t kBoneSize = 0x40;
constexpr uint32_t kMostBones = 256;

constexpr float kCut = 8.0f;  // world units, a car moves well under 1 a tick
constexpr uint32_t kStackBelow = 0x1000;  // copies into the stack are temporaries
constexpr uint32_t kStackAbove = 0x10000;

struct Frame {
    float rows[kRows][3];
};

struct Moving {
    Frame before;
    Frame after;
    uint32_t copied = 0;   // the tick a place was copied in
    uint32_t sampled = 0;  // the tick after was read
    uint32_t drawn = 0;    // the draw it was last shown in
    bool object = false;   // an object's frame, not a scratch matrix
};

// what a character's draw reads
struct Skin {
    uint32_t body = 0;
    uint32_t bones = 0;
    std::vector<float> pose;
    float world[kMatrixFloats];
    float range[kMatrixFloats];
};

struct Character {
    Skin before;
    Skin after;
    uint32_t updated = 0;  // the tick it last ran
};

struct CameraPose {
    float world[16];
    float at[4];
    float view[16];
    float angle;
    float aim[3];
};

struct Camera {
    CameraPose before;
    CameraPose after;  // as the game left it
    uint32_t seen = 0;  // the draw a view last drew with it
    bool shown = false;
};

// ticks are named by the game loop's frame that ran them
uint32_t g_tick = 0;
uint32_t g_last_tick = 0;
bool g_drawing = false;
uint32_t g_draw = 0;
std::unordered_map<uint32_t, Moving> g_moving;  // by frame address
std::unordered_map<uint32_t, Camera> g_cameras;
std::unordered_map<uint32_t, Character> g_characters;
std::vector<uint32_t> g_shown;  // frames drawn between ticks, put back after the draw

Frame ReadFrame(uint32_t address) {
    Frame frame;
    for (uint32_t row = 0; row < kRows; row++) std::memcpy(frame.rows[row], &G<float>(address + row * kRowStride), 12);
    return frame;
}

void WriteFrame(uint32_t address, const Frame& frame) {
    for (uint32_t row = 0; row < kRows; row++) std::memcpy(&G<float>(address + row * kRowStride), frame.rows[row], 12);
}

float Distance(const float* a, const float* b) {
    return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

void Lerp(float* out, const float* before, const float* after, int count, float share) {
    for (int i = 0; i < count; i++) out[i] = before[i] + (after[i] - before[i]) * share;
}

bool Interpolating() { return driving::Cutscene() && driving::Between() < 1.0; }

// a new tick drops the frames the last one did not draw
void NoteTick() {
    uint32_t frame = driving::FrameCount();
    if (frame == g_tick) return;
    g_last_tick = g_tick;
    g_tick = frame;
    for (auto it = g_moving.begin(); it != g_moving.end();) {
        if (it->second.sampled != g_last_tick && it->second.copied != g_tick) it = g_moving.erase(it);
        else ++it;
    }
}

// the camera's own copy is drawn between its ticks with its view (DrivingCameraCopy)
bool InCamera(uint32_t address) {
    for (auto& entry : g_cameras) {
        if (address >= entry.first && address < entry.first + kCameraSize) return true;
    }
    return false;
}

void Sample(uint32_t address, Moving& moving) {
    if (moving.sampled == g_tick) return;
    Frame now = ReadFrame(address);
    if (moving.copied == g_tick) {
        moving.after = now;
    } else if (moving.sampled == g_last_tick) {
        moving.before = moving.after;
        moving.after = now;
    } else {
        moving.before = now;
        moving.after = now;
    }
    moving.sampled = g_tick;
}

// draws a frame between its last two ticks, once per draw
void Show(uint32_t address) {
    if (!address || InCamera(address)) return;
    Moving& moving = g_moving[address];
    moving.object = true;
    if (moving.drawn == g_draw) return;
    moving.drawn = g_draw;
    Sample(address, moving);
    if (Distance(moving.before.rows[kPlaceRow], moving.after.rows[kPlaceRow]) > kCut) return;
    Frame between;
    Lerp(&between.rows[0][0], &moving.before.rows[0][0], &moving.after.rows[0][0], kRows * 3, float(driving::Between()));
    WriteFrame(address, between);
    g_shown.push_back(address);
}

// a place the draw copied into an object's frame, drawn between it and the last tick's
void Blend(uint32_t address) {
    auto it = g_moving.find(address);
    if (it == g_moving.end() || !it->second.object) return;
    Moving& moving = it->second;
    Frame now = ReadFrame(address);
    if (moving.sampled != g_tick) {
        moving.before = moving.sampled == g_last_tick ? moving.after : now;
        moving.sampled = g_tick;
    }
    moving.after = now;
    if (Distance(moving.before.rows[kPlaceRow], now.rows[kPlaceRow]) > kCut) return;
    Frame between;
    Lerp(&between.rows[0][0], &moving.before.rows[0][0], &now.rows[0][0], kRows * 3, float(driving::Between()));
    WriteFrame(address, between);
    g_shown.push_back(address);
}

void PutBack() {
    for (uint32_t address : g_shown) WriteFrame(address, g_moving[address].after);
    g_shown.clear();
}

bool HasAim(uint32_t camera) { return R32(camera) == kPlayerCameraClass; }

CameraPose ReadCamera(uint32_t camera) {
    CameraPose pose{};
    std::memcpy(pose.world, &G<float>(camera + kCameraWorld), sizeof(pose.world));
    std::memcpy(pose.at, &G<float>(camera + kCameraAt), sizeof(pose.at));
    std::memcpy(pose.view, &G<float>(camera + kCameraView), sizeof(pose.view));
    pose.angle = G<float>(camera + kCameraAngle);
    if (HasAim(camera)) std::memcpy(pose.aim, &G<float>(camera + kCameraAim), sizeof(pose.aim));
    return pose;
}

void WriteCamera(uint32_t camera, const CameraPose& pose) {
    std::memcpy(&G<float>(camera + kCameraWorld), pose.world, sizeof(pose.world));
    std::memcpy(&G<float>(camera + kCameraAt), pose.at, sizeof(pose.at));
    std::memcpy(&G<float>(camera + kCameraView), pose.view, sizeof(pose.view));
    G<float>(camera + kCameraAngle) = pose.angle;
    if (HasAim(camera)) std::memcpy(&G<float>(camera + kCameraAim), pose.aim, sizeof(pose.aim));
}

Skin ReadSkin(uint32_t character) {
    Skin skin;
    skin.body = R32(character + kCharacterBody);
    if (!skin.body) return skin;
    std::memcpy(skin.world, &G<float>(skin.body + kBodyWorld), sizeof(skin.world));
    std::memcpy(skin.range, &G<float>(skin.body + kBodyRange), sizeof(skin.range));
    uint32_t animation = R32(character + kCharacterAnimation);
    uint32_t pose = animation ? R32(animation + kAnimationPose) : 0;
    uint32_t bones = pose ? R32(pose + kPoseBones) : 0;
    uint32_t count = pose ? R32(pose + kPoseBoneCount) : 0;
    if (!bones || !count || count > kMostBones) return skin;
    skin.bones = bones;
    skin.pose.resize(count * kMatrixFloats);
    std::memcpy(skin.pose.data(), &G<float>(bones), count * kBoneSize);
    return skin;
}

void WriteSkin(const Skin& skin) {
    if (!skin.body) return;
    std::memcpy(&G<float>(skin.body + kBodyWorld), skin.world, sizeof(skin.world));
    std::memcpy(&G<float>(skin.body + kBodyRange), skin.range, sizeof(skin.range));
    if (skin.bones) std::memcpy(&G<float>(skin.bones), skin.pose.data(), skin.pose.size() * sizeof(float));
}

void ShowSkin(const Character& tracked, float share) {
    const Skin& before = tracked.before;
    const Skin& after = tracked.after;
    if (!after.body || before.body != after.body) return;
    if (Distance(&before.world[kMatrixPlace], &after.world[kMatrixPlace]) <= kCut) {
        Lerp(&G<float>(after.body + kBodyWorld), before.world, after.world, kMatrixFloats, share);
        Lerp(&G<float>(after.body + kBodyRange), before.range, after.range, kMatrixFloats, share);
    }
    bool same_pose = before.bones == after.bones && before.pose.size() == after.pose.size();
    if (!after.bones || !same_pose) return;
    if (Distance(&before.pose[kMatrixPlace], &after.pose[kMatrixPlace]) > kCut) return;
    Lerp(&G<float>(after.bones), before.pose.data(), after.pose.data(), int(after.pose.size()), share);
}

// the characters the last tick ran, between their skins before and after it
void ShowCharacters() {
    float share = float(driving::Between());
    for (auto it = g_characters.begin(); it != g_characters.end();) {
        if (it->second.updated != g_tick && it->second.updated != g_last_tick) {
            it = g_characters.erase(it);
            continue;
        }
        if (it->second.updated == g_tick) ShowSkin(it->second, share);
        ++it;
    }
}

// the skins as the tick left them
void PutCharactersBack() {
    for (auto& [character, tracked] : g_characters) {
        if (tracked.updated == g_tick) WriteSkin(tracked.after);
    }
}

// each draw: a camera the game moved since the last draw keeps where it was as before, unless this draw is not
// between ticks
void SampleCameras(bool between) {
    for (auto it = g_cameras.begin(); it != g_cameras.end();) {
        if (it->second.seen + 1 < g_draw) it = g_cameras.erase(it);
        else ++it;
    }
    for (auto& [camera, tracked] : g_cameras) {
        CameraPose now = ReadCamera(camera);
        if (std::memcmp(&now, &tracked.after, sizeof(now)) != 0) {
            tracked.before = between ? tracked.after : now;
            tracked.after = now;
        } else if (!between) {
            tracked.before = now;
        }
    }
}

// the cameras the views drew with last time, between their last two places for this draw
void ShowCameras() {
    float share = float(driving::Between());
    for (auto& [camera, tracked] : g_cameras) {
        if (Distance(&tracked.before.world[12], &tracked.after.world[12]) > kCut) continue;
        CameraPose between;
        Lerp(between.world, tracked.before.world, tracked.after.world, 16, share);
        Lerp(between.at, tracked.before.at, tracked.after.at, 4, share);
        Lerp(between.view, tracked.before.view, tracked.after.view, 16, share);
        between.angle = tracked.before.angle + (tracked.after.angle - tracked.before.angle) * share;
        Lerp(between.aim, tracked.before.aim, tracked.after.aim, 3, share);
        WriteCamera(camera, between);
        tracked.shown = true;
    }
}

// the cameras as the game left them
void PutCamerasBack() {
    for (auto& [camera, tracked] : g_cameras) {
        if (tracked.shown) WriteCamera(camera, tracked.after);
        tracked.shown = false;
    }
}

}  // namespace

// a place copied into a frame (frame, place), thiscall: the frame keeps the place it had before the tick
X86_EXTERN_FUNC(__imp__sub_00035610);
X86_FUNC(sub_00035610) {
    uint32_t frame = ctx.ecx;
    bool temporary = frame >= ctx.esp - kStackBelow && frame < ctx.esp + kStackAbove;
    if (g_drawing) {
        __imp__sub_00035610(ctx, base);
        if (!temporary && Interpolating()) Blend(frame);
        return;
    }
    NoteTick();  // on every tick, so a cutscene's first draw finds none of the last cutscene's ticks
    if (driving::Cutscene() && !temporary) {
        Moving& moving = g_moving[frame];
        if (moving.copied != g_tick && moving.sampled != g_tick) moving.before = ReadFrame(frame);
        moving.copied = g_tick;
    }
    __imp__sub_00035610(ctx, base);
}

// an object's draw, thiscall: its frame is drawn between its last two ticks
X86_EXTERN_FUNC(__imp__sub_0008F930);
X86_FUNC(sub_0008F930) {
    if (g_drawing && Interpolating()) Show(R32(ctx.ecx + kObjectFrame));
    __imp__sub_0008F930(ctx, base);
}

// a character's update, thiscall: its skin is drawn between before and after it
X86_EXTERN_FUNC(__imp__sub_00012840);
X86_FUNC(sub_00012840) {
    uint32_t character = ctx.ecx;
    if (!driving::Cutscene() || g_drawing) {
        __imp__sub_00012840(ctx, base);
        return;
    }
    NoteTick();
    Skin before = ReadSkin(character);
    __imp__sub_00012840(ctx, base);
    Character& tracked = g_characters[character];
    tracked.before = std::move(before);
    tracked.after = ReadSkin(character);
    tracked.updated = g_tick;
}

// a character's teardown, thiscall: its skin is no longer drawn
X86_EXTERN_FUNC(__imp__sub_00012EC0);
X86_FUNC(sub_00012EC0) {
    g_characters.erase(ctx.ecx);
    __imp__sub_00012EC0(ctx, base);
}

// at 0x96EF0, the view's draw (esi) has copied its camera: a camera the next draws show between its ticks
void DrivingCameraCopy(X86Context& ctx, uint8_t*) {
    uint32_t camera = R32(ctx.esi + kViewCamera);
    if (!g_cameras.count(camera)) {
        Camera& tracked = g_cameras[camera];
        tracked.after = ReadCamera(camera);
        tracked.before = tracked.after;
    }
    g_cameras[camera].seen = g_draw;
}

// the player's draw, thiscall: frames the tick moved are drawn between its last two ticks, then put back
X86_EXTERN_FUNC(__imp__sub_0008BF30);
X86_FUNC(sub_0008BF30) {
    g_drawing = true;
    g_draw++;
    bool between = Interpolating();
    SampleCameras(between);
    if (between) {
        ShowCameras();
        ShowCharacters();
        std::vector<uint32_t> copied;
        for (auto& [address, moving] : g_moving) {
            if (moving.copied == g_tick) copied.push_back(address);
        }
        for (uint32_t address : copied) Show(address);
    }
    __imp__sub_0008BF30(ctx, base);
    PutBack();
    PutCamerasBack();
    if (between) PutCharactersBack();
    g_drawing = false;
}
