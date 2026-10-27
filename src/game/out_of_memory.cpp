#include <algorithm>
#include <array>
#include <chrono>

#include "common/options.h"
#include "core/log.h"
#include "xdk/hle.h"

// The game's own heap (0x70AE0 allocates, 0x705F0 frees): a summary every half minute, and what filled it when it
// runs out. Out of memory zeroes its search start and the next allocation reads address 0.
// Sprite batches (0xDCAC0) take a heap list every frame, which splits the heap at high fps; their lists come from
// space taken off the heap's end instead (the heap's free skips it), handed out in turn as the slots are.
// The space is contiguous like the heap: the vertex buffer a list backs holds its physical address.

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
// sprite batch ring: the slot used next, then 12 byte entries whose first field is the drawn batch, 0 when idle
constexpr uint32_t kBatchAllocReturn = 0xDCAF6;
constexpr uint32_t kBatchSlot = 0x2ADF80;
constexpr uint32_t kBatchEntries = 0x2ADF88;
constexpr uint32_t kBatchEntry = 12;
constexpr uint32_t kBatchSlots = 64;
// the lists' space off the heap's end
constexpr uint32_t kListSpace = 4u << 20;

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
std::array<BatchList, kBatchSlots> g_batch_lists;  // the list each slot was last given
uint32_t g_list_space = 0;
uint32_t g_list_next = 0;  // offset in the space

// a slot draws its batch, so reads its list, until its frames run out (0xDCC60)
bool SlotDrawn(uint32_t slot) { return kernel::R32(kBatchEntries + slot * kBatchEntry) != 0; }

bool ListDrawn(uint32_t at, uint32_t size) {
    for (uint32_t slot = 0; slot < kBatchSlots; slot++) {
        const BatchList& list = g_batch_lists[slot];
        if (list.va < at + size && at < list.va + list.size && SlotDrawn(slot)) return true;
    }
    return false;
}

// the slot is idle here. Slots free in the order they were used, so a drawn list at the next place means the space
// is full: 0, and the batch is skipped.
uint32_t AllocBatchList(uint32_t size) {
    if (!g_list_space || size > kListSpace) return 0;
    uint32_t pos = g_list_next + size > kListSpace ? 0 : g_list_next;
    if (ListDrawn(g_list_space + pos, size)) {
        static bool logged = false;
        if (!logged) LOGE("heap: no room for a sprite batch list of %u bytes", size);
        logged = true;
        return 0;
    }
    g_batch_lists[kernel::R32(kBatchSlot) % kBatchSlots] = {g_list_space + pos, size};
    g_list_next = pos + size;
    return g_list_space + pos;
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
        kernel::Return(ctx, AllocBatchList(kernel::R32(ctx.esp + kArgSize)), 0);
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

// at 0x70A72, once the heap's memory is made (0xDC870) and before the heap is laid out in it
void ReserveBatchLists(X86Context&, uint8_t*) {
    uint32_t size = kernel::R32(kHeapSize) - kListSpace;
    kernel::W32(kHeapSize, size);
    g_list_space = kernel::R32(kHeapBase) + size;
}
