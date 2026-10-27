#include "common/aspect.h"

#include <atomic>
#include <cstdint>

#include "app/app.h"

namespace {

constexpr float kSameShape = 0.01f;  // 1280x960 and the like are 4:3

}  // namespace

float game::OutputAspect() {
    // asked per sprite: the desktop mode lookup only runs when the resolution setting changes
    static std::atomic<uint64_t> key{~0ull};
    static std::atomic<float> aspect{kWideAspect};
    const app::Settings& s = app::Get();
    uint64_t k = uint64_t(s.width) << 32 | s.height;
    if (key.exchange(k) != k) {
        app::Mode m = app::Output();
        float shape = m.height ? float(m.width) / float(m.height) : kWideAspect;
        aspect = shape < kConsoleAspect + kSameShape ? kConsoleAspect : shape;
    }
    return aspect;
}
