#include <algorithm>
#include <cstdint>
#include <vector>

#include "common/options.h"
#include "xdk/hle.h"

// The spoken lines are the sounds with a subtitle (0xCBD10's table) and the characters' own lines, the guards' calls
// among them (0x37340's table). A sound's level (0xC50D0) is its linear percent squared over 100, so the voice volume
// scales it before the square, as the music and effects volumes do.
namespace {

constexpr uint32_t kSubtitles = 0x18BCB8;  // (string id, sound id) pairs
constexpr uint32_t kSubtitleCount = 0x1E4;
constexpr uint32_t kPairSize = 8;
constexpr uint32_t kPairSound = 4;
constexpr uint32_t kCharacterLines = 0x174B28;  // a sound id first
constexpr uint32_t kCharacterLineCount = 0x15A;
constexpr uint32_t kCharacterLineSize = 0x20;
constexpr uint32_t kSoundId = 8;  // in the playing sound
constexpr int64_t kFull = 100;

bool Spoken(uint32_t id) {
    static const std::vector<uint32_t> ids = [] {
        std::vector<uint32_t> out;
        for (uint32_t i = 0; i < kSubtitleCount; i++)
            out.push_back(kernel::R32(kSubtitles + i * kPairSize + kPairSound));
        for (uint32_t i = 0; i < kCharacterLineCount; i++) {
            uint32_t id = kernel::R32(kCharacterLines + i * kCharacterLineSize);
            if (id) out.push_back(id);  // 0 an unused line
        }
        std::sort(out.begin(), out.end());
        return out;
    }();
    return std::binary_search(ids.begin(), ids.end(), id);
}

}  // namespace

// a playing sound's level each frame, cdecl (volume, sound)
X86_EXTERN_FUNC(__imp__sub_000C50D0);
X86_FUNC(sub_000C50D0) {
    uint32_t sound = kernel::R32(ctx.esp + 8);
    __imp__sub_000C50D0(ctx, base);
    int64_t voice = std::min<int64_t>(game::Opt().voice_volume, kFull);
    if (voice == kFull || !sound || !Spoken(kernel::R32(sound + kSoundId))) return;
    ctx.eax = uint32_t(int64_t(int32_t(ctx.eax)) * voice * voice / (kFull * kFull));
}
