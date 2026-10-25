#include <algorithm>
#include <array>
#include <chrono>

#include "common/options.h"
#include "core/log.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// The game's own heap (0x70AE0 allocates, 0x705F0 frees): a summary every half minute, and what filled it when it
// runs out. Out of memory zeroes its search start and the next allocation reads address 0.
// Sprite batches (0xDCAC0) take a heap list every frame, which splits the heap at high fps; each slot keeps its own
// list outside the heap instead (the heap's free skips those).

namespace {

// heap globals
constexpr uint32_t kHeapBase = 0x223A80;
constexpr uint32_t kHeapSize = 0x223A88;
constexpr uint32_t kTagNames = 0x17C298;  // a name per tag
constexpr uint32_t kTags = 0x50;
constexpr uint32_t kXboxTag = 18;
// block header, 12 bytes: +4 size with header, +9 tag, +0xB free
constexpr uint32_t kBlockHeader = 0xC;
constexpr uint32_t kBlockSize = 0x4;
constexpr uint32_t kBlockTag = 0x9;
constexpr uint32_t kBlockFree = 0xB;
// allocator (size, flags, alignment), cdecl; the tag is the flags' second byte
constexpr uint32_t kArgSize = 4;
constexpr uint32_t kArgFlags = 8;
// 0x709C0's stack at entry: return, the allocator's four saved registers, its return, then its arguments
constexpr uint32_t kOutOfMemoryArgs = 0x14;
constexpr std::chrono::seconds kSummaryPeriod{30};
// sprite batch ring
constexpr uint32_t kBatchAllocReturn = 0xDCAF6;
constexpr uint32_t kBatchSlot = 0x2ADF80;
constexpr uint32_t kBatchSlots = 64;

struct HeapUse {
    std::array<uint32_t, kTags> tag_bytes{};
    uint32_t free_total = 0;
    uint32_t free_largest = 0;
    uint32_t free_blocks = 0;
    uint32_t used_blocks = 0;
};

HeapUse ReadHeapUse() {
    HeapUse use;
    uint32_t end = kernel::R32(kHeapBase) + kernel::R32(kHeapSize) - kBlockHeader;
    for (uint32_t b = kernel::R32(kHeapBase); b < end; b += kernel::R32(b + kBlockSize)) {
        uint32_t size = kernel::R32(b + kBlockSize);
        if (size == 0) break;
        if (kernel::G<uint8_t>(b + kBlockFree) == 0) {
            use.tag_bytes[kernel::G<uint8_t>(b + kBlockTag) % kTags] += size;
            use.used_blocks++;
            continue;
        }
        use.free_total += size;
        use.free_largest = std::max(use.free_largest, size);
        use.free_blocks++;
    }
    return use;
}

struct BatchList {
    uint32_t va = 0;
    uint32_t size = 0;
};
std::array<BatchList, kBatchSlots> g_batch_lists;

// the slot is idle here, so its old list is no longer drawn
uint32_t EnsureBatchList(uint32_t size) {
    BatchList& list = g_batch_lists[kernel::R32(kBatchSlot) % kBatchSlots];
    if (list.size < size) {
        if (list.va) mem::FreeSystem(list.va);
        list.va = mem::AllocSystem(size);
        list.size = size;
    }
    return list.va;
}

void LogSummary(uint32_t allocations, double seconds) {
    HeapUse use = ReadHeapUse();
    LOGR("heap: %.0f allocations/s at %.0f fps, %u used blocks, xbox %u, free %u in %u blocks, largest %u",
         allocations / seconds, double(game::g_fps.load()), use.used_blocks, use.tag_bytes[kXboxTag], use.free_total,
         use.free_blocks, use.free_largest);
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00070AE0);
X86_FUNC(sub_00070AE0) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now();
    static uint32_t allocations = 0;
    allocations++;
    clock::time_point now = clock::now();
    if (now - last >= kSummaryPeriod) {
        LogSummary(allocations, std::chrono::duration<double>(now - last).count());
        allocations = 0;
        last = now;
    }
    if (kernel::R32(ctx.esp) == kBatchAllocReturn) {
        kernel::Return(ctx, EnsureBatchList(kernel::R32(ctx.esp + kArgSize)), 0);
        return;
    }
    __imp__sub_00070AE0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000709C0);
X86_FUNC(sub_000709C0) {
    uint32_t args = ctx.esp + kOutOfMemoryArgs;
    uint32_t caller = kernel::R32(args);
    uint32_t request = kernel::R32(args + kArgSize);
    uint32_t tag = (kernel::R32(args + kArgFlags) >> 8) & 0xFF;
    __imp__sub_000709C0(ctx, base);
    HeapUse use = ReadHeapUse();
    LOGR("heap: out of memory, %u bytes tag %u from %08X; free %u in %u blocks, largest %u", request, tag, caller,
         use.free_total, use.free_blocks, use.free_largest);
    for (uint32_t t = 0; t < kTags; t++) {
        if (use.tag_bytes[t] == 0) continue;
        uint32_t name = kernel::R32(kTagNames + t * 4);
        LOGR("heap:   tag %u %s %u", t, name ? kernel::ReadCString(name, 64).c_str() : "", use.tag_bytes[t]);
    }
}
