#include <NoGraphicsAPI/NoGraphicsAPI.hpp>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#include <webgpu/webgpu.h>
#elif defined(__APPLE__)
#include <dawn/webgpu.h>
#include <objc/message.h>
#include <objc/runtime.h>
#else
#include <webgpu/webgpu.h>
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(NDEBUG)
#undef assert
#define assert(expression) ((void)sizeof(static_cast<bool>(expression)))
#endif

namespace gpu
{

namespace
{

thread_local ErrorCallback error_callback = nullptr;

void report_error(const char* operation, const char* message, bool fatal = false) noexcept
{
    if (error_callback)
    {
        char buffer[1024];
        snprintf(buffer, sizeof(buffer), "WebGPU %s: %s", operation, message ? message : "unknown error");
        error_callback(buffer, fatal);
    }
    else
    {
        fprintf(stderr, "NoGraphicsAPI WebGPU: %s: %s\n", operation, message ? message : "unknown error");
    }
}

void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) noexcept
{
    (void)type;
    report_error("uncaptured_error", message.data ? message.data : "unknown error");
}

void on_device_lost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*) noexcept
{
    (void)reason;
    report_error("device_lost", message.data ? message.data : "device lost", true);
}

struct AdapterSync
{
    WGPUAdapter adapter = nullptr;
    bool done = false;
};

void on_request_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* u1, void*) noexcept
{
    AdapterSync* sync = static_cast<AdapterSync*>(u1);
    if (status == WGPURequestAdapterStatus_Success)
    {
        sync->adapter = adapter;
    }
    else
    {
        report_error("request_adapter", message.data ? message.data : "adapter request failed");
    }
    sync->done = true;
}

struct DeviceSync
{
    WGPUDevice device = nullptr;
    bool done = false;
};

void on_request_device(WGPURequestDeviceStatus status, WGPUDevice dev, WGPUStringView message, void* u1, void*) noexcept
{
    DeviceSync* sync = static_cast<DeviceSync*>(u1);
    if (status == WGPURequestDeviceStatus_Success)
    {
        sync->device = dev;
    }
    else
    {
        report_error("request_device", message.data ? message.data : "device request failed");
    }
    sync->done = true;
}

#if !defined(__EMSCRIPTEN__)
struct IdleSync
{
    bool done = false;
};

void on_idle_work_done(WGPUQueueWorkDoneStatus, WGPUStringView, void* u1, void*) noexcept
{
    static_cast<IdleSync*>(u1)->done = true;
}
#endif

WGPUTextureFormat to_wgpu_format(Format format) noexcept
{
    switch (format)
    {
    case Format::r8_unorm: return WGPUTextureFormat_R8Unorm;
    case Format::r8_uint: return WGPUTextureFormat_R8Uint;
    case Format::rg8_unorm: return WGPUTextureFormat_RG8Unorm;
    case Format::rg8_uint: return WGPUTextureFormat_RG8Uint;
    case Format::rgba8_unorm: return WGPUTextureFormat_RGBA8Unorm;
    case Format::rgba8_srgb: return WGPUTextureFormat_RGBA8UnormSrgb;
    case Format::rgba8_uint: return WGPUTextureFormat_RGBA8Uint;
    case Format::bgra8_unorm: return WGPUTextureFormat_BGRA8Unorm;
    case Format::bgra8_srgb: return WGPUTextureFormat_BGRA8UnormSrgb;
    case Format::r16_unorm: return WGPUTextureFormat_R16Unorm;
    case Format::r16_uint: return WGPUTextureFormat_R16Uint;
    case Format::r16_float: return WGPUTextureFormat_R16Float;
    case Format::rg16_unorm: return WGPUTextureFormat_RG16Unorm;
    case Format::rg16_uint: return WGPUTextureFormat_RG16Uint;
    case Format::rg16_float: return WGPUTextureFormat_RG16Float;
    case Format::rgba16_unorm: return WGPUTextureFormat_RGBA16Unorm;
    case Format::rgba16_uint: return WGPUTextureFormat_RGBA16Uint;
    case Format::rgba16_float: return WGPUTextureFormat_RGBA16Float;
    case Format::r32_uint: return WGPUTextureFormat_R32Uint;
    case Format::r32_float: return WGPUTextureFormat_R32Float;
    case Format::rg32_uint: return WGPUTextureFormat_RG32Uint;
    case Format::rg32_float: return WGPUTextureFormat_RG32Float;
    case Format::rgba32_uint: return WGPUTextureFormat_RGBA32Uint;
    case Format::rgba32_float: return WGPUTextureFormat_RGBA32Float;
    case Format::rgb10a2_unorm: return WGPUTextureFormat_RGB10A2Unorm;
    case Format::rg11b10_float: return WGPUTextureFormat_RG11B10Ufloat;
    case Format::d16_unorm: return WGPUTextureFormat_Depth16Unorm;
    case Format::d24_unorm_s8_uint: return WGPUTextureFormat_Depth24PlusStencil8;
    case Format::d32_float: return WGPUTextureFormat_Depth32Float;
    case Format::s8_uint: return WGPUTextureFormat_Stencil8;
    case Format::d32_float_s8_uint: return WGPUTextureFormat_Depth32FloatStencil8;
    case Format::bc3_unorm: return WGPUTextureFormat_BC3RGBAUnorm;
    case Format::bc3_srgb: return WGPUTextureFormat_BC3RGBAUnormSrgb;
    case Format::bc5_rg: return WGPUTextureFormat_BC5RGUnorm;
    case Format::bc6h_ufloat: return WGPUTextureFormat_BC6HRGBUfloat;
    case Format::bc6h_sfloat: return WGPUTextureFormat_BC6HRGBFloat;
    case Format::bc7_unorm: return WGPUTextureFormat_BC7RGBAUnorm;
    case Format::bc7_srgb: return WGPUTextureFormat_BC7RGBAUnormSrgb;
    default: return WGPUTextureFormat_Undefined;
    }
}

WGPUCullMode to_wgpu_cull_mode(CullMode cull) noexcept
{
    switch (cull)
    {
    case CullMode::none: return WGPUCullMode_None;
    case CullMode::clockwise: return WGPUCullMode_Back;
    case CullMode::counter_clockwise: return WGPUCullMode_Front;
    }
    return WGPUCullMode_None;
}

WGPUBlendFactor to_wgpu_blend_factor(BlendFactor factor) noexcept
{
    switch (factor)
    {
    case BlendFactor::zero: return WGPUBlendFactor_Zero;
    case BlendFactor::one: return WGPUBlendFactor_One;
    case BlendFactor::source_color: return WGPUBlendFactor_Src;
    case BlendFactor::one_minus_source_color: return WGPUBlendFactor_OneMinusSrc;
    case BlendFactor::destination_color: return WGPUBlendFactor_Dst;
    case BlendFactor::one_minus_destination_color: return WGPUBlendFactor_OneMinusDst;
    case BlendFactor::source_alpha: return WGPUBlendFactor_SrcAlpha;
    case BlendFactor::one_minus_source_alpha: return WGPUBlendFactor_OneMinusSrcAlpha;
    case BlendFactor::destination_alpha: return WGPUBlendFactor_DstAlpha;
    case BlendFactor::one_minus_destination_alpha: return WGPUBlendFactor_OneMinusDstAlpha;
    case BlendFactor::source_alpha_saturate: return WGPUBlendFactor_SrcAlphaSaturated;
    }
    return WGPUBlendFactor_One;
}

WGPUBlendOperation to_wgpu_blend_op(BlendOp op) noexcept
{
    switch (op)
    {
    case BlendOp::add: return WGPUBlendOperation_Add;
    case BlendOp::subtract: return WGPUBlendOperation_Subtract;
    case BlendOp::reverse_subtract: return WGPUBlendOperation_ReverseSubtract;
    case BlendOp::minimum: return WGPUBlendOperation_Min;
    case BlendOp::maximum: return WGPUBlendOperation_Max;
    }
    return WGPUBlendOperation_Add;
}

WGPUAddressMode to_wgpu_address_mode(AddressMode mode) noexcept
{
    switch (mode)
    {
    case AddressMode::repeat: return WGPUAddressMode_Repeat;
    case AddressMode::mirrored_repeat: return WGPUAddressMode_MirrorRepeat;
    case AddressMode::clamp_to_edge: return WGPUAddressMode_ClampToEdge;
    }
    return WGPUAddressMode_ClampToEdge;
}

WGPUFilterMode to_wgpu_filter(Filter filter) noexcept
{
    switch (filter)
    {
    case Filter::nearest: return WGPUFilterMode_Nearest;
    case Filter::linear: return WGPUFilterMode_Linear;
    }
    return WGPUFilterMode_Nearest;
}

WGPUMipmapFilterMode to_wgpu_mip_filter(Filter filter) noexcept
{
    switch (filter)
    {
    case Filter::nearest: return WGPUMipmapFilterMode_Nearest;
    case Filter::linear: return WGPUMipmapFilterMode_Linear;
    }
    return WGPUMipmapFilterMode_Nearest;
}

WGPUCompareFunction to_wgpu_compare(CompareOp op) noexcept
{
    switch (op)
    {
    case CompareOp::never: return WGPUCompareFunction_Never;
    case CompareOp::less: return WGPUCompareFunction_Less;
    case CompareOp::equal: return WGPUCompareFunction_Equal;
    case CompareOp::less_equal: return WGPUCompareFunction_LessEqual;
    case CompareOp::greater: return WGPUCompareFunction_Greater;
    case CompareOp::not_equal: return WGPUCompareFunction_NotEqual;
    case CompareOp::greater_equal: return WGPUCompareFunction_GreaterEqual;
    case CompareOp::always: return WGPUCompareFunction_Always;
    }
    return WGPUCompareFunction_Undefined;
}

enum class QueueKind : uint8 { general, compute, copy };

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static inline uintptr get_current_thread_id() noexcept
{
    return static_cast<uintptr>(GetCurrentThreadId());
}
#elif defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
#include <sched.h>
static inline uintptr get_current_thread_id() noexcept
{
    return 1;
}
#else
#include <pthread.h>
#include <sched.h>
static inline uintptr get_current_thread_id() noexcept
{
    return (uintptr)pthread_self();
}
#endif

// Backend-specific exception documented per AGENTS.md:
// Dawn Native WGPUQueue and WGPUBuffer are externally synchronized objects; concurrent
// wgpuQueueWriteBuffer and wgpuQueueSubmit on the shared primary_queue / mega_buffer trigger
// Dawn's kConcurrentUseError ("Concurrent buffer operations are not allowed") and Metal command
// encoder assertion failures. We serialize native queue submissions and shared heap bookkeeping
// using lightweight re-entrant atomic spinlocks.
struct AtomicSpinLock
{
    uintptr owner = 0;
    uint32 recursion_count = 0;

    void lock() noexcept
    {
        const uintptr self = get_current_thread_id();
        if (__atomic_load_n(&owner, __ATOMIC_RELAXED) == self)
        {
            recursion_count++;
            return;
        }

        uintptr expected = 0;
        uint32 spin_count = 0;
        while (!__atomic_compare_exchange_n(&owner, &expected, self, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        {
            expected = 0;
            if (++spin_count < 64)
            {
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
                asm volatile("yield");
#endif
            }
            else
            {
#if defined(_WIN32)
                SwitchToThread();
#elif !defined(__EMSCRIPTEN__)
                sched_yield();
#endif
                spin_count = 0;
            }
        }
        recursion_count = 1;
    }

    void unlock() noexcept
    {
        assert(owner == get_current_thread_id());
        if (--recursion_count == 0)
        {
            __atomic_store_n(&owner, uintptr{0}, __ATOMIC_RELEASE);
        }
    }

    bool is_locked_by_current_thread() const noexcept
    {
        return __atomic_load_n(&owner, __ATOMIC_RELAXED) == get_current_thread_id();
    }
};

} // namespace

struct TimelineSemaphore;

struct PendingPoint
{
    uint64 value = 0;
    WGPUFuture future = {};
};

struct TimelineSemaphore
{
    Device* device = nullptr;
    uint64 completed_value = 0;
    uint64 last_submitted_value = 0;
    static constexpr uint32 max_pending = 64;
    PendingPoint pending[max_pending] = {};
    uint32 pending_head = 0;
    uint32 pending_tail = 0;
};

static void advance_timeline_value(uint64* target, uint64 value) noexcept
{
    uint64 cur = __atomic_load_n(target, __ATOMIC_RELAXED);
    while (value > cur && !__atomic_compare_exchange_n(target, &cur, value, true, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
    {
    }
}

void on_timeline_work_done(WGPUQueueWorkDoneStatus status, WGPUStringView, void* u1, void* u2) noexcept
{
    if (status != WGPUQueueWorkDoneStatus_Success || !u1) return;
    TimelineSemaphore* s = static_cast<TimelineSemaphore*>(u1);
    advance_timeline_value(&s->completed_value, static_cast<uint64>(reinterpret_cast<uintptr>(u2)));
}

struct Queue
{
    Device* device = nullptr;
    WGPUQueue queue = nullptr;
    uint32 queue_index = 0;
    QueueKind kind = QueueKind::general;
};

struct RenderView
{
    WGPUTextureView view = nullptr;
    WGPUTexture texture = nullptr;
    Format format = Format::undefined;
    uint32 mip = 0;
    uint32 slice = 0;
};

struct Texture
{
    Device* device = nullptr;
    WGPUTexture texture = nullptr;
    TextureDesc desc = {};
};

struct Swapchain
{
    WGPUSurface surface = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    uint32 width = 0;
    uint32 height = 0;
    WGPUSurfaceTexture current_texture = {};
    WGPUTextureView current_view = nullptr;
    RenderView render_view = {};
    bool configured = false;
    bool acquired = false;
};

struct PSO
{
    Device* device = nullptr;
    WGPURenderPipeline render = nullptr;
    WGPUComputePipeline compute = nullptr;
    RasterizationState rasterization = {};
    bool is_compute = false;
};

struct TextureDescriptorEntry
{
    WGPUTextureView view = nullptr;
    const Texture* texture = nullptr;
    TextureDescriptorType type = TextureDescriptorType::sampled;
    TextureDescriptorDesc desc = {};
};

struct TextureDescriptorHeap
{
    Device* device = nullptr;
    uint32 capacity = 0;
    TextureDescriptorEntry* entries = nullptr;
    uint64 generation = 1;
};

struct SamplerDescriptorEntry
{
    WGPUSampler sampler = nullptr;
    SamplerDesc desc = {};
};

struct SamplerDescriptorHeap
{
    Device* device = nullptr;
    uint32 capacity = 0;
    SamplerDescriptorEntry* entries = nullptr;
    uint64 generation = 1;
};

struct ResourceTableCacheEntry
{
    const TextureDescriptorHeap* texture_heap = nullptr;
    const SamplerDescriptorHeap* sampler_heap = nullptr;
    uint64 texture_generation = 0;
    uint64 sampler_generation = 0;
    uint64 last_use_tick = 0;
    WGPUResourceTable table = nullptr;
};

struct TextureHeapOwner
{
    Device* device = nullptr;
};

struct GpuHeapOwner;

struct ReadbackRange
{
    GpuHeapOwner* heap = nullptr;
    uint64 offset = 0;
    uint64 size = 0;
};

struct GpuHeapOwner
{
    Device* device = nullptr;
    WGPUBuffer buffer = nullptr;
    uint64 byte_count = 0;
    uint64 aligned_size = 0;
    MemoryType memory = MemoryType::cpu_visible;
    byte* cpu_shadow = nullptr;
    byte* synced_snapshot = nullptr;
    void* raw_allocation = nullptr;
    void* raw_snapshot = nullptr;
    uint64 base_address = 0;
    uint32 heap_id = 0;
    GpuHeapOwner* next = nullptr;
    GpuHeapOwner* prev = nullptr;
    uint64 mega_offset = 0;
    GpuHeapOwner* readback_next = nullptr;
    GpuHeapOwner* readback_prev = nullptr;
    TimelineSemaphore* last_writer_semaphore = nullptr;
    uint64 last_writer_value = 0;
    bool pending_readback_sync = false;
    bool needs_initial_sync = true;
    ReadbackRange* pending_ranges = nullptr;
    uint32 pending_range_count = 0;
    uint32 pending_range_capacity = 0;
};

struct CommandBuffer
{
    Device* device = nullptr;
    CommandPool* pool = nullptr;
    CommandBuffer* next = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPURenderPassEncoder render_pass = nullptr;
    WGPUComputePassEncoder compute_pass = nullptr;
    WGPUCommandBuffer recorded = nullptr;
    const PSO* current_pso = nullptr;
    TextureDescriptorHeap* active_texture_heap = nullptr;
    SamplerDescriptorHeap* active_sampler_heap = nullptr;
    static constexpr uint32 max_referenced_heaps = 16;
    GpuHeapOwner* referenced_heaps[max_referenced_heaps] = {};
    uint32 referenced_heap_count = 0;
    bool recording = false;
    bool ended = false;
    bool submitted = false;
    bool acquired = false;

    struct TimestampRecord
    {
        uint64* destination = nullptr;
        uint64 recorded_time = 0;
    };
    TimestampRecord* timestamp_records = nullptr;
    uint32 timestamp_capacity = 0;
    uint32 timestamp_count = 0;

    ReadbackRange* readback_ranges = nullptr;
    uint32 readback_range_count = 0;
    uint32 readback_range_capacity = 0;
};

struct CommandPool
{
    Device* device = nullptr;
    uint32 queue_index = 0;
    CommandBuffer* first = nullptr;
    CommandBuffer* last = nullptr;
    CommandBuffer* next_buffer = nullptr;
    WGPUBuffer scratch_buffer = nullptr;
    uint64 scratch_capacity = 0;
    bool timestamps = false;
};

struct ArenaBlock
{
    uint64 offset = 0;
    uint64 size = 0;
    bool used = false;
    bool gpu_tainted = false;
};

struct DeferredSubmit
{
    CommandBuffer* commands_storage[64] = {};
    uint32 command_count = 0;
    TimelinePoint waits_storage[8] = {};
    uint32 wait_count = 0;
    TimelinePoint completion = {};
    uint32 queue_index = 0;
    bool active = false;
};

struct Device
{
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    Queue* queues = nullptr;
    Swapchain swapchain = {};
    CommandBuffer* acquired = nullptr;
    DeviceCaps caps = {};
    char device_name[128] = {};
    void* native_window = nullptr;
    Format swapchain_format = Format::bgra8_srgb;
    ColorSpace swapchain_color_space = ColorSpace::srgb;
    WGPUResourceTable resource_table = nullptr;
    static constexpr uint32 resource_table_cache_size = 32;
    ResourceTableCacheEntry resource_table_cache[resource_table_cache_size] = {};
    uint64 resource_table_tick = 0;
    WGPUBindGroupLayout graphics_bind_group_layout = nullptr;
    WGPUBindGroupLayout compute_bind_group_layout = nullptr;
    WGPUPipelineLayout default_pipeline_layout = nullptr;
    WGPUPipelineLayout compute_pipeline_layout = nullptr;

    AtomicSpinLock submit_lock = {};
    AtomicSpinLock heap_lock = {};
    AtomicSpinLock table_lock = {};

    static constexpr uint32 max_deferred_submits = 8;
    DeferredSubmit deferred_submits[max_deferred_submits] = {};
    uint32 deferred_submit_count = 0;

    WGPUBuffer mega_buffer = nullptr;
    uint64 mega_buffer_capacity = 512 * 1024 * 1024;
    WGPUBindGroup mega_bind_group = nullptr;
    WGPUBindGroup mega_compute_bind_group = nullptr;
    static constexpr uint32 indirect_dispatch_buffer_capacity = 256 * 1024;
    static constexpr uint32 indirect_dispatch_slot_size = 256;
    static constexpr uint32 indirect_dispatch_slot_count = indirect_dispatch_buffer_capacity / indirect_dispatch_slot_size;
    WGPUBuffer indirect_dispatch_buffer = nullptr;
    uint32 indirect_dispatch_cursor = 0;

    static constexpr uint32 max_arena_blocks = 512;
    ArenaBlock arena_blocks[max_arena_blocks] = {};
    uint32 arena_block_count = 0;

    static constexpr uint32 max_heaps = 256;
    GpuHeapOwner* heap_table[max_heaps] = {};
    GpuHeapOwner* cpu_visible_heaps = nullptr;
    GpuHeapOwner* readback_heaps = nullptr;
    uint64 next_readback_vaddr = 0x8000'0000ull;
    uint64 max_frames = 0;
    uint64 presented_frames = 0;
    uint32 next_heap_id = 1;
    uint32 timestamp_query_count = 256;
};

static WGPUResourceTable resolve_resource_table(Device* device, const TextureDescriptorHeap* tex_heap, const SamplerDescriptorHeap* samp_heap) noexcept
{
    if (!device) return nullptr;
    if (!tex_heap && !samp_heap) return device->resource_table;

    device->table_lock.lock();

    const uint64 tex_gen = tex_heap ? __atomic_load_n(&tex_heap->generation, __ATOMIC_ACQUIRE) : 0;
    const uint64 samp_gen = samp_heap ? __atomic_load_n(&samp_heap->generation, __ATOMIC_ACQUIRE) : 0;

    int32 stale_same_pair = -1;
    int32 free_slot = -1;
    uint32 lru_slot = 0;
    uint64 lru_tick = UINT64_MAX;

    for (uint32 i = 0; i < Device::resource_table_cache_size; ++i)
    {
        ResourceTableCacheEntry& entry = device->resource_table_cache[i];
        if (entry.table)
        {
            if (entry.texture_heap == tex_heap && entry.sampler_heap == samp_heap)
            {
                if (entry.texture_generation == tex_gen && entry.sampler_generation == samp_gen)
                {
                    entry.last_use_tick = ++device->resource_table_tick;
                    const WGPUResourceTable hit = entry.table;
                    device->table_lock.unlock();
                    return hit;
                }
                stale_same_pair = static_cast<int32>(i);
            }
            if (entry.last_use_tick < lru_tick)
            {
                lru_tick = entry.last_use_tick;
                lru_slot = i;
            }
        }
        else if (free_slot < 0)
        {
            free_slot = static_cast<int32>(i);
        }
    }

    const uint32 table_size = samp_heap ? (32768u + samp_heap->capacity) : (tex_heap ? tex_heap->capacity : 1u);
    const WGPUResourceTableDescriptor table_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Bindless Heap Resource Table", .length = WGPU_STRLEN },
        .size = table_size,
    };
    const WGPUResourceTable new_table = wgpuDeviceCreateResourceTable(device->device, &table_desc);
    if (!new_table)
    {
        device->table_lock.unlock();
        return device->resource_table;
    }

    if (tex_heap && tex_heap->entries)
    {
        for (uint32 index = 0; index < tex_heap->capacity; ++index)
        {
            if (tex_heap->entries[index].view)
            {
                const WGPUBindingResource resource = {
                    .nextInChain = nullptr,
                    .buffer = nullptr,
                    .offset = 0,
                    .size = 0,
                    .sampler = nullptr,
                    .textureView = tex_heap->entries[index].view,
                };
                wgpuResourceTableUpdate(new_table, index, &resource);
            }
        }
    }

    if (samp_heap && samp_heap->entries)
    {
        for (uint32 index = 0; index < samp_heap->capacity; ++index)
        {
            if (samp_heap->entries[index].sampler)
            {
                const WGPUBindingResource resource = {
                    .nextInChain = nullptr,
                    .buffer = nullptr,
                    .offset = 0,
                    .size = 0,
                    .sampler = samp_heap->entries[index].sampler,
                    .textureView = nullptr,
                };
                wgpuResourceTableUpdate(new_table, index + 32768, &resource);
            }
        }
    }

    const uint32 target_slot = (stale_same_pair >= 0)
        ? static_cast<uint32>(stale_same_pair)
        : ((free_slot >= 0) ? static_cast<uint32>(free_slot) : lru_slot);
    ResourceTableCacheEntry& dst = device->resource_table_cache[target_slot];
    if (dst.table)
    {
        wgpuResourceTableRelease(dst.table);
    }
    dst = {
        .texture_heap = tex_heap,
        .sampler_heap = samp_heap,
        .texture_generation = tex_gen,
        .sampler_generation = samp_gen,
        .last_use_tick = ++device->resource_table_tick,
        .table = new_table,
    };

    device->table_lock.unlock();
    return new_table;
}

static void evict_resource_tables_for_heap(Device* device, const TextureDescriptorHeap* tex_heap, const SamplerDescriptorHeap* samp_heap) noexcept
{
    if (!device) return;
    device->table_lock.lock();
    for (uint32 i = 0; i < Device::resource_table_cache_size; ++i)
    {
        ResourceTableCacheEntry& entry = device->resource_table_cache[i];
        if (entry.table && ((tex_heap && entry.texture_heap == tex_heap) || (samp_heap && entry.sampler_heap == samp_heap)))
        {
            wgpuResourceTableRelease(entry.table);
            entry = {};
        }
    }
    device->table_lock.unlock();
}

static uint64 arena_allocate(Device* device, uint64 size, bool high_end, bool* out_gpu_tainted) noexcept
{
    assert(device && size > 0);
    if (out_gpu_tainted) *out_gpu_tainted = false;

    if (high_end)
    {
        for (uint32 rev = device->arena_block_count; rev > 0; --rev)
        {
            const uint32 i = rev - 1;
            ArenaBlock& cur = device->arena_blocks[i];
            if (!cur.used && cur.size >= size)
            {
                if (out_gpu_tainted) *out_gpu_tainted = cur.gpu_tainted;
                const uint64 remaining = cur.size - size;
                if (remaining > 0 && device->arena_block_count < Device::max_arena_blocks)
                {
                    const uint64 alloc_offset = cur.offset + remaining;
                    cur.size = remaining;
                    for (uint32 j = device->arena_block_count; j > i + 1; --j)
                    {
                        device->arena_blocks[j] = device->arena_blocks[j - 1];
                    }
                    device->arena_block_count++;
                    device->arena_blocks[i + 1] = {.offset = alloc_offset, .size = size, .used = true, .gpu_tainted = true};
                    return alloc_offset;
                }
                cur.used = true;
                cur.gpu_tainted = true;
                return cur.offset;
            }
        }
        return UINT64_MAX;
    }

    for (uint32 i = 0; i < device->arena_block_count; ++i)
    {
        ArenaBlock& cur = device->arena_blocks[i];
        if (!cur.used && cur.size >= size)
        {
            if (out_gpu_tainted) *out_gpu_tainted = cur.gpu_tainted;
            const uint64 remaining = cur.size - size;
            if (remaining > 0 && device->arena_block_count < Device::max_arena_blocks)
            {
                const bool tainted = cur.gpu_tainted;
                cur.size = size;
                cur.used = true;
                for (uint32 j = device->arena_block_count; j > i + 1; --j)
                {
                    device->arena_blocks[j] = device->arena_blocks[j - 1];
                }
                device->arena_block_count++;
                device->arena_blocks[i + 1] = {.offset = cur.offset + size, .size = remaining, .used = false, .gpu_tainted = tainted};
                return cur.offset;
            }
            cur.used = true;
            return cur.offset;
        }
    }
    return UINT64_MAX;
}

static void arena_free(Device* device, uint64 offset, bool gpu_tainted) noexcept
{
    assert(device);
    for (uint32 i = 0; i < device->arena_block_count; ++i)
    {
        if (device->arena_blocks[i].offset == offset && device->arena_blocks[i].used)
        {
            device->arena_blocks[i].used = false;
            device->arena_blocks[i].gpu_tainted = device->arena_blocks[i].gpu_tainted || gpu_tainted;
            const bool force_coalesce = device->arena_block_count >= Device::max_arena_blocks - 2;
            if (i + 1 < device->arena_block_count && !device->arena_blocks[i + 1].used &&
                (force_coalesce || device->arena_blocks[i + 1].gpu_tainted == device->arena_blocks[i].gpu_tainted))
            {
                device->arena_blocks[i].size += device->arena_blocks[i + 1].size;
                device->arena_blocks[i].gpu_tainted = device->arena_blocks[i].gpu_tainted || device->arena_blocks[i + 1].gpu_tainted;
                for (uint32 j = i + 1; j + 1 < device->arena_block_count; ++j)
                {
                    device->arena_blocks[j] = device->arena_blocks[j + 1];
                }
                device->arena_block_count--;
            }
            if (i > 0 && !device->arena_blocks[i - 1].used &&
                (force_coalesce || device->arena_blocks[i - 1].gpu_tainted == device->arena_blocks[i].gpu_tainted))
            {
                device->arena_blocks[i - 1].size += device->arena_blocks[i].size;
                device->arena_blocks[i - 1].gpu_tainted = device->arena_blocks[i - 1].gpu_tainted || device->arena_blocks[i].gpu_tainted;
                for (uint32 j = i; j + 1 < device->arena_block_count; ++j)
                {
                    device->arena_blocks[j] = device->arena_blocks[j + 1];
                }
                device->arena_block_count--;
            }
            return;
        }
    }
}

struct MapCallbackContext
{
    bool done = false;
    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
};

static void on_buffer_mapped(WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) noexcept
{
    MapCallbackContext* ctx = static_cast<MapCallbackContext*>(userdata1);
    if (ctx)
    {
        ctx->status = status;
        ctx->done = true;
    }
}

static void sync_readback_heaps_locked(Device* device, const TimelineSemaphore* sem = nullptr, uint64 value = 0) noexcept
{
    if (!device) return;
    for (GpuHeapOwner* heap = device->readback_heaps; heap != nullptr; heap = heap->readback_next)
    {
        if (!heap->buffer || !heap->cpu_shadow || heap->aligned_size == 0) continue;
        if (!heap->pending_readback_sync) continue;

        if (sem && heap->last_writer_semaphore && (heap->last_writer_semaphore != sem || heap->last_writer_value > value))
        {
            continue;
        }

        MapCallbackContext map_ctx = { .done = false, .status = WGPUMapAsyncStatus_Error };
        const WGPUBufferMapCallbackInfo cb_info = {
            .nextInChain = nullptr,
            .mode = WGPUCallbackMode_AllowProcessEvents,
            .callback = on_buffer_mapped,
            .userdata1 = &map_ctx,
            .userdata2 = nullptr,
        };
        wgpuBufferMapAsync(heap->buffer, WGPUMapMode_Read, 0, static_cast<size_t>(heap->aligned_size), cb_info);
        while (!map_ctx.done)
        {
            wgpuInstanceProcessEvents(device->instance);
        }
        if (map_ctx.status == WGPUMapAsyncStatus_Success)
        {
            const void* mapped = wgpuBufferGetConstMappedRange(heap->buffer, 0, static_cast<size_t>(heap->aligned_size));
            if (mapped)
            {
                if (heap->pending_range_count > 0)
                {
                    for (uint32 r = 0; r < heap->pending_range_count; ++r)
                    {
                        const ReadbackRange& range = heap->pending_ranges[r];
                        if (range.offset < heap->byte_count)
                        {
                            const uint64 copy_size = (range.offset + range.size <= heap->byte_count)
                                ? range.size : (heap->byte_count - range.offset);
                            memcpy(heap->cpu_shadow + range.offset,
                                   static_cast<const byte*>(mapped) + range.offset,
                                   static_cast<size_t>(copy_size));
                            if (heap->synced_snapshot)
                            {
                                memcpy(heap->synced_snapshot + range.offset,
                                       static_cast<const byte*>(mapped) + range.offset,
                                       static_cast<size_t>(copy_size));
                            }
                        }
                    }
                }
                else
                {
                    memcpy(heap->cpu_shadow, mapped, static_cast<size_t>(heap->byte_count));
                    if (heap->synced_snapshot)
                    {
                        memcpy(heap->synced_snapshot, mapped, static_cast<size_t>(heap->byte_count));
                    }
                }
            }
            wgpuBufferUnmap(heap->buffer);
            heap->pending_readback_sync = false;
            heap->pending_range_count = 0;
        }
    }
}

static void sync_readback_heaps(Device* device, const TimelineSemaphore* sem = nullptr, uint64 value = 0) noexcept
{
    if (!device) return;
    device->submit_lock.lock();
    sync_readback_heaps_locked(device, sem, value);
    device->submit_lock.unlock();
}

static inline void track_command_buffer_heap(CommandBuffer* commands, GpuHeapOwner* owner) noexcept
{
    if (!commands || !owner) return;
    for (uint32 i = 0; i < commands->referenced_heap_count; ++i)
    {
        if (commands->referenced_heaps[i] == owner) return;
    }
    if (commands->referenced_heap_count < CommandBuffer::max_referenced_heaps)
    {
        commands->referenced_heaps[commands->referenced_heap_count++] = owner;
    }
}

static void record_readback_write(CommandBuffer* commands, GpuHeapOwner* heap, uint64 offset, uint64 size) noexcept
{
    if (!commands || !heap || heap->memory != MemoryType::readback || size == 0) return;
    if (commands->readback_range_count > 0)
    {
        ReadbackRange& last = commands->readback_ranges[commands->readback_range_count - 1];
        if (last.heap == heap && last.offset + last.size == offset)
        {
            last.size += size;
            return;
        }
    }
    if (commands->readback_range_count == commands->readback_range_capacity)
    {
        const uint32 new_cap = commands->readback_range_capacity == 0 ? 32 : commands->readback_range_capacity * 2;
        ReadbackRange* new_ranges = static_cast<ReadbackRange*>(malloc(new_cap * sizeof(ReadbackRange)));
        if (commands->readback_ranges)
        {
            memcpy(new_ranges, commands->readback_ranges, commands->readback_range_count * sizeof(ReadbackRange));
            free(commands->readback_ranges);
        }
        commands->readback_ranges = new_ranges;
        commands->readback_range_capacity = new_cap;
    }
    commands->readback_ranges[commands->readback_range_count++] = {
        .heap = heap,
        .offset = offset,
        .size = size,
    };
}

static WGPUBuffer get_or_create_pool_scratch(CommandPool* pool, uint64 required_size) noexcept
{
    assert(pool && pool->device);
    if (pool->scratch_buffer && pool->scratch_capacity >= required_size)
    {
        return pool->scratch_buffer;
    }
    if (pool->scratch_buffer)
    {
        wgpuBufferDestroy(pool->scratch_buffer);
        wgpuBufferRelease(pool->scratch_buffer);
        pool->scratch_buffer = nullptr;
    }
    const uint64 new_cap = (required_size > 1024 * 1024) ? required_size : 1024 * 1024;
    const WGPUBufferDescriptor desc = {
        .nextInChain = nullptr,
        .label = { .data = "CommandPool Scratch Buffer", .length = WGPU_STRLEN },
        .usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst,
        .size = new_cap,
        .mappedAtCreation = WGPU_FALSE,
    };
    pool->device->heap_lock.lock();
    pool->scratch_buffer = wgpuDeviceCreateBuffer(pool->device->device, &desc);
    pool->device->heap_lock.unlock();
    pool->scratch_capacity = new_cap;
    return pool->scratch_buffer;
}

struct GpuAddressInfo
{
    WGPUBuffer buffer = nullptr;
    uint64 offset = 0;
    GpuHeapOwner* owner = nullptr;
};

inline GpuAddressInfo resolve_gpu_address(Device* device, const void* gpu_ptr) noexcept
{
    if (!gpu_ptr || !device) return {};

#if defined(__wasm32__)
    const uint32 addr = static_cast<uint32>(reinterpret_cast<uintptr>(gpu_ptr));
    if (addr < device->mega_buffer_capacity)
    {
        GpuHeapOwner* owner = nullptr;
        for (uint32 i = 1; i < Device::max_heaps; ++i)
        {
            GpuHeapOwner* h = device->heap_table[i];
            if (h && h->buffer == device->mega_buffer &&
                addr >= h->mega_offset && addr < h->mega_offset + h->aligned_size)
            {
                owner = h;
                break;
            }
        }
        return {
            .buffer = device->mega_buffer,
            .offset = addr,
            .owner = owner,
        };
    }
    for (GpuHeapOwner* h = device->readback_heaps; h != nullptr; h = h->readback_next)
    {
        if (addr >= h->base_address && addr < h->base_address + h->aligned_size)
        {
            return {
                .buffer = h->buffer,
                .offset = addr - static_cast<uint32>(h->base_address),
                .owner = h,
            };
        }
    }
    return {};
#else
    const uint64 addr = reinterpret_cast<uintptr>(gpu_ptr);
    if (addr >= 0x1'0000'0000ull && addr < 0x2'0000'0000ull)
    {
        const uint32 mega_offset = static_cast<uint32>(addr - 0x1'0000'0000ull);
        GpuHeapOwner* owner = nullptr;
        for (uint32 i = 1; i < Device::max_heaps; ++i)
        {
            GpuHeapOwner* h = device->heap_table[i];
            if (h && h->buffer == device->mega_buffer &&
                mega_offset >= h->mega_offset && mega_offset < h->mega_offset + h->aligned_size)
            {
                owner = h;
                break;
            }
        }
        return {
            .buffer = device->mega_buffer,
            .offset = mega_offset,
            .owner = owner,
        };
    }
    const uint32 heap_id = static_cast<uint32>((addr >> 32) - 1);
    const uint32 offset = static_cast<uint32>(addr & 0xFFFFFFFFull);
    if (heap_id == 0 || heap_id >= Device::max_heaps || !device->heap_table[heap_id]) return {};
    assert(heap_id > 0 && heap_id < Device::max_heaps && "Invalid GPU address: heap ID out of range");
    GpuHeapOwner* heap = device->heap_table[heap_id];
    assert(heap != nullptr && "Invalid GPU address: heap does not exist or has been destroyed");
    assert(offset <= heap->byte_count && "Invalid GPU address: offset exceeds heap bounds");
    return {
        .buffer = heap->buffer,
        .offset = offset,
        .owner = heap,
    };
#endif
}

void set_error_callback(ErrorCallback callback) noexcept
{
    error_callback = callback;
}

const DeviceCaps& get_device_caps(const Device* device) noexcept
{
    assert(device);
    return device->caps;
}

static inline bool is_bc_format(Format format) noexcept
{
    switch (format)
    {
    case Format::bc3_srgb:
    case Format::bc3_unorm:
    case Format::bc5_rg:
    case Format::bc6h_ufloat:
    case Format::bc6h_sfloat:
    case Format::bc7_srgb:
    case Format::bc7_unorm:
        return true;
    default:
        return false;
    }
}

bool supports_texture_format(const Device* device, Format format, TextureUsage usage) noexcept
{
    if (to_wgpu_format(format) == WGPUTextureFormat_Undefined)
    {
        return false;
    }
    const TextureFormatInfo info = get_texture_format_info(format);
    if (info.depth && (static_cast<uint32>(usage) & static_cast<uint32>(TextureUsage::transfer_destination)) != 0)
    {
        return false;
    }
    if (is_bc_format(format) && (!device || !device->caps.texture_compression_bc))
    {
        return false;
    }
    return true;
}

uint32x2 get_drawable_extent(Device* device) noexcept
{
    if (!device || !device->native_window) return {};
#if defined(__EMSCRIPTEN__)
    const char* selector = static_cast<const char*>(device->native_window);
    double css_w = 0.0, css_h = 0.0;
    emscripten_get_element_css_size(selector, &css_w, &css_h);
    int width = 0, height = 0;
    if (css_w > 0.0 && css_h > 0.0)
    {
        double dpr = emscripten_get_device_pixel_ratio();
        if (dpr <= 0.0) dpr = 1.0;
        width = static_cast<int>(css_w * dpr + 0.5);
        height = static_cast<int>(css_h * dpr + 0.5);
    }
    if (width <= 0 || height <= 0)
    {
        emscripten_get_canvas_element_size(selector, &width, &height);
    }
    return {.x = static_cast<uint32>(width > 0 ? width : 1), .y = static_cast<uint32>(height > 0 ? height : 1)};
#elif defined(__APPLE__)
    struct CGSizeCompat { double width; double height; };
    const CGSizeCompat size = reinterpret_cast<CGSizeCompat (*)(id, SEL)>(objc_msgSend)(
        static_cast<id>(device->native_window), sel_registerName("drawableSize"));
    return {.x = static_cast<uint32>(size.width), .y = static_cast<uint32>(size.height)};
#else
    return {};
#endif
}

static void configure_surface_internal(Device* device, uint32 width, uint32 height) noexcept
{
    assert(device && device->swapchain.surface && device->device);
    if (width == 0 || height == 0) return;

#if defined(__EMSCRIPTEN__)
    if (device->native_window)
    {
        emscripten_set_canvas_element_size(
            static_cast<const char*>(device->native_window), static_cast<int>(width), static_cast<int>(height));
    }
#endif

    const WGPUSurfaceConfiguration config = {
        .nextInChain = nullptr,
        .device = device->device,
        .format = device->swapchain.format,
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = width,
        .height = height,
        .viewFormatCount = 0,
        .viewFormats = nullptr,
        .alphaMode = WGPUCompositeAlphaMode_Opaque,
        .presentMode = WGPUPresentMode_Fifo,
    };
    wgpuSurfaceConfigure(device->swapchain.surface, &config);
    device->swapchain.width = width;
    device->swapchain.height = height;
    device->swapchain.configured = true;
}

Error set_swapchain_format(Device* device, Format format, ColorSpace color_space) noexcept
{
    assert(device && !device->acquired);
    if (!device->swapchain.surface || color_space != ColorSpace::srgb) return Error::unsupported;

    if (format != Format::bgra8_unorm && format != Format::bgra8_srgb && format != Format::rgba8_unorm)
        return Error::unsupported;

    device->swapchain_format = format;
    device->swapchain.format = to_wgpu_format(format);

    if (device->swapchain.width > 0 && device->swapchain.height > 0)
    {
        configure_surface_internal(device, device->swapchain.width, device->swapchain.height);
    }
    return Error::none;
}

DeviceInit create_device(const DeviceDesc& desc) noexcept
{
    if (desc.desired_compute_queue_count > 0 || desc.desired_copy_queue_count > 0)
    {
        return {.device = nullptr, .error = Error::unsupported};
    }

    if (desc.swapchain_color_space != ColorSpace::srgb)
    {
        report_error("create_device", "unsupported swapchain color space");
        return {.device = nullptr, .error = Error::unsupported};
    }

    Device* device = new Device{};
    device->native_window = desc.window;
    device->swapchain_format = desc.swapchain_format != Format::undefined ? desc.swapchain_format : Format::bgra8_srgb;
    device->swapchain_color_space = desc.swapchain_color_space;
    device->timestamp_query_count = desc.timestamp_query_count;

#if !defined(__EMSCRIPTEN__)
    const char* const enabled_toggles[] = {
        "allow_unsafe_apis",
    };

    WGPUDawnTogglesDescriptor dawn_toggles = {
        .chain = { .next = nullptr, .sType = WGPUSType_DawnTogglesDescriptor },
        .enabledToggleCount = 1,
        .enabledToggles = enabled_toggles,
        .disabledToggleCount = 0,
        .disabledToggles = nullptr,
    };

    const WGPUInstanceFeatureName instance_features[] = {
        WGPUInstanceFeatureName_TimedWaitAny,
    };

    const WGPUInstanceDescriptor instance_desc = {
        .nextInChain = &dawn_toggles.chain,
        .requiredFeatureCount = 1,
        .requiredFeatures = instance_features,
        .requiredLimits = nullptr,
    };
#else
    const WGPUInstanceDescriptor instance_desc = {};
#endif

    device->instance = wgpuCreateInstance(&instance_desc);
    if (!device->instance)
    {
        report_error("create_device", "failed to create WGPUInstance");
        destroy_device(device);
        return {.device = nullptr, .error = Error::driver_error};
    }

    AdapterSync adapter_sync = {};

    const WGPURequestAdapterCallbackInfo adapter_cb_info = {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = on_request_adapter,
        .userdata1 = &adapter_sync,
        .userdata2 = nullptr,
    };

    const WGPURequestAdapterOptions adapter_opts = {
        .nextInChain = nullptr,
        .featureLevel = WGPUFeatureLevel_Undefined,
        .powerPreference = WGPUPowerPreference_HighPerformance,
        .forceFallbackAdapter = WGPU_FALSE,
#if defined(__APPLE__)
        .backendType = WGPUBackendType_Metal,
#else
        .backendType = WGPUBackendType_Undefined,
#endif
        .compatibleSurface = nullptr,
    };

    const WGPUFuture adapter_future = wgpuInstanceRequestAdapter(device->instance, &adapter_opts, adapter_cb_info);
    WGPUFutureWaitInfo wait_adapter = { .future = adapter_future, .completed = WGPU_FALSE };
    wgpuInstanceWaitAny(device->instance, 1, &wait_adapter, UINT64_MAX);
    device->adapter = adapter_sync.adapter;
    if (!device->adapter)
    {
        report_error("create_device", "no suitable WebGPU adapter found");
        destroy_device(device);
        return {.device = nullptr, .error = Error::unsupported};
    }

    WGPULimits supported_limits = {};
    wgpuAdapterGetLimits(device->adapter, &supported_limits);

    DeviceSync device_sync = {};

    const WGPURequestDeviceCallbackInfo device_cb_info = {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = on_request_device,
        .userdata1 = &device_sync,
        .userdata2 = nullptr,
    };

    WGPUFeatureName required_features[8] = {
        WGPUFeatureName_ChromiumExperimentalSamplingResourceTable,
    };
    uint32 required_feature_count = 1;
#if defined(NOGRAPHICSAPI_WEBGPU_PERSISTENT_MAP)
    required_features[required_feature_count++] = static_cast<WGPUFeatureName>(0x00050013); // WGPUFeatureName_BufferMapExtendedUsages
#endif
    const bool has_bc = wgpuAdapterHasFeature(adapter_sync.adapter, WGPUFeatureName_TextureCompressionBC);
    if (has_bc)
    {
        required_features[required_feature_count++] = WGPUFeatureName_TextureCompressionBC;
    }

    const WGPUDeviceDescriptor device_desc = {
#if !defined(__EMSCRIPTEN__)
        .nextInChain = &dawn_toggles.chain,
#else
        .nextInChain = nullptr,
#endif
        .label = { .data = "NoGraphicsAPI Device", .length = WGPU_STRLEN },
        .requiredFeatureCount = required_feature_count,
        .requiredFeatures = required_features,
        .requiredLimits = &supported_limits,
        .defaultQueue = {
            .nextInChain = nullptr,
            .label = { .data = "NoGraphicsAPI Queue", .length = WGPU_STRLEN },
        },
        .deviceLostCallbackInfo = {
            .nextInChain = nullptr,
            .mode = WGPUCallbackMode_AllowProcessEvents,
            .callback = on_device_lost,
            .userdata1 = device,
            .userdata2 = nullptr,
        },
        .uncapturedErrorCallbackInfo = {
            .nextInChain = nullptr,
            .callback = on_uncaptured_error,
            .userdata1 = device,
            .userdata2 = nullptr,
        },
    };

    const WGPUFuture device_future = wgpuAdapterRequestDevice(device->adapter, &device_desc, device_cb_info);
    WGPUFutureWaitInfo wait_dev = { .future = device_future, .completed = WGPU_FALSE };
    wgpuInstanceWaitAny(device->instance, 1, &wait_dev, UINT64_MAX);
    device->device = device_sync.device;
    if (!device->device)
    {
        report_error("create_device", "failed to create WGPUDevice");
        destroy_device(device);
        return {.device = nullptr, .error = Error::driver_error};
    }

    const WGPUQueue primary_queue = wgpuDeviceGetQueue(device->device);
    device->caps.general_queue_count = 1;
    device->caps.compute_queue_count = 0;
    device->caps.copy_queue_count = 0;
    device->caps.queue_count = 1;
    device->queues = new Queue[1]{};
    device->queues[0].device = device;
    device->queues[0].queue = primary_queue;
    device->queues[0].queue_index = 0;
    device->queues[0].kind = QueueKind::general;

    if (desc.window)
    {
#if defined(__EMSCRIPTEN__)
        const char* selector = static_cast<const char*>(desc.window);
        if (!selector || selector[0] == '\0') selector = "#canvas";
        WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas_source = {
            .chain = { .next = nullptr, .sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector },
            .selector = { .data = selector, .length = strlen(selector) },
        };
        const WGPUSurfaceDescriptor surface_desc = {
            .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&canvas_source),
            .label = { .data = "Canvas Surface", .length = WGPU_STRLEN },
        };
        device->swapchain.surface = wgpuInstanceCreateSurface(device->instance, &surface_desc);
#elif defined(__APPLE__)
        WGPUSurfaceSourceMetalLayer metal_source = {
            .chain = { .next = nullptr, .sType = WGPUSType_SurfaceSourceMetalLayer },
            .layer = desc.window,
        };
        const WGPUSurfaceDescriptor surface_desc = {
            .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&metal_source),
            .label = { .data = "Metal Surface", .length = WGPU_STRLEN },
        };
        device->swapchain.surface = wgpuInstanceCreateSurface(device->instance, &surface_desc);
#endif
        device->swapchain.format = to_wgpu_format(device->swapchain_format);
    }

    const WGPUResourceTableDescriptor table_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Default Resource Table", .length = WGPU_STRLEN },
        .size = 65536,
    };
    device->resource_table = wgpuDeviceCreateResourceTable(device->device, &table_desc);

    const WGPUBindGroupLayoutEntry gfx_bgl_entries[1] = {
        {
            .nextInChain = nullptr,
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .nextInChain = nullptr,
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .hasDynamicOffset = WGPU_FALSE,
                .minBindingSize = 0,
            },
        },
    };
    const WGPUBindGroupLayoutDescriptor gfx_bgl_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Graphics Bind Group Layout 0", .length = WGPU_STRLEN },
        .entryCount = 1,
        .entries = gfx_bgl_entries,
    };
    device->graphics_bind_group_layout = wgpuDeviceCreateBindGroupLayout(device->device, &gfx_bgl_desc);

    const WGPUBindGroupLayoutEntry comp_bgl_entries[1] = {
        {
            .nextInChain = nullptr,
            .binding = 0,
            .visibility = WGPUShaderStage_Compute,
            .buffer = {
                .nextInChain = nullptr,
                .type = WGPUBufferBindingType_Storage,
                .hasDynamicOffset = WGPU_FALSE,
                .minBindingSize = 0,
            },
        },
    };
    const WGPUBindGroupLayoutDescriptor comp_bgl_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Compute Bind Group Layout 0", .length = WGPU_STRLEN },
        .entryCount = 1,
        .entries = comp_bgl_entries,
    };
    device->compute_bind_group_layout = wgpuDeviceCreateBindGroupLayout(device->device, &comp_bgl_desc);

    WGPUPipelineLayoutResourceTable rt_chain = {
        .chain = { .next = nullptr, .sType = WGPUSType_PipelineLayoutResourceTable },
        .usesResourceTable = WGPU_TRUE,
    };
    const WGPUPipelineLayoutDescriptor gfx_layout_desc = {
        .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&rt_chain),
        .label = { .data = "Graphics Pipeline Layout", .length = WGPU_STRLEN },
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &device->graphics_bind_group_layout,
        .immediateSize = 4,
    };
    device->default_pipeline_layout = wgpuDeviceCreatePipelineLayout(device->device, &gfx_layout_desc);

    const WGPUPipelineLayoutDescriptor comp_layout_desc = {
        .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&rt_chain),
        .label = { .data = "Compute Pipeline Layout", .length = WGPU_STRLEN },
        .bindGroupLayoutCount = 1,
        .bindGroupLayouts = &device->compute_bind_group_layout,
        .immediateSize = 4,
    };
    device->compute_pipeline_layout = wgpuDeviceCreatePipelineLayout(device->device, &comp_layout_desc);

#if defined(__EMSCRIPTEN__)
    uint64 desired_mega_capacity = 128ull * 1024 * 1024;
#else
    uint64 desired_mega_capacity = 512ull * 1024 * 1024;
#endif
    if (supported_limits.maxStorageBufferBindingSize > 0 && desired_mega_capacity > supported_limits.maxStorageBufferBindingSize)
    {
        desired_mega_capacity = supported_limits.maxStorageBufferBindingSize & ~255ull;
    }
    if (supported_limits.maxBufferSize > 0 && desired_mega_capacity > supported_limits.maxBufferSize)
    {
        desired_mega_capacity = supported_limits.maxBufferSize & ~255ull;
    }
    device->mega_buffer_capacity = desired_mega_capacity;

    const WGPUBufferDescriptor mega_buf_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Global Virtual Arena Mega-Buffer", .length = WGPU_STRLEN },
#if defined(NOGRAPHICSAPI_WEBGPU_PERSISTENT_MAP)
        .usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform |
                 WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                 WGPUBufferUsage_Index | WGPUBufferUsage_Indirect,
        .size = device->mega_buffer_capacity,
        .mappedAtCreation = WGPU_TRUE,
#else
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform |
                 WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                 WGPUBufferUsage_Index | WGPUBufferUsage_Indirect,
        .size = device->mega_buffer_capacity,
        .mappedAtCreation = WGPU_FALSE,
#endif
    };
    device->mega_buffer = wgpuDeviceCreateBuffer(device->device, &mega_buf_desc);
    if (!device->mega_buffer)
    {
        report_error("create_device", "failed to create Global Virtual Arena Mega-Buffer");
        destroy_device(device);
        return {.device = nullptr, .error = Error::driver_error};
    }
    device->arena_blocks[0] = { .offset = 256, .size = device->mega_buffer_capacity - 256, .used = false };
    device->arena_block_count = 1;

    const WGPUBufferDescriptor ind_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Indirect Dispatch Buffer", .length = WGPU_STRLEN },
        .usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst,
        .size = Device::indirect_dispatch_buffer_capacity,
        .mappedAtCreation = WGPU_FALSE,
    };
    device->indirect_dispatch_buffer = wgpuDeviceCreateBuffer(device->device, &ind_desc);

    const WGPUBindGroupEntry mega_gfx_entries[1] = {
        {
            .nextInChain = nullptr,
            .binding = 0,
            .buffer = device->mega_buffer,
            .offset = 0,
            .size = device->mega_buffer_capacity,
            .sampler = nullptr,
            .textureView = nullptr,
        },
    };
    const WGPUBindGroupDescriptor mega_gfx_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Universal Mega-Buffer Graphics Bind Group", .length = WGPU_STRLEN },
        .layout = device->graphics_bind_group_layout,
        .entryCount = 1,
        .entries = mega_gfx_entries,
    };
    device->mega_bind_group = wgpuDeviceCreateBindGroup(device->device, &mega_gfx_desc);

    const WGPUBindGroupEntry mega_comp_entries[1] = {
        {
            .nextInChain = nullptr,
            .binding = 0,
            .buffer = device->mega_buffer,
            .offset = 0,
            .size = device->mega_buffer_capacity,
            .sampler = nullptr,
            .textureView = nullptr,
        },
    };
    const WGPUBindGroupDescriptor mega_comp_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Universal Mega-Buffer Compute Bind Group", .length = WGPU_STRLEN },
        .layout = device->compute_bind_group_layout,
        .entryCount = 1,
        .entries = mega_comp_entries,
    };
    device->mega_compute_bind_group = wgpuDeviceCreateBindGroup(device->device, &mega_comp_desc);

    snprintf(device->device_name, sizeof(device->device_name), "Dawn WebGPU (Apple Metal Backend)");
    device->caps.device_name = device->device_name;
    device->caps.texture_heap_alignment = 256;
    device->caps.max_push_data_size = 256;
    device->caps.timestamp_period_ns = 1.0f;
    device->caps.texture_compression_bc = has_bc;

#if !defined(__EMSCRIPTEN__)
    const char* max_frames_str = getenv("NOGRAPHICSAPI_MAX_FRAMES");
    if (max_frames_str)
    {
        device->max_frames = static_cast<uint64>(strtoull(max_frames_str, nullptr, 10));
    }
#endif

    return {.device = device, .error = Error::none};
}

void destroy_device(Device* device) noexcept
{
    if (!device) return;
    wait_idle(device);

    if (device->swapchain.current_view) wgpuTextureViewRelease(device->swapchain.current_view);
    if (device->swapchain.current_texture.texture) wgpuTextureRelease(device->swapchain.current_texture.texture);
    if (device->swapchain.surface) wgpuSurfaceRelease(device->swapchain.surface);

    for (uint32 i = 0; i < Device::resource_table_cache_size; ++i)
    {
        if (device->resource_table_cache[i].table)
        {
            wgpuResourceTableDestroy(device->resource_table_cache[i].table);
            wgpuResourceTableRelease(device->resource_table_cache[i].table);
            device->resource_table_cache[i] = {};
        }
    }
    if (device->resource_table)
    {
        wgpuResourceTableDestroy(device->resource_table);
        wgpuResourceTableRelease(device->resource_table);
    }
    if (device->default_pipeline_layout) wgpuPipelineLayoutRelease(device->default_pipeline_layout);
    if (device->compute_pipeline_layout) wgpuPipelineLayoutRelease(device->compute_pipeline_layout);
    if (device->mega_bind_group) wgpuBindGroupRelease(device->mega_bind_group);
    if (device->mega_compute_bind_group) wgpuBindGroupRelease(device->mega_compute_bind_group);
    if (device->mega_buffer)
    {
        wgpuBufferDestroy(device->mega_buffer);
        wgpuBufferRelease(device->mega_buffer);
    }
    if (device->indirect_dispatch_buffer)
    {
        wgpuBufferDestroy(device->indirect_dispatch_buffer);
        wgpuBufferRelease(device->indirect_dispatch_buffer);
        device->indirect_dispatch_buffer = nullptr;
    }
    if (device->graphics_bind_group_layout) wgpuBindGroupLayoutRelease(device->graphics_bind_group_layout);
    if (device->compute_bind_group_layout) wgpuBindGroupLayoutRelease(device->compute_bind_group_layout);

    if (device->queues)
    {
        if (device->queues[0].queue) wgpuQueueRelease(device->queues[0].queue);
        delete[] device->queues;
        device->queues = nullptr;
    }

    if (device->device)
    {
        wgpuDeviceDestroy(device->device);
        wgpuDeviceRelease(device->device);
    }
    if (device->adapter) wgpuAdapterRelease(device->adapter);
    if (device->instance) wgpuInstanceRelease(device->instance);

    delete device;
}

static void retire_completed(TimelineSemaphore* sem) noexcept
{
    if (!sem) return;
    const uint64 completed = __atomic_load_n(&sem->completed_value, __ATOMIC_ACQUIRE);
    while (sem->pending_head < sem->pending_tail)
    {
        const PendingPoint& p = sem->pending[sem->pending_head % TimelineSemaphore::max_pending];
        if (p.value <= completed)
        {
            sem->pending_head++;
        }
        else
        {
            break;
        }
    }
}

TimelineSemaphore* create_timeline_semaphore(Device* device, uint64 initial_value) noexcept
{
    assert(device);
    TimelineSemaphore* sem = new TimelineSemaphore{};
    sem->device = device;
    sem->completed_value = initial_value;
    return sem;
}

void destroy_timeline_semaphore(TimelineSemaphore* semaphore) noexcept
{
    if (!semaphore) return;
    if (semaphore->device && semaphore->device->instance)
    {
        Device* device = semaphore->device;
#if !defined(__EMSCRIPTEN__)
        device->submit_lock.lock();
        wgpuInstanceProcessEvents(device->instance);
        retire_completed(semaphore);
        const bool has_pending = semaphore->pending_tail > semaphore->pending_head;
        const WGPUFuture newest_future = has_pending
            ? semaphore->pending[(semaphore->pending_tail - 1) % TimelineSemaphore::max_pending].future
            : WGPUFuture{};
        device->submit_lock.unlock();
        if (has_pending && newest_future.id != 0)
        {
            WGPUFutureWaitInfo wait_info = { .future = newest_future, .completed = WGPU_FALSE };
            wgpuInstanceWaitAny(device->instance, 1, &wait_info, UINT64_MAX);
        }
#endif
        wgpuInstanceProcessEvents(device->instance);
    }
    delete semaphore;
}

uint64 timeline_completed_value(const TimelineSemaphore* semaphore) noexcept
{
    if (!semaphore || !semaphore->device) return 0;
    TimelineSemaphore* sem = const_cast<TimelineSemaphore*>(semaphore);
    sem->device->submit_lock.lock();
    wgpuInstanceProcessEvents(sem->device->instance);
    retire_completed(sem);
    const uint64 val = __atomic_load_n(&sem->completed_value, __ATOMIC_RELAXED);
    sem->device->submit_lock.unlock();
    return val;
}

void wait_timeline(TimelinePoint point) noexcept
{
    TimelineSemaphore* sem = point.semaphore;
    if (!sem || !sem->device) return;
    Device* device = sem->device;

    if (__atomic_load_n(&sem->completed_value, __ATOMIC_ACQUIRE) >= point.value)
    {
        sync_readback_heaps(device, sem, point.value);
        return;
    }

#if !defined(__EMSCRIPTEN__)
    uint32 spin_count = 0;
    while (__atomic_load_n(&sem->completed_value, __ATOMIC_ACQUIRE) < point.value)
    {
        WGPUFuture future_to_wait = {};
        bool has_future = false;

        device->submit_lock.lock();
        wgpuInstanceProcessEvents(device->instance);
        retire_completed(sem);

        if (__atomic_load_n(&sem->completed_value, __ATOMIC_RELAXED) >= point.value)
        {
            device->submit_lock.unlock();
            break;
        }

        for (uint32 i = sem->pending_head; i < sem->pending_tail; ++i)
        {
            const uint32 slot = i % TimelineSemaphore::max_pending;
            if (sem->pending[slot].value >= point.value)
            {
                future_to_wait = sem->pending[slot].future;
                has_future = true;
                break;
            }
        }

        device->submit_lock.unlock();

        if (has_future)
        {
            WGPUFutureWaitInfo wait_info = {
                .future = future_to_wait,
                .completed = WGPU_FALSE,
            };
            wgpuInstanceWaitAny(device->instance, 1, &wait_info, UINT64_MAX);
            wgpuInstanceProcessEvents(device->instance);

            device->submit_lock.lock();
            retire_completed(sem);
            device->submit_lock.unlock();
        }
        else
        {
            if (++spin_count < 64)
            {
#if defined(__x86_64__) || defined(_M_X64)
                __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
                asm volatile("yield");
#endif
            }
            else
            {
#if defined(_WIN32)
                SwitchToThread();
#elif !defined(__EMSCRIPTEN__)
                sched_yield();
#endif
                spin_count = 0;
            }
        }
    }

    sync_readback_heaps(device, sem, point.value);
#else
    device->submit_lock.lock();
    wgpuInstanceProcessEvents(device->instance);
    advance_timeline_value(&sem->completed_value, point.value);
    retire_completed(sem);
    device->submit_lock.unlock();
    sync_readback_heaps(device, sem, point.value);
#endif
}

void wait_idle(Device* device) noexcept
{
    if (!device || !device->queues || !device->queues[0].queue || !device->instance) return;

#if !defined(__EMSCRIPTEN__)
    IdleSync idle_sync = {};

    const WGPUQueueWorkDoneCallbackInfo cb_info = {
        .nextInChain = nullptr,
        .mode = WGPUCallbackMode_WaitAnyOnly,
        .callback = on_idle_work_done,
        .userdata1 = &idle_sync,
        .userdata2 = nullptr,
    };

    const WGPUFuture idle_future = wgpuQueueOnSubmittedWorkDone(device->queues[0].queue, cb_info);
    WGPUFutureWaitInfo wait_info = { .future = idle_future, .completed = WGPU_FALSE };
    wgpuInstanceWaitAny(device->instance, 1, &wait_info, UINT64_MAX);
    wgpuInstanceProcessEvents(device->instance);
    sync_readback_heaps(device, nullptr, 0);
#else
    wgpuInstanceProcessEvents(device->instance);
    sync_readback_heaps(device, nullptr, 0);
#endif
}

CommandPool* create_command_pool(Device* device, uint32 queue_index) noexcept
{
    assert(device);
    CommandPool* pool = new CommandPool{};
    pool->device = device;
    pool->queue_index = queue_index;
    return pool;
}

void destroy_command_pool(CommandPool* pool) noexcept
{
    if (!pool) return;
    if (pool->scratch_buffer)
    {
        wgpuBufferDestroy(pool->scratch_buffer);
        wgpuBufferRelease(pool->scratch_buffer);
        pool->scratch_buffer = nullptr;
    }
    CommandBuffer* current = pool->first;
    while (current)
    {
        CommandBuffer* next = current->next;
        if (current->timestamp_records) free(current->timestamp_records);
        if (current->readback_ranges) free(current->readback_ranges);
        if (current->recorded) wgpuCommandBufferRelease(current->recorded);
        if (current->encoder) wgpuCommandEncoderRelease(current->encoder);
        delete current;
        current = next;
    }
    delete pool;
}

void reset_command_pool(CommandPool* pool) noexcept
{
    assert(pool);
    for (CommandBuffer* cb = pool->first; cb; cb = cb->next)
    {
        cb->referenced_heap_count = 0;
        cb->timestamp_count = 0;
        cb->readback_range_count = 0;
        if (cb->recorded)
        {
            wgpuCommandBufferRelease(cb->recorded);
            cb->recorded = nullptr;
        }
        if (cb->encoder)
        {
            wgpuCommandEncoderRelease(cb->encoder);
            cb->encoder = nullptr;
        }
        cb->render_pass = nullptr;
        cb->compute_pass = nullptr;
        cb->current_pso = nullptr;
        cb->active_texture_heap = nullptr;
        cb->active_sampler_heap = nullptr;
        cb->recording = false;
        cb->ended = false;
        cb->submitted = false;
        cb->acquired = false;
    }
    pool->next_buffer = pool->first;
}

CommandBuffer* begin_commands(CommandPool* pool) noexcept
{
    assert(pool);
    CommandBuffer* commands = nullptr;
    if (pool->next_buffer)
    {
        commands = pool->next_buffer;
        pool->next_buffer = pool->next_buffer->next;
    }
    else
    {
        commands = new CommandBuffer{};
        commands->device = pool->device;
        commands->pool = pool;
        if (!pool->first) pool->first = commands;
        if (pool->last) pool->last->next = commands;
        pool->last = commands;
    }

    commands->current_pso = nullptr;
    commands->active_texture_heap = nullptr;
    commands->active_sampler_heap = nullptr;
    commands->recording = true;
    commands->ended = false;
    commands->submitted = false;
    commands->acquired = false;
    commands->timestamp_count = 0;

    const WGPUCommandEncoderDescriptor enc_desc = {
        .nextInChain = nullptr,
        .label = { .data = "CommandBuffer Encoder", .length = WGPU_STRLEN },
    };
    commands->encoder = wgpuDeviceCreateCommandEncoder(pool->device->device, &enc_desc);
    return commands;
}

void end_commands(CommandBuffer* commands) noexcept
{
    assert(commands && commands->recording && !commands->ended);
    if (commands->render_pass)
    {
        wgpuRenderPassEncoderEnd(commands->render_pass);
        wgpuRenderPassEncoderRelease(commands->render_pass);
        commands->render_pass = nullptr;
    }
    if (commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }
    const WGPUCommandBufferDescriptor buf_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Recorded Commands", .length = WGPU_STRLEN },
    };
    commands->recorded = wgpuCommandEncoderFinish(commands->encoder, &buf_desc);
    wgpuCommandEncoderRelease(commands->encoder);
    commands->encoder = nullptr;
    commands->recording = false;
    commands->ended = true;
}

SwapchainFrame acquire(CommandBuffer* commands) noexcept
{
    assert(commands && commands->recording && !commands->render_pass);
    Device* device = commands->device;
    assert(device && device->swapchain.surface && !device->acquired);

    const uint32x2 extent = get_drawable_extent(device);
    if (!extent.x || !extent.y) return {};

    if (!device->swapchain.configured || device->swapchain.width != extent.x || device->swapchain.height != extent.y)
    {
        configure_surface_internal(device, extent.x, extent.y);
    }

    wgpuSurfaceGetCurrentTexture(device->swapchain.surface, &device->swapchain.current_texture);
    if (device->swapchain.current_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated)
    {
        configure_surface_internal(device, extent.x, extent.y);
        wgpuSurfaceGetCurrentTexture(device->swapchain.surface, &device->swapchain.current_texture);
    }

    if (device->swapchain.current_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        device->swapchain.current_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
    {
        if (device->swapchain.current_texture.texture)
        {
            wgpuTextureRelease(device->swapchain.current_texture.texture);
            device->swapchain.current_texture.texture = nullptr;
        }
        return {};
    }

    const WGPUTextureViewDescriptor view_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Swapchain View", .length = WGPU_STRLEN },
        .format = device->swapchain.format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All,
        .usage = WGPUTextureUsage_RenderAttachment,
    };
    device->swapchain.current_view = wgpuTextureCreateView(device->swapchain.current_texture.texture, &view_desc);
    device->swapchain.render_view.view = device->swapchain.current_view;
    device->swapchain.render_view.texture = device->swapchain.current_texture.texture;
    device->swapchain.render_view.format = device->swapchain_format;

    device->acquired = commands;
    commands->acquired = true;
    device->swapchain.acquired = true;

    return {.render_view = &device->swapchain.render_view, .extent = extent};
}

static inline uint64 get_monotonic_timestamp_ns() noexcept
{
    static uint64 last_ts = 0;
#if defined(__APPLE__)
    uint64 ts = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#elif defined(_WIN32)
    LARGE_INTEGER count, freq;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&freq);
    uint64 ts = static_cast<uint64>((static_cast<double>(count.QuadPart) / freq.QuadPart) * 1e9);
#else
    struct timespec ts_spec;
    clock_gettime(CLOCK_MONOTONIC, &ts_spec);
    uint64 ts = static_cast<uint64>(ts_spec.tv_sec) * 1000000000ull + static_cast<uint64>(ts_spec.tv_nsec);
#endif
    if (ts <= last_ts) ts = last_ts + 1;
    last_ts = ts;
    return ts;
}

#if !defined(NOGRAPHICSAPI_WEBGPU_PERSISTENT_MAP)
static void sync_mega_cpu_heap(WGPUQueue queue, WGPUBuffer mega_buffer, GpuHeapOwner* heap) noexcept
{
    if (!heap || !heap->cpu_shadow || !heap->synced_snapshot || heap->byte_count == 0) return;
    if (heap->needs_initial_sync)
    {
        wgpuQueueWriteBuffer(queue, mega_buffer, heap->mega_offset, heap->cpu_shadow, static_cast<size_t>(heap->aligned_size));
        memcpy(heap->synced_snapshot, heap->cpu_shadow, static_cast<size_t>(heap->aligned_size));
        heap->needs_initial_sync = false;
        return;
    }
    constexpr uint64 sync_chunk_size = 256;
    const uint64 total_size = heap->byte_count;
    const byte* cpu_ptr = heap->cpu_shadow;
    byte* sync_ptr = heap->synced_snapshot;
    uint64 offset = 0;
    while (offset < total_size)
    {
        while (offset < total_size)
        {
            if (total_size - offset >= 4096 && memcmp(cpu_ptr + offset, sync_ptr + offset, 4096) == 0)
            {
                offset += 4096;
                continue;
            }
            const uint64 chunk = (total_size - offset < sync_chunk_size) ? (total_size - offset) : sync_chunk_size;
            if (memcmp(cpu_ptr + offset, sync_ptr + offset, static_cast<size_t>(chunk)) != 0) break;
            offset += chunk;
        }
        if (offset >= total_size) break;
        const uint64 dirty_start = offset;
        uint64 dirty_end = offset;
        while (offset < total_size)
        {
            const uint64 chunk = (total_size - offset < sync_chunk_size) ? (total_size - offset) : sync_chunk_size;
            if (memcmp(cpu_ptr + offset, sync_ptr + offset, static_cast<size_t>(chunk)) == 0) break;
            offset += chunk;
            dirty_end = offset;
        }
        const uint64 up_off = dirty_start & ~3ull;
        uint64 up_end = (dirty_end + 3ull) & ~3ull;
        if (up_end > heap->aligned_size) up_end = heap->aligned_size;
        const uint64 up_size = up_end - up_off;
        if (up_size > 0)
        {
            wgpuQueueWriteBuffer(queue, mega_buffer, heap->mega_offset + up_off, cpu_ptr + up_off, static_cast<size_t>(up_size));
            memcpy(sync_ptr + up_off, cpu_ptr + up_off, static_cast<size_t>(up_size));
        }
    }
}
#endif

static void execute_submit_internal(Device* device, Span<CommandBuffer* const> commands, const TimelinePoint& completion, uint32 queue_index) noexcept
{
    Queue& cur_queue = device->queues[queue_index];
#if !defined(NOGRAPHICSAPI_WEBGPU_PERSISTENT_MAP)
    for (GpuHeapOwner* heap = device->cpu_visible_heaps; heap != nullptr; heap = heap->next)
    {
        sync_mega_cpu_heap(cur_queue.queue, device->mega_buffer, heap);
    }
#endif

    uint32 total_referenced = 0;
    for (size_t i = 0; i < commands.size; ++i)
    {
        total_referenced += commands.data[i]->referenced_heap_count;
    }

    for (size_t i = 0; i < commands.size; ++i)
    {
        CommandBuffer* cb = commands.data[i];
        for (uint32 r = 0; r < cb->readback_range_count; ++r)
        {
            const ReadbackRange& range = cb->readback_ranges[r];
            GpuHeapOwner* heap = range.heap;
            if (!heap || heap->memory != MemoryType::readback) continue;
            heap->last_writer_semaphore = completion.semaphore;
            heap->last_writer_value = completion.value;
            heap->pending_readback_sync = true;
            if (heap->pending_range_count > 0)
            {
                ReadbackRange& last = heap->pending_ranges[heap->pending_range_count - 1];
                if (last.offset + last.size == range.offset)
                {
                    last.size += range.size;
                    continue;
                }
            }
            if (heap->pending_range_count == heap->pending_range_capacity)
            {
                const uint32 new_cap = heap->pending_range_capacity == 0 ? 32 : heap->pending_range_capacity * 2;
                ReadbackRange* new_ranges = static_cast<ReadbackRange*>(malloc(new_cap * sizeof(ReadbackRange)));
                if (heap->pending_ranges)
                {
                    memcpy(new_ranges, heap->pending_ranges, heap->pending_range_count * sizeof(ReadbackRange));
                    free(heap->pending_ranges);
                }
                heap->pending_ranges = new_ranges;
                heap->pending_range_capacity = new_cap;
            }
            heap->pending_ranges[heap->pending_range_count++] = range;
        }
    }

    if (total_referenced > 0)
    {
        for (size_t i = 0; i < commands.size; ++i)
        {
            CommandBuffer* cb = commands.data[i];
            for (uint32 h = 0; h < cb->referenced_heap_count; ++h)
            {
                GpuHeapOwner* heap = cb->referenced_heaps[h];
                if (heap && heap->memory == MemoryType::readback)
                {
                    heap->last_writer_semaphore = completion.semaphore;
                    heap->last_writer_value = completion.value;
                    heap->pending_readback_sync = true;
                }
            }
        }
    }
    else
    {
        for (GpuHeapOwner* heap = device->readback_heaps; heap != nullptr; heap = heap->readback_next)
        {
            heap->last_writer_semaphore = completion.semaphore;
            heap->last_writer_value = completion.value;
            heap->pending_readback_sync = true;
        }
    }

    WGPUCommandBuffer native_stack[64] = {};
    WGPUCommandBuffer* native_buffers = commands.size <= 64 ? native_stack :
        static_cast<WGPUCommandBuffer*>(malloc(commands.size * sizeof(WGPUCommandBuffer)));
    for (size_t i = 0; i < commands.size; ++i)
    {
        assert(commands.data[i]->ended && commands.data[i]->recorded);
        native_buffers[i] = commands.data[i]->recorded;
        CommandBuffer* cb = commands.data[i];
        cb->submitted = true;
        for (uint32 j = 0; j < cb->timestamp_count; ++j)
        {
            cb->timestamp_records[j].recorded_time = get_monotonic_timestamp_ns();
        }
    }

    wgpuQueueSubmit(cur_queue.queue, commands.size, native_buffers);
    if (native_buffers != native_stack)
    {
        free(native_buffers);
    }

    if (completion.semaphore)
    {
        TimelineSemaphore* sem = completion.semaphore;
        sem->last_submitted_value = completion.value;
        while (sem->pending_tail - sem->pending_head >= TimelineSemaphore::max_pending)
        {
            const uint32 oldest_slot = sem->pending_head % TimelineSemaphore::max_pending;
            wait_timeline({.semaphore = sem, .value = sem->pending[oldest_slot].value});
            retire_completed(sem);
        }
        assert(sem->pending_tail - sem->pending_head < TimelineSemaphore::max_pending);
        const uint32 slot_index = sem->pending_tail++ % TimelineSemaphore::max_pending;
        PendingPoint& point = sem->pending[slot_index];
        point.value = completion.value;

        const WGPUQueueWorkDoneCallbackInfo cb_info = {
            .nextInChain = nullptr,
            .mode = WGPUCallbackMode_AllowProcessEvents,
            .callback = on_timeline_work_done,
            .userdata1 = sem,
            .userdata2 = reinterpret_cast<void*>(static_cast<uintptr>(completion.value)),
        };
        point.future = wgpuQueueOnSubmittedWorkDone(cur_queue.queue, cb_info);
    }
}

void submit(Device* device, const SubmitDesc& desc, uint32 queue_index) noexcept
{
    assert(device && queue_index < device->caps.queue_count);

    device->submit_lock.lock();

    bool must_defer = false;
    for (size_t w = 0; w < desc.waits.size; ++w)
    {
        const TimelinePoint& wait_pt = desc.waits.data[w];
        if (wait_pt.semaphore && wait_pt.semaphore->last_submitted_value < wait_pt.value)
        {
            must_defer = true;
            break;
        }
    }

    if (must_defer)
    {
        assert(device->deferred_submit_count < Device::max_deferred_submits);
        DeferredSubmit& def = device->deferred_submits[device->deferred_submit_count++];
        def.command_count = static_cast<uint32>(desc.commands.size);
        for (size_t i = 0; i < desc.commands.size && i < 64; ++i)
        {
            def.commands_storage[i] = desc.commands.data[i];
        }
        def.wait_count = static_cast<uint32>(desc.waits.size);
        for (size_t i = 0; i < desc.waits.size && i < 8; ++i)
        {
            def.waits_storage[i] = desc.waits.data[i];
        }
        def.completion = desc.completion;
        def.queue_index = queue_index;
        def.active = true;

        device->submit_lock.unlock();
        return;
    }

    execute_submit_internal(device, desc.commands, desc.completion, queue_index);

    bool progressed = true;
    while (progressed)
    {
        progressed = false;
        for (uint32 d = 0; d < device->deferred_submit_count; ++d)
        {
            DeferredSubmit& def = device->deferred_submits[d];
            if (!def.active) continue;

            bool ready = true;
            for (uint32 w = 0; w < def.wait_count; ++w)
            {
                const TimelinePoint& wait_pt = def.waits_storage[w];
                if (wait_pt.semaphore && wait_pt.semaphore->last_submitted_value < wait_pt.value)
                {
                    ready = false;
                    break;
                }
            }
            if (ready)
            {
                def.active = false;
                Span<CommandBuffer* const> cmd_span = {def.commands_storage, def.command_count};
                execute_submit_internal(device, cmd_span, def.completion, def.queue_index);
                progressed = true;
            }
        }
    }

    while (device->deferred_submit_count > 0 && !device->deferred_submits[device->deferred_submit_count - 1].active)
    {
        device->deferred_submit_count--;
    }

    device->submit_lock.unlock();
}

void submit_and_present(Device* device, const SubmitDesc& desc) noexcept
{
    assert(device && device->swapchain.acquired && device->acquired);
    bool found = false;
    for (size_t i = 0; i < desc.commands.size; ++i)
    {
        if (desc.commands.data[i] == device->acquired)
        {
            found = true;
            break;
        }
    }
    assert(found && "Acquired command buffer must be included in submit_and_present");

    submit(device, desc, 0);

#if !defined(__EMSCRIPTEN__)
    wgpuSurfacePresent(device->swapchain.surface);
#endif

    if (device->swapchain.current_view)
    {
        wgpuTextureViewRelease(device->swapchain.current_view);
        device->swapchain.current_view = nullptr;
    }
    if (device->swapchain.current_texture.texture)
    {
        wgpuTextureRelease(device->swapchain.current_texture.texture);
        device->swapchain.current_texture.texture = nullptr;
    }

    device->swapchain.acquired = false;
    device->acquired->acquired = false;
    device->acquired = nullptr;
    device->swapchain.render_view = {};

#if !defined(__EMSCRIPTEN__)
    wgpuInstanceProcessEvents(device->instance);
    if (device->max_frames > 0 && ++device->presented_frames >= device->max_frames)
    {
        wait_idle(device);
        exit(0);
    }
#endif
}

void begin_render_pass(CommandBuffer* commands, const RenderingDesc& desc, RenderingFlags flags) noexcept
{
    assert(commands && commands->recording && !commands->render_pass);
    commands->current_pso = nullptr;
    if (commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }

    const bool resuming = (static_cast<uint32>(flags) & static_cast<uint32>(RenderingFlags::resuming)) != 0;
    const bool suspending = (static_cast<uint32>(flags) & static_cast<uint32>(RenderingFlags::suspending)) != 0;

    WGPURenderPassColorAttachment color_attachments[8] = {};
    const size_t color_count = desc.colors.size < 8 ? desc.colors.size : 8;
    for (size_t i = 0; i < color_count; ++i)
    {
        const ColorAttachment& src = desc.colors.data[i];
        assert(src.render_view && src.render_view->view);
        color_attachments[i] = {
            .nextInChain = nullptr,
            .view = src.render_view->view,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .resolveTarget = nullptr,
            .loadOp = resuming ? WGPULoadOp_Load : ((src.load == LoadOp::load) ? WGPULoadOp_Load : WGPULoadOp_Clear),
            .storeOp = (suspending || src.store == StoreOp::store) ? WGPUStoreOp_Store : WGPUStoreOp_Discard,
            .clearValue = {
                .r = static_cast<double>(src.clear.x),
                .g = static_cast<double>(src.clear.y),
                .b = static_cast<double>(src.clear.z),
                .a = static_cast<double>(src.clear.w),
            },
        };
    }

    WGPURenderPassDepthStencilAttachment ds_attachment = {};
    bool has_ds = false;
    if (desc.depth.render_view && desc.depth.render_view->view)
    {
        has_ds = true;
        ds_attachment.view = desc.depth.render_view->view;
        ds_attachment.depthLoadOp = resuming ? WGPULoadOp_Load : ((desc.depth.load == LoadOp::load) ? WGPULoadOp_Load : WGPULoadOp_Clear);
        ds_attachment.depthStoreOp = (suspending || desc.depth.store == StoreOp::store) ? WGPUStoreOp_Store : WGPUStoreOp_Discard;
        ds_attachment.depthClearValue = desc.depth.clear;
        ds_attachment.depthReadOnly = WGPU_FALSE;
        ds_attachment.stencilLoadOp = WGPULoadOp_Undefined;
        ds_attachment.stencilStoreOp = WGPUStoreOp_Undefined;
        ds_attachment.stencilReadOnly = WGPU_TRUE;
    }

    const WGPURenderPassDescriptor pass_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Render Pass", .length = WGPU_STRLEN },
        .colorAttachmentCount = color_count,
        .colorAttachments = color_attachments,
        .depthStencilAttachment = has_ds ? &ds_attachment : nullptr,
        .occlusionQuerySet = nullptr,
        .timestampWrites = nullptr,
    };

    commands->render_pass = wgpuCommandEncoderBeginRenderPass(commands->encoder, &pass_desc);
    if (commands->render_pass)
    {
        const WGPUResourceTable active_table = resolve_resource_table(
            commands->device, commands->active_texture_heap, commands->active_sampler_heap);
        if (active_table)
        {
            wgpuRenderPassEncoderSetResourceTable(commands->render_pass, active_table);
        }
        if (commands->device->mega_bind_group)
        {
            wgpuRenderPassEncoderSetBindGroup(commands->render_pass, 0, commands->device->mega_bind_group, 0, nullptr);
        }
    }
}

void end_render_pass(CommandBuffer* commands) noexcept
{
    assert(commands && commands->render_pass);
    wgpuRenderPassEncoderEnd(commands->render_pass);
    wgpuRenderPassEncoderRelease(commands->render_pass);
    commands->render_pass = nullptr;
    commands->current_pso = nullptr;
}

void set_viewport(CommandBuffer* commands, const Viewport& viewport) noexcept
{
    assert(commands);
    if (commands->render_pass)
    {
        wgpuRenderPassEncoderSetViewport(commands->render_pass, viewport.x, viewport.y, viewport.width, viewport.height,
                                         viewport.min_depth, viewport.max_depth);
    }
}

void set_scissor(CommandBuffer* commands, const Scissor& scissor) noexcept
{
    assert(commands);
    if (commands->render_pass)
    {
        const uint32 x = scissor.x < 0 ? 0 : static_cast<uint32>(scissor.x);
        const uint32 y = scissor.y < 0 ? 0 : static_cast<uint32>(scissor.y);
        wgpuRenderPassEncoderSetScissorRect(commands->render_pass, x, y, scissor.width, scissor.height);
    }
}

void set_depth_stencil(CommandBuffer* commands, const DepthStencilState& state) noexcept
{
    assert(commands);
    if (commands->render_pass)
    {
        wgpuRenderPassEncoderSetStencilReference(commands->render_pass, state.front.reference);
    }
}

PSO* create_graphics_pso(Device* device, const GraphicsPSODesc& desc) noexcept
{
    assert(device && desc.vertex.code.size);

    WGPUShaderSourceWGSL vs_source = {
        .chain = { .next = nullptr, .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = reinterpret_cast<const char*>(desc.vertex.code.data), .length = desc.vertex.code.size },
    };
    const WGPUShaderModuleDescriptor vs_desc = {
        .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&vs_source),
        .label = { .data = "Vertex Shader", .length = WGPU_STRLEN },
    };
    const WGPUShaderModule vs_module = wgpuDeviceCreateShaderModule(device->device, &vs_desc);
    if (!vs_module)
    {
        report_error("create_graphics_pso", "failed to create vertex shader module");
        return nullptr;
    }

    WGPUShaderModule fs_module = nullptr;
    WGPUShaderSourceWGSL fs_source = {};
    if (desc.fragment.code.size)
    {
        fs_source = {
            .chain = { .next = nullptr, .sType = WGPUSType_ShaderSourceWGSL },
            .code = { .data = reinterpret_cast<const char*>(desc.fragment.code.data), .length = desc.fragment.code.size },
        };
        const WGPUShaderModuleDescriptor fs_desc = {
            .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&fs_source),
            .label = { .data = "Fragment Shader", .length = WGPU_STRLEN },
        };
        fs_module = wgpuDeviceCreateShaderModule(device->device, &fs_desc);
        if (!fs_module)
        {
            wgpuShaderModuleRelease(vs_module);
            report_error("create_graphics_pso", "failed to create fragment shader module");
            return nullptr;
        }
    }

    WGPUColorTargetState color_targets[8] = {};
    WGPUBlendState blend_states[8] = {};
    const size_t target_count = desc.color_targets.size < 8 ? desc.color_targets.size : 8;
    for (size_t i = 0; i < target_count; ++i)
    {
        const ColorTargetDesc& target = desc.color_targets.data[i];
        blend_states[i] = {
            .color = {
                .operation = to_wgpu_blend_op(target.blend.color.operation),
                .srcFactor = to_wgpu_blend_factor(target.blend.color.source),
                .dstFactor = to_wgpu_blend_factor(target.blend.color.destination),
            },
            .alpha = {
                .operation = to_wgpu_blend_op(target.blend.alpha.operation),
                .srcFactor = to_wgpu_blend_factor(target.blend.alpha.source),
                .dstFactor = to_wgpu_blend_factor(target.blend.alpha.destination),
            },
        };
        color_targets[i] = {
            .nextInChain = nullptr,
            .format = to_wgpu_format(target.format),
            .blend = target.blend.enabled ? &blend_states[i] : nullptr,
            .writeMask = target.write_mask,
        };
    }

    const WGPUFragmentState fragment_state = {
        .nextInChain = nullptr,
        .module = fs_module,
        .entryPoint = { .data = desc.fragment.entry_point, .length = WGPU_STRLEN },
        .constantCount = 0,
        .constants = nullptr,
        .targetCount = target_count,
        .targets = color_targets,
    };

    WGPUDepthStencilState ds_state = {};
    bool has_depth = false;
    if (desc.depth_format != Format::undefined)
    {
        has_depth = true;
        ds_state.nextInChain = nullptr;
        ds_state.format = to_wgpu_format(desc.depth_format);
        ds_state.depthWriteEnabled = WGPUOptionalBool_True;
        ds_state.depthCompare = WGPUCompareFunction_LessEqual;
        ds_state.stencilFront = { .compare = WGPUCompareFunction_Always, .failOp = WGPUStencilOperation_Keep,
                                  .depthFailOp = WGPUStencilOperation_Keep, .passOp = WGPUStencilOperation_Keep };
        ds_state.stencilBack = { .compare = WGPUCompareFunction_Always, .failOp = WGPUStencilOperation_Keep,
                                 .depthFailOp = WGPUStencilOperation_Keep, .passOp = WGPUStencilOperation_Keep };
        ds_state.stencilReadMask = 0xFF;
        ds_state.stencilWriteMask = 0xFF;
        ds_state.depthBias = static_cast<int32>(desc.rasterization.depth_bias_constant);
        ds_state.depthBiasSlopeScale = desc.rasterization.depth_bias_slope;
        ds_state.depthBiasClamp = desc.rasterization.depth_bias_clamp;
    }

    const WGPURenderPipelineDescriptor pipe_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Graphics PSO", .length = WGPU_STRLEN },
        .layout = device->default_pipeline_layout,
        .vertex = {
            .nextInChain = nullptr,
            .module = vs_module,
            .entryPoint = { .data = desc.vertex.entry_point, .length = WGPU_STRLEN },
            .constantCount = 0,
            .constants = nullptr,
            .bufferCount = 0,
            .buffers = nullptr,
        },
        .primitive = {
            .nextInChain = nullptr,
            .topology = WGPUPrimitiveTopology_TriangleList,
            .stripIndexFormat = WGPUIndexFormat_Undefined,
            .frontFace = WGPUFrontFace_CCW,
            .cullMode = to_wgpu_cull_mode(desc.rasterization.cull),
            .unclippedDepth = WGPU_FALSE,
        },
        .depthStencil = has_depth ? &ds_state : nullptr,
        .multisample = {
            .nextInChain = nullptr,
            .count = 1,
            .mask = 0xFFFFFFFF,
            .alphaToCoverageEnabled = WGPU_FALSE,
        },
        .fragment = fs_module ? &fragment_state : nullptr,
    };

    const WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device->device, &pipe_desc);
    wgpuShaderModuleRelease(vs_module);
    if (fs_module) wgpuShaderModuleRelease(fs_module);

    if (!pipeline)
    {
        report_error("create_graphics_pso", "wgpuDeviceCreateRenderPipeline failed");
        return nullptr;
    }

    PSO* pso = new PSO{};
    pso->device = device;
    pso->render = pipeline;
    pso->rasterization = desc.rasterization;
    pso->is_compute = false;
    return pso;
}

PSO* create_mesh_pso(Device* device, const MeshPSODesc& desc) noexcept
{
    assert(device && desc.mesh.code.size);
    const GraphicsPSODesc gfx_desc = {
        .vertex = desc.mesh,
        .fragment = desc.fragment,
        .color_targets = desc.color_targets,
        .depth_format = desc.depth_format,
        .stencil_format = desc.stencil_format,
        .rasterization = desc.rasterization,
    };
    return create_graphics_pso(device, gfx_desc);
}

PSO* create_compute_pso(Device* device, const ShaderStage& compute) noexcept
{
    assert(device && compute.code.size);

    WGPUShaderSourceWGSL cs_source = {
        .chain = { .next = nullptr, .sType = WGPUSType_ShaderSourceWGSL },
        .code = { .data = reinterpret_cast<const char*>(compute.code.data), .length = compute.code.size },
    };
    const WGPUShaderModuleDescriptor cs_desc = {
        .nextInChain = reinterpret_cast<WGPUChainedStruct*>(&cs_source),
        .label = { .data = "Compute Shader", .length = WGPU_STRLEN },
    };
    const WGPUShaderModule cs_module = wgpuDeviceCreateShaderModule(device->device, &cs_desc);
    if (!cs_module) return nullptr;

    const WGPUComputePipelineDescriptor pipe_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Compute PSO", .length = WGPU_STRLEN },
        .layout = device->compute_pipeline_layout ? device->compute_pipeline_layout : device->default_pipeline_layout,
        .compute = {
            .nextInChain = nullptr,
            .module = cs_module,
            .entryPoint = { .data = compute.entry_point, .length = WGPU_STRLEN },
            .constantCount = 0,
            .constants = nullptr,
        },
    };

    const WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device->device, &pipe_desc);
    wgpuShaderModuleRelease(cs_module);
    if (!pipeline) return nullptr;

    PSO* pso = new PSO{};
    pso->device = device;
    pso->compute = pipeline;
    pso->is_compute = true;
    return pso;
}

void destroy_pso(PSO* pso) noexcept
{
    if (!pso) return;
    if (pso->render) wgpuRenderPipelineRelease(pso->render);
    if (pso->compute) wgpuComputePipelineRelease(pso->compute);
    delete pso;
}

void bind_pso(CommandBuffer* commands, const PSO* pso) noexcept
{
    assert(commands && commands->recording && pso);
    assert(pso->is_compute ? (commands->render_pass == nullptr) : (commands->render_pass != nullptr));
    commands->current_pso = pso;
    if (commands->render_pass && pso->render)
    {
        wgpuRenderPassEncoderSetPipeline(commands->render_pass, pso->render);
    }
    else if (commands->compute_pass && pso->compute)
    {
        wgpuComputePassEncoderSetPipeline(commands->compute_pass, pso->compute);
    }
}

void draw(CommandBuffer* commands, const void* root, uint32 vertex_count, uint32 instance_count, uint32 first_vertex, uint32 first_instance) noexcept
{
    assert(commands && commands->render_pass);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    wgpuRenderPassEncoderSetImmediates(commands->render_pass, 0, &root_offset, sizeof(root_offset));
    wgpuRenderPassEncoderDraw(commands->render_pass, vertex_count, instance_count, first_vertex, first_instance);
}

void draw_indexed(CommandBuffer* commands, const void* root, GpuRange indices, IndexType type, uint32 index_count, uint32 instance_count,
                  uint32 first_index, int32 vertex_offset, uint32 first_instance) noexcept
{
    assert(commands && commands->render_pass);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    wgpuRenderPassEncoderSetImmediates(commands->render_pass, 0, &root_offset, sizeof(root_offset));
    if (indices.gpu)
    {
        const GpuAddressInfo idx_info = resolve_gpu_address(commands->device, indices.gpu);
        track_command_buffer_heap(commands, idx_info.owner);
        const WGPUIndexFormat format = (type == IndexType::uint16) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        wgpuRenderPassEncoderSetIndexBuffer(commands->render_pass, idx_info.buffer, format, idx_info.offset, indices.size);
    }
    wgpuRenderPassEncoderDrawIndexed(commands->render_pass, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void draw_indirect(CommandBuffer* commands, const void* root, GpuRange arguments, uint32 draw_count, uint32 stride) noexcept
{
    assert(commands && commands->render_pass);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    wgpuRenderPassEncoderSetImmediates(commands->render_pass, 0, &root_offset, sizeof(root_offset));
    if (arguments.gpu)
    {
        const GpuAddressInfo arg_info = resolve_gpu_address(commands->device, arguments.gpu);
        track_command_buffer_heap(commands, arg_info.owner);
        const uint32 actual_stride = stride ? stride : 16;
        for (uint32 i = 0; i < draw_count; ++i)
        {
            wgpuRenderPassEncoderDrawIndirect(commands->render_pass, arg_info.buffer, arg_info.offset + i * actual_stride);
        }
    }
}

void draw_indexed_indirect(CommandBuffer* commands, const void* root, GpuRange indices, IndexType type, GpuRange arguments,
                           uint32 draw_count, uint32 stride) noexcept
{
    assert(commands && commands->render_pass);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    wgpuRenderPassEncoderSetImmediates(commands->render_pass, 0, &root_offset, sizeof(root_offset));
    if (indices.gpu)
    {
        const GpuAddressInfo idx_info = resolve_gpu_address(commands->device, indices.gpu);
        track_command_buffer_heap(commands, idx_info.owner);
        const WGPUIndexFormat format = (type == IndexType::uint16) ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
        wgpuRenderPassEncoderSetIndexBuffer(commands->render_pass, idx_info.buffer, format, idx_info.offset, indices.size);
    }
    if (arguments.gpu)
    {
        const GpuAddressInfo arg_info = resolve_gpu_address(commands->device, arguments.gpu);
        track_command_buffer_heap(commands, arg_info.owner);
        const uint32 actual_stride = stride ? stride : 20;
        for (uint32 i = 0; i < draw_count; ++i)
        {
            wgpuRenderPassEncoderDrawIndexedIndirect(commands->render_pass, arg_info.buffer, arg_info.offset + i * actual_stride);
        }
    }
}

void ensure_compute_pass(CommandBuffer* commands) noexcept
{
    assert(commands && commands->recording && !commands->render_pass);
    if (!commands->compute_pass)
    {
        const WGPUComputePassDescriptor comp_desc = {
            .nextInChain = nullptr,
            .label = { .data = "Compute Pass", .length = WGPU_STRLEN },
            .timestampWrites = nullptr,
        };
        commands->compute_pass = wgpuCommandEncoderBeginComputePass(commands->encoder, &comp_desc);
        if (commands->compute_pass)
        {
            const WGPUResourceTable active_table = resolve_resource_table(
                commands->device, commands->active_texture_heap, commands->active_sampler_heap);
            if (active_table)
            {
                wgpuComputePassEncoderSetResourceTable(commands->compute_pass, active_table);
            }
            if (commands->device->mega_compute_bind_group)
            {
                wgpuComputePassEncoderSetBindGroup(commands->compute_pass, 0, commands->device->mega_compute_bind_group, 0, nullptr);
            }
            if (commands->current_pso && commands->current_pso->compute)
            {
                wgpuComputePassEncoderSetPipeline(commands->compute_pass, commands->current_pso->compute);
            }
        }
    }
}

void dispatch(CommandBuffer* commands, const void* root, uint32x3 group_count) noexcept
{
    assert(commands);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    ensure_compute_pass(commands);
    assert(commands->compute_pass);
    wgpuComputePassEncoderSetImmediates(commands->compute_pass, 0, &root_offset, sizeof(root_offset));
    wgpuComputePassEncoderDispatchWorkgroups(commands->compute_pass, group_count.x, group_count.y, group_count.z);
}

void dispatch_indirect(CommandBuffer* commands, const void* root, GpuRange arguments) noexcept
{
    assert(commands);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    if (arguments.gpu)
    {
        const GpuAddressInfo arg_info = resolve_gpu_address(commands->device, arguments.gpu);
        track_command_buffer_heap(commands, arg_info.owner);
        assert(arg_info.offset % 4 == 0);
        if (arg_info.buffer == commands->device->mega_buffer && commands->device->indirect_dispatch_buffer)
        {
            if (commands->compute_pass)
            {
                wgpuComputePassEncoderEnd(commands->compute_pass);
                wgpuComputePassEncoderRelease(commands->compute_pass);
                commands->compute_pass = nullptr;
            }
            constexpr uint64 min_dispatch_size = 12ull;
            constexpr uint64 max_dispatch_size = Device::indirect_dispatch_slot_size;
            uint64 copy_size = arguments.size ? ((arguments.size + 3ull) & ~3ull) : min_dispatch_size;
            if (copy_size < min_dispatch_size) copy_size = min_dispatch_size;
            if (copy_size > max_dispatch_size) copy_size = max_dispatch_size;
            if (arg_info.offset + copy_size > commands->device->mega_buffer_capacity)
            {
                copy_size = (commands->device->mega_buffer_capacity > arg_info.offset)
                                ? ((commands->device->mega_buffer_capacity - arg_info.offset) & ~3ull)
                                : 0ull;
            }
            const uint32 slot_index = __atomic_fetch_add(&commands->device->indirect_dispatch_cursor, 1u, __ATOMIC_RELAXED) &
                                      (Device::indirect_dispatch_slot_count - 1);
            const uint64 dst_offset = static_cast<uint64>(slot_index) * Device::indirect_dispatch_slot_size;
            if (copy_size >= min_dispatch_size)
            {
                wgpuCommandEncoderCopyBufferToBuffer(commands->encoder, arg_info.buffer, arg_info.offset,
                                                    commands->device->indirect_dispatch_buffer, dst_offset, copy_size);
            }
            ensure_compute_pass(commands);
            assert(commands->compute_pass);
            wgpuComputePassEncoderSetImmediates(commands->compute_pass, 0, &root_offset, sizeof(root_offset));
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(commands->compute_pass, commands->device->indirect_dispatch_buffer, dst_offset);
            return;
        }
        ensure_compute_pass(commands);
        assert(commands->compute_pass);
        wgpuComputePassEncoderSetImmediates(commands->compute_pass, 0, &root_offset, sizeof(root_offset));
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(commands->compute_pass, arg_info.buffer, arg_info.offset);
        return;
    }
    ensure_compute_pass(commands);
    assert(commands->compute_pass);
    wgpuComputePassEncoderSetImmediates(commands->compute_pass, 0, &root_offset, sizeof(root_offset));
}

void draw_meshlets(CommandBuffer* commands, const void* root, uint32x3 group_count) noexcept
{
    assert(commands && commands->render_pass);
    const uint32 instance_count = group_count.x * group_count.y * group_count.z;
    if (instance_count == 0) return;

    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    wgpuRenderPassEncoderSetImmediates(commands->render_pass, 0, &root_offset, sizeof(root_offset));
    wgpuRenderPassEncoderDraw(commands->render_pass, 36, instance_count, 0, 0);
}

void draw_meshlets_indirect(CommandBuffer* commands, const void* root, GpuRange arguments, uint32 draw_count, uint32 stride) noexcept
{
    assert(commands && commands->render_pass);
    uint32 root_offset = 0;
    if (root)
    {
        const GpuAddressInfo root_info = resolve_gpu_address(commands->device, root);
        track_command_buffer_heap(commands, root_info.owner);
        root_offset = static_cast<uint32>(root_info.offset);
    }
    wgpuRenderPassEncoderSetImmediates(commands->render_pass, 0, &root_offset, sizeof(root_offset));
    if (arguments.gpu)
    {
        const GpuAddressInfo arg_info = resolve_gpu_address(commands->device, arguments.gpu);
        track_command_buffer_heap(commands, arg_info.owner);
        const uint32 actual_stride = stride ? stride : 12;
        for (uint32 i = 0; i < draw_count; ++i)
        {
            wgpuRenderPassEncoderDrawIndirect(commands->render_pass, arg_info.buffer, arg_info.offset + i * actual_stride);
        }
    }
}

void set_texture_descriptor_heap(CommandBuffer* commands, TextureDescriptorHeap* heap) noexcept
{
    assert(commands);
    commands->active_texture_heap = heap;
    if (commands->render_pass || commands->compute_pass)
    {
        const WGPUResourceTable active_table = resolve_resource_table(
            commands->device, commands->active_texture_heap, commands->active_sampler_heap);
        if (active_table)
        {
            if (commands->render_pass) wgpuRenderPassEncoderSetResourceTable(commands->render_pass, active_table);
            if (commands->compute_pass) wgpuComputePassEncoderSetResourceTable(commands->compute_pass, active_table);
        }
    }
}

void set_sampler_descriptor_heap(CommandBuffer* commands, SamplerDescriptorHeap* heap) noexcept
{
    assert(commands);
    commands->active_sampler_heap = heap;
    if (commands->render_pass || commands->compute_pass)
    {
        const WGPUResourceTable active_table = resolve_resource_table(
            commands->device, commands->active_texture_heap, commands->active_sampler_heap);
        if (active_table)
        {
            if (commands->render_pass) wgpuRenderPassEncoderSetResourceTable(commands->render_pass, active_table);
            if (commands->compute_pass) wgpuComputePassEncoderSetResourceTable(commands->compute_pass, active_table);
        }
    }
}

void copy_memory(CommandBuffer* commands, GpuRange source, GpuRange destination) noexcept
{
    assert(commands && commands->encoder);
    if (!source.gpu || !destination.gpu || source.size == 0 || destination.size == 0) return;
    const GpuAddressInfo src_info = resolve_gpu_address(commands->device, source.gpu);
    const GpuAddressInfo dst_info = resolve_gpu_address(commands->device, destination.gpu);
    if (!src_info.buffer || !dst_info.buffer) return;

    track_command_buffer_heap(commands, src_info.owner);
    track_command_buffer_heap(commands, dst_info.owner);

    const uint64 size = source.size < destination.size ? source.size : destination.size;
    if (size == 0) return;

    if (dst_info.owner && dst_info.owner->memory == MemoryType::readback)
    {
        record_readback_write(commands, dst_info.owner, dst_info.offset, size);
    }

    if (commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }

    if (src_info.buffer == dst_info.buffer)
    {
        if (src_info.offset == dst_info.offset) return;

        WGPUBuffer scratch_buf = get_or_create_pool_scratch(commands->pool, size);
        assert(scratch_buf);
        const uint64 scratch_cap = commands->pool->scratch_capacity;

        if (src_info.offset < dst_info.offset && src_info.offset + size > dst_info.offset)
        {
            uint64 remaining = size;
            while (remaining > 0)
            {
                const uint64 chunk = remaining < scratch_cap ? remaining : scratch_cap;
                const uint64 chunk_offset = remaining - chunk;
                wgpuCommandEncoderCopyBufferToBuffer(commands->encoder,
                    src_info.buffer, src_info.offset + chunk_offset,
                    scratch_buf, 0, chunk);
                wgpuCommandEncoderCopyBufferToBuffer(commands->encoder,
                    scratch_buf, 0,
                    dst_info.buffer, dst_info.offset + chunk_offset, chunk);
                remaining -= chunk;
            }
        }
        else
        {
            for (uint64 copied = 0; copied < size; copied += scratch_cap)
            {
                const uint64 chunk = (size - copied < scratch_cap) ? (size - copied) : scratch_cap;
                wgpuCommandEncoderCopyBufferToBuffer(commands->encoder,
                    src_info.buffer, src_info.offset + copied,
                    scratch_buf, 0, chunk);
                wgpuCommandEncoderCopyBufferToBuffer(commands->encoder,
                    scratch_buf, 0,
                    dst_info.buffer, dst_info.offset + copied, chunk);
            }
        }
    }
    else
    {
        wgpuCommandEncoderCopyBufferToBuffer(commands->encoder,
            src_info.buffer, src_info.offset,
            dst_info.buffer, dst_info.offset, size);
    }
}

void copy_memory_to_texture(CommandBuffer* commands, GpuRange source, Texture* destination, const TextureCopyDesc& copy) noexcept
{
    assert(commands && commands->encoder && destination && destination->texture);
    if (!source.gpu) return;
    const GpuAddressInfo src_info = resolve_gpu_address(commands->device, source.gpu);
    track_command_buffer_heap(commands, src_info.owner);

    if (commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }

    const uint32 mip_width = destination->desc.extent.x >> copy.mip_level;
    const uint32 mip_height = destination->desc.extent.y >> copy.mip_level;
    const uint32 mip_depth = destination->desc.extent.z >> copy.mip_level;
    const uint32 max_w = mip_width ? mip_width : 1;
    const uint32 max_h = mip_height ? mip_height : 1;
    const uint32 max_d = mip_depth ? mip_depth : 1;

    const uint32 width = copy.extent.x ? copy.extent.x : max_w - copy.offset.x;
    const uint32 height = copy.extent.y ? copy.extent.y : max_h - copy.offset.y;
    const uint32 depth = copy.extent.z ? copy.extent.z : max_d - copy.offset.z;
    const uint32 slices = copy.slice_count ? copy.slice_count : destination->desc.layer_count - copy.base_slice;

    const TextureFormatInfo format_info = get_texture_format_info(destination->desc.format);
    if (format_info.depth) return;
    const uint32 block_w = format_info.block_extent.x ? format_info.block_extent.x : 1;
    const uint32 block_h = format_info.block_extent.y ? format_info.block_extent.y : 1;
    const uint32 bytes_per_block = format_info.bytes_per_block ? format_info.bytes_per_block : 4;

    const uint32 width_in_blocks = (width + block_w - 1) / block_w;
    const uint32 height_in_blocks = (height + block_h - 1) / block_h;
    const uint64 row_bytes = static_cast<uint64>(width_in_blocks) * bytes_per_block;
    const uint64 row_pitch = copy.row_pitch_bytes ? copy.row_pitch_bytes : row_bytes;
    const uint64 slice_pitch = copy.slice_pitch_bytes ? copy.slice_pitch_bytes : row_pitch * height_in_blocks;

    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    if (format_info.depth && !format_info.stencil) aspect = WGPUTextureAspect_DepthOnly;
    else if (!format_info.depth && format_info.stencil) aspect = WGPUTextureAspect_StencilOnly;

    // Fast path: single copy when height <= 1 and depth <= 1 and slices <= 1
    if (height_in_blocks <= 1 && depth <= 1 && slices <= 1)
    {
        const WGPUTexelCopyBufferInfo src_view = {
            .layout = {
                .offset = src_info.offset,
                .bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED,
                .rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED,
            },
            .buffer = src_info.buffer,
        };
        const WGPUTexelCopyTextureInfo dst_view = {
            .texture = destination->texture,
            .mipLevel = copy.mip_level,
            .origin = {
                .x = copy.offset.x,
                .y = copy.offset.y,
                .z = (destination->desc.type == TextureType::three_d ? copy.offset.z : copy.base_slice),
            },
            .aspect = aspect,
        };
        const WGPUExtent3D copy_size = { .width = width, .height = height, .depthOrArrayLayers = 1 };
        wgpuCommandEncoderCopyBufferToTexture(commands->encoder, &src_view, &dst_view, &copy_size);
        return;
    }

    // Fast path: 256-byte aligned row pitch
    if (row_pitch % 256 == 0 && ((slices <= 1 && depth <= 1) || slice_pitch % row_pitch == 0))
    {
        const WGPUTexelCopyBufferInfo src_view = {
            .layout = {
                .offset = src_info.offset,
                .bytesPerRow = static_cast<uint32>(row_pitch),
                .rowsPerImage = static_cast<uint32>(slice_pitch / row_pitch),
            },
            .buffer = src_info.buffer,
        };
        const WGPUTexelCopyTextureInfo dst_view = {
            .texture = destination->texture,
            .mipLevel = copy.mip_level,
            .origin = {
                .x = copy.offset.x,
                .y = copy.offset.y,
                .z = (destination->desc.type == TextureType::three_d ? copy.offset.z : copy.base_slice),
            },
            .aspect = aspect,
        };
        const WGPUExtent3D copy_size = {
            .width = width,
            .height = height,
            .depthOrArrayLayers = (destination->desc.type == TextureType::three_d ? depth : slices),
        };
        wgpuCommandEncoderCopyBufferToTexture(commands->encoder, &src_view, &dst_view, &copy_size);
        return;
    }

    // Robust path: row-by-row copy for unaligned / tightly packed row pitches
    for (uint32 s = 0; s < slices; ++s)
    {
        const uint32 slice_idx = copy.base_slice + s;
        for (uint32 z = 0; z < depth; ++z)
        {
            for (uint32 y = 0; y < height_in_blocks; ++y)
            {
                const uint64 plane_offset = (destination->desc.type == TextureType::three_d)
                                                ? (static_cast<uint64>(z) * slice_pitch)
                                                : (static_cast<uint64>(s) * slice_pitch);
                const uint64 row_offset = src_info.offset + plane_offset + static_cast<uint64>(y) * row_pitch;
                const WGPUTexelCopyBufferInfo src_view = {
                    .layout = {
                        .offset = row_offset,
                        .bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED,
                        .rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED,
                    },
                    .buffer = src_info.buffer,
                };
                const WGPUTexelCopyTextureInfo dst_view = {
                    .texture = destination->texture,
                    .mipLevel = copy.mip_level,
                    .origin = {
                        .x = copy.offset.x,
                        .y = copy.offset.y + y * block_h,
                        .z = (destination->desc.type == TextureType::three_d ? copy.offset.z + z : slice_idx),
                    },
                    .aspect = aspect,
                };
                const WGPUExtent3D copy_size = {
                    .width = width,
                    .height = block_h,
                    .depthOrArrayLayers = 1,
                };
                wgpuCommandEncoderCopyBufferToTexture(commands->encoder, &src_view, &dst_view, &copy_size);
            }
        }
    }
}

void copy_texture_to_memory(CommandBuffer* commands, Texture* source, GpuRange destination, const TextureCopyDesc& copy) noexcept
{
    assert(commands && commands->encoder && source && source->texture);
    if (!destination.gpu) return;
    const GpuAddressInfo dst_info = resolve_gpu_address(commands->device, destination.gpu);
    track_command_buffer_heap(commands, dst_info.owner);

    if (commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }

    const uint32 mip_width = source->desc.extent.x >> copy.mip_level;
    const uint32 mip_height = source->desc.extent.y >> copy.mip_level;
    const uint32 mip_depth = source->desc.extent.z >> copy.mip_level;
    const uint32 max_w = mip_width ? mip_width : 1;
    const uint32 max_h = mip_height ? mip_height : 1;
    const uint32 max_d = mip_depth ? mip_depth : 1;

    const uint32 width = copy.extent.x ? copy.extent.x : max_w - copy.offset.x;
    const uint32 height = copy.extent.y ? copy.extent.y : max_h - copy.offset.y;
    const uint32 depth = copy.extent.z ? copy.extent.z : max_d - copy.offset.z;
    const uint32 slices = copy.slice_count ? copy.slice_count : source->desc.layer_count - copy.base_slice;

    const TextureFormatInfo format_info = get_texture_format_info(source->desc.format);
    const uint32 block_w = format_info.block_extent.x ? format_info.block_extent.x : 1;
    const uint32 block_h = format_info.block_extent.y ? format_info.block_extent.y : 1;
    const uint32 bytes_per_block = format_info.bytes_per_block ? format_info.bytes_per_block : 4;

    const uint32 width_in_blocks = (width + block_w - 1) / block_w;
    const uint32 height_in_blocks = (height + block_h - 1) / block_h;
    const uint64 row_bytes = static_cast<uint64>(width_in_blocks) * bytes_per_block;
    const uint64 row_pitch = copy.row_pitch_bytes ? copy.row_pitch_bytes : row_bytes;
    const uint64 slice_pitch = copy.slice_pitch_bytes ? copy.slice_pitch_bytes : row_pitch * height_in_blocks;

    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    if (format_info.depth && !format_info.stencil) aspect = WGPUTextureAspect_DepthOnly;
    else if (!format_info.depth && format_info.stencil) aspect = WGPUTextureAspect_StencilOnly;

    // Fast path: single copy when height <= 1 and depth <= 1 and slices <= 1
    if (height_in_blocks <= 1 && depth <= 1 && slices <= 1)
    {
        const WGPUTexelCopyTextureInfo src_view = {
            .texture = source->texture,
            .mipLevel = copy.mip_level,
            .origin = {
                .x = copy.offset.x,
                .y = copy.offset.y,
                .z = (source->desc.type == TextureType::three_d ? copy.offset.z : copy.base_slice),
            },
            .aspect = aspect,
        };
        const WGPUTexelCopyBufferInfo dst_view = {
            .layout = {
                .offset = dst_info.offset,
                .bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED,
                .rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED,
            },
            .buffer = dst_info.buffer,
        };
        const WGPUExtent3D copy_size = { .width = width, .height = height, .depthOrArrayLayers = 1 };
        wgpuCommandEncoderCopyTextureToBuffer(commands->encoder, &src_view, &dst_view, &copy_size);
        if (dst_info.owner && dst_info.owner->memory == MemoryType::readback)
        {
            record_readback_write(commands, dst_info.owner, dst_info.offset, static_cast<uint64>(width_in_blocks) * bytes_per_block);
        }
        return;
    }

    // Fast path: 256-byte aligned row pitch
    if (row_pitch % 256 == 0 && ((slices <= 1 && depth <= 1) || slice_pitch % row_pitch == 0))
    {
        const WGPUTexelCopyTextureInfo src_view = {
            .texture = source->texture,
            .mipLevel = copy.mip_level,
            .origin = {
                .x = copy.offset.x,
                .y = copy.offset.y,
                .z = (source->desc.type == TextureType::three_d ? copy.offset.z : copy.base_slice),
            },
            .aspect = aspect,
        };
        const WGPUTexelCopyBufferInfo dst_view = {
            .layout = {
                .offset = dst_info.offset,
                .bytesPerRow = static_cast<uint32>(row_pitch),
                .rowsPerImage = static_cast<uint32>(slice_pitch / row_pitch),
            },
            .buffer = dst_info.buffer,
        };
        const WGPUExtent3D copy_size = {
            .width = width,
            .height = height,
            .depthOrArrayLayers = (source->desc.type == TextureType::three_d ? depth : slices),
        };
        wgpuCommandEncoderCopyTextureToBuffer(commands->encoder, &src_view, &dst_view, &copy_size);
        if (dst_info.owner && dst_info.owner->memory == MemoryType::readback)
        {
            const uint32 total_slices = (source->desc.type == TextureType::three_d ? depth : slices);
            for (uint32 s = 0; s < total_slices; ++s)
            {
                for (uint32 y = 0; y < height_in_blocks; ++y)
                {
                    const uint64 row_offset = dst_info.offset + static_cast<uint64>(s) * slice_pitch + static_cast<uint64>(y) * row_pitch;
                    record_readback_write(commands, dst_info.owner, row_offset, row_bytes);
                }
            }
        }
        return;
    }

    // Robust path: row-by-row copy for unaligned / tightly packed row pitches
    for (uint32 s = 0; s < slices; ++s)
    {
        const uint32 slice_idx = copy.base_slice + s;
        for (uint32 z = 0; z < depth; ++z)
        {
            for (uint32 y = 0; y < height_in_blocks; ++y)
            {
                const uint64 plane_offset = (source->desc.type == TextureType::three_d)
                                                ? (static_cast<uint64>(z) * slice_pitch)
                                                : (static_cast<uint64>(s) * slice_pitch);
                const uint64 row_offset = dst_info.offset + plane_offset + static_cast<uint64>(y) * row_pitch;
                const WGPUTexelCopyTextureInfo src_view = {
                    .texture = source->texture,
                    .mipLevel = copy.mip_level,
                    .origin = {
                        .x = copy.offset.x,
                        .y = copy.offset.y + y * block_h,
                        .z = (source->desc.type == TextureType::three_d ? copy.offset.z + z : slice_idx),
                    },
                    .aspect = aspect,
                };
                const WGPUTexelCopyBufferInfo dst_view = {
                    .layout = {
                        .offset = row_offset,
                        .bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED,
                        .rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED,
                    },
                    .buffer = dst_info.buffer,
                };
                const WGPUExtent3D copy_size = {
                    .width = width,
                    .height = block_h,
                    .depthOrArrayLayers = 1,
                };
                wgpuCommandEncoderCopyTextureToBuffer(commands->encoder, &src_view, &dst_view, &copy_size);
                if (dst_info.owner && dst_info.owner->memory == MemoryType::readback)
                {
                    record_readback_write(commands, dst_info.owner, row_offset, row_bytes);
                }
            }
        }
    }
}

void barrier(CommandBuffer* commands, Stage before, Access before_access, Stage after, Access after_access) noexcept
{
    (void)before; (void)before_access; (void)after; (void)after_access;
    if (commands && commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }
}

void write_timestamp(CommandBuffer* commands, uint64* cpu_destination, Stage stage) noexcept
{
    (void)stage;
    assert(commands);
    if (!commands->device || commands->device->timestamp_query_count == 0) return;
    if (!commands->recording || !cpu_destination) return;
    if (commands->timestamp_count == commands->timestamp_capacity)
    {
        const uint32 new_cap = commands->timestamp_capacity == 0 ? 64 : commands->timestamp_capacity * 2;
        CommandBuffer::TimestampRecord* new_recs =
            static_cast<CommandBuffer::TimestampRecord*>(malloc(new_cap * sizeof(CommandBuffer::TimestampRecord)));
        if (commands->timestamp_records)
        {
            memcpy(new_recs, commands->timestamp_records, commands->timestamp_count * sizeof(CommandBuffer::TimestampRecord));
            free(commands->timestamp_records);
        }
        commands->timestamp_records = new_recs;
        commands->timestamp_capacity = new_cap;
    }
    commands->timestamp_records[commands->timestamp_count++] = {
        .destination = cpu_destination,
        .recorded_time = 0,
    };
}

void read_timestamps(CommandPool* pool) noexcept
{
    assert(pool);
    for (CommandBuffer* cb = pool->first; cb != nullptr; cb = cb->next)
    {
        if (!cb->submitted || cb->timestamp_count == 0) continue;
        for (uint32 i = 0; i < cb->timestamp_count; ++i)
        {
            if (cb->timestamp_records[i].destination)
            {
                *cb->timestamp_records[i].destination = cb->timestamp_records[i].recorded_time;
            }
        }
        cb->timestamp_count = 0;
    }
}

GpuHeap create_gpu_heap(Device* device, uint64 byte_count, MemoryType memory) noexcept
{
    assert(device);
    if (byte_count == 0) return {};

    constexpr uint64 sync_chunk_size = 256;
    const uint64 aligned_size = (byte_count + sync_chunk_size - 1) & ~(sync_chunk_size - 1);

    device->heap_lock.lock();

    uint32 heap_id = 0;
    for (uint32 i = 1; i < Device::max_heaps; ++i)
    {
        const uint32 candidate = (device->next_heap_id + i - 1) % (Device::max_heaps - 1) + 1;
        if (!device->heap_table[candidate])
        {
            heap_id = candidate;
            device->next_heap_id = (candidate % (Device::max_heaps - 1)) + 1;
            break;
        }
    }
    if (heap_id == 0)
    {
        device->heap_lock.unlock();
        report_error("create_gpu_heap", "exceeded maximum active GPU heaps");
        return {};
    }

    WGPUBuffer buffer = nullptr;
    uint64 mega_offset = 0;
    uint64 base_addr = 0;

    if (memory == MemoryType::readback)
    {
        const WGPUBufferDescriptor buffer_desc = {
            .nextInChain = nullptr,
            .label = { .data = "GpuHeap Readback Buffer", .length = WGPU_STRLEN },
            .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
            .size = aligned_size,
            .mappedAtCreation = WGPU_FALSE,
        };
        buffer = wgpuDeviceCreateBuffer(device->device, &buffer_desc);
        if (!buffer)
        {
            device->heap_lock.unlock();
            report_error("create_gpu_heap", "wgpuDeviceCreateBuffer failed for readback heap");
            return {};
        }
        mega_offset = 0;
#if defined(__wasm32__)
        if (!device->readback_heaps)
        {
            device->next_readback_vaddr = 0x8000'0000ull;
        }
        if (device->next_readback_vaddr + aligned_size > 0xFFFF'FF00ull)
        {
            uint64 candidate = 0x8000'0000ull;
            bool overlap = true;
            while (overlap && candidate + aligned_size <= 0xFFFF'FF00ull)
            {
                overlap = false;
                for (GpuHeapOwner* h = device->readback_heaps; h != nullptr; h = h->readback_next)
                {
                    if (candidate < h->base_address + h->aligned_size && candidate + aligned_size > h->base_address)
                    {
                        candidate = h->base_address + h->aligned_size;
                        overlap = true;
                        break;
                    }
                }
            }
            device->next_readback_vaddr = candidate;
        }
        assert(device->next_readback_vaddr + aligned_size <= 0xFFFF'FF00ull);
        base_addr = device->next_readback_vaddr;
        device->next_readback_vaddr += aligned_size;
#else
        base_addr = (static_cast<uint64>(heap_id + 1) << 32);
#endif
    }
    bool was_gpu_tainted = false;
    if (memory != MemoryType::readback)
    {
        mega_offset = arena_allocate(device, aligned_size, memory == MemoryType::gpu_only, &was_gpu_tainted);
        if (mega_offset == UINT64_MAX)
        {
            device->heap_lock.unlock();
            report_error("create_gpu_heap", "exceeded Global Virtual Arena Mega-Buffer capacity");
            return {};
        }
        buffer = device->mega_buffer;
#if defined(__wasm32__)
        base_addr = mega_offset;
#else
        base_addr = 0x1'0000'0000ull + mega_offset;
#endif
    }

    byte* cpu_shadow = nullptr;
    byte* synced_snapshot = nullptr;
    void* raw_alloc = nullptr;
    void* raw_snap = nullptr;

#if defined(NOGRAPHICSAPI_WEBGPU_PERSISTENT_MAP)
    const bool needs_initial_sync = false;
    if (memory == MemoryType::cpu_visible)
    {
        cpu_shadow = static_cast<byte*>(wgpuBufferGetMappedRange(
            device->mega_buffer, static_cast<size_t>(mega_offset), static_cast<size_t>(aligned_size)));
        assert(cpu_shadow != nullptr);
        memset(cpu_shadow, 0, static_cast<size_t>(aligned_size));
#if defined(__EMSCRIPTEN__)
        if (was_gpu_tainted)
        {
            static constexpr byte zero_chunk[4096] = {};
            uint64 cleared = 0;
            while (cleared < aligned_size)
            {
                const uint64 chunk = (aligned_size - cleared < sizeof(zero_chunk)) ? (aligned_size - cleared) : sizeof(zero_chunk);
                wgpuBufferWriteMappedRange(
                    device->mega_buffer, static_cast<size_t>(mega_offset + cleared), zero_chunk, static_cast<size_t>(chunk));
                cleared += chunk;
            }
        }
#else
        (void)was_gpu_tainted;
#endif
    }
    else if (memory == MemoryType::readback)
#else
    const bool needs_initial_sync = (memory == MemoryType::cpu_visible) && was_gpu_tainted;
    if (memory == MemoryType::cpu_visible || memory == MemoryType::readback)
#endif
    {
        const int ret1 = posix_memalign(&raw_alloc, 16, static_cast<size_t>(aligned_size));
        const int ret2 = posix_memalign(&raw_snap, 16, static_cast<size_t>(aligned_size));
        if (ret1 != 0 || !raw_alloc || ret2 != 0 || !raw_snap)
        {
            if (raw_alloc) free(raw_alloc);
            if (raw_snap) free(raw_snap);
            if (memory == MemoryType::readback)
            {
                wgpuBufferDestroy(buffer);
                wgpuBufferRelease(buffer);
            }
            else
            {
                arena_free(device, mega_offset, was_gpu_tainted);
            }
            device->heap_lock.unlock();
            report_error("create_gpu_heap", "failed to allocate 16-byte aligned CPU shadow memory");
            return {};
        }
        cpu_shadow = static_cast<byte*>(raw_alloc);
        synced_snapshot = static_cast<byte*>(raw_snap);
        memset(cpu_shadow, 0, static_cast<size_t>(aligned_size));
        memset(synced_snapshot, 0, static_cast<size_t>(aligned_size));
    }

    GpuHeapOwner* owner = new GpuHeapOwner{
        .device = device,
        .buffer = buffer,
        .byte_count = byte_count,
        .aligned_size = aligned_size,
        .memory = memory,
        .cpu_shadow = cpu_shadow,
        .synced_snapshot = synced_snapshot,
        .raw_allocation = raw_alloc,
        .raw_snapshot = raw_snap,
        .base_address = base_addr,
        .heap_id = heap_id,
        .next = nullptr,
        .prev = nullptr,
        .mega_offset = mega_offset,
        .readback_next = nullptr,
        .readback_prev = nullptr,
        .needs_initial_sync = needs_initial_sync,
    };

    device->heap_table[heap_id] = owner;

    if (memory == MemoryType::cpu_visible)
    {
        owner->next = device->cpu_visible_heaps;
        if (device->cpu_visible_heaps) device->cpu_visible_heaps->prev = owner;
        device->cpu_visible_heaps = owner;
    }
    else if (memory == MemoryType::readback)
    {
        owner->readback_next = device->readback_heaps;
        if (device->readback_heaps) device->readback_heaps->readback_prev = owner;
        device->readback_heaps = owner;
    }

    device->heap_lock.unlock();

    return {
        .range = {
            .cpu = cpu_shadow,
            .gpu = reinterpret_cast<byte*>(static_cast<uintptr>(owner->base_address)),
            .size = byte_count,
        },
        .owner = owner,
    };
}

void destroy_gpu_heap(const GpuHeap& heap) noexcept
{
    if (!heap.owner) return;

    GpuHeapOwner* owner = const_cast<GpuHeapOwner*>(heap.owner);
    Device* device = owner->device;
    assert(device);

    device->heap_lock.lock();

    if (owner->memory == MemoryType::cpu_visible)
    {
        if (owner->prev) owner->prev->next = owner->next;
        else if (device->cpu_visible_heaps == owner) device->cpu_visible_heaps = owner->next;
        if (owner->next) owner->next->prev = owner->prev;
    }
    else if (owner->memory == MemoryType::readback)
    {
        if (owner->readback_prev) owner->readback_prev->readback_next = owner->readback_next;
        else if (device->readback_heaps == owner) device->readback_heaps = owner->readback_next;
        if (owner->readback_next) owner->readback_next->readback_prev = owner->readback_prev;
    }

    if (owner->heap_id > 0 && owner->heap_id < Device::max_heaps)
    {
        assert(device->heap_table[owner->heap_id] == owner);
        device->heap_table[owner->heap_id] = nullptr;
    }

    if (owner->memory != MemoryType::readback)
    {
        arena_free(device, owner->mega_offset, true);
    }

    device->heap_lock.unlock();

    if (owner->memory == MemoryType::readback)
    {
        if (owner->buffer)
        {
            wgpuBufferDestroy(owner->buffer);
            wgpuBufferRelease(owner->buffer);
            owner->buffer = nullptr;
        }
    }

    if (owner->raw_allocation)
    {
        free(owner->raw_allocation);
        owner->raw_allocation = nullptr;
        owner->cpu_shadow = nullptr;
    }
    if (owner->raw_snapshot)
    {
        free(owner->raw_snapshot);
        owner->raw_snapshot = nullptr;
        owner->synced_snapshot = nullptr;
    }
    if (owner->pending_ranges)
    {
        free(owner->pending_ranges);
        owner->pending_ranges = nullptr;
    }

    delete owner;
}

TextureHeap create_texture_heap(Device* device, uint64 byte_count) noexcept
{
    assert(device);
    TextureHeapOwner* owner = new TextureHeapOwner{};
    owner->device = device;
    return {
        .size = byte_count,
        .owner = owner,
    };
}

void destroy_texture_heap(const TextureHeap& heap) noexcept
{
    if (heap.owner)
    {
        delete const_cast<TextureHeapOwner*>(heap.owner);
    }
}

SizeAlign get_texture_size_align(Device* device, const TextureDesc& desc) noexcept
{
    (void)device;
    uint32 bpp = 4;
    switch (desc.format)
    {
    case Format::r8_unorm:
    case Format::r8_uint:
    case Format::s8_uint:
        bpp = 1; break;
    case Format::rg8_unorm:
    case Format::rg8_uint:
    case Format::r16_unorm:
    case Format::r16_uint:
    case Format::r16_float:
    case Format::d16_unorm:
        bpp = 2; break;
    case Format::rgba8_unorm:
    case Format::rgba8_srgb:
    case Format::rgba8_uint:
    case Format::bgra8_unorm:
    case Format::bgra8_srgb:
    case Format::rg16_unorm:
    case Format::rg16_uint:
    case Format::rg16_float:
    case Format::r32_uint:
    case Format::r32_float:
    case Format::d24_unorm_s8_uint:
    case Format::d32_float:
    case Format::rgb10a2_unorm:
    case Format::rg11b10_float:
        bpp = 4; break;
    case Format::rgba16_unorm:
    case Format::rgba16_uint:
    case Format::rgba16_float:
    case Format::rg32_uint:
    case Format::rg32_float:
    case Format::d32_float_s8_uint:
        bpp = 8; break;
    case Format::rgba32_uint:
    case Format::rgba32_float:
        bpp = 16; break;
    default:
        bpp = 4; break;
    }
    const uint64 size = static_cast<uint64>(desc.extent.x) * desc.extent.y * desc.extent.z * desc.layer_count * bpp;
    const uint64 aligned_size = (size + 255u) & ~255u;
    return {.size = aligned_size > 0 ? aligned_size : 256, .align = 256};
}

Texture* create_texture(Device* device, const TextureDesc& desc, const TextureHeap& heap, uint64 offset) noexcept
{
    (void)offset;
    assert(device && heap.owner && heap.owner->device == device);

    WGPUTextureUsage usage = WGPUTextureUsage_None;
    const uint32 u = static_cast<uint32>(desc.usage);
    if (u & static_cast<uint32>(TextureUsage::sampled))
        usage |= WGPUTextureUsage_TextureBinding;
    if (u & static_cast<uint32>(TextureUsage::storage))
        usage |= WGPUTextureUsage_StorageBinding;
    if (u & static_cast<uint32>(TextureUsage::color_attachment))
        usage |= WGPUTextureUsage_RenderAttachment;
    if (u & static_cast<uint32>(TextureUsage::depth_stencil_attachment))
        usage |= WGPUTextureUsage_RenderAttachment;
    if (u & static_cast<uint32>(TextureUsage::transfer_source))
        usage |= WGPUTextureUsage_CopySrc;
    if (u & static_cast<uint32>(TextureUsage::transfer_destination))
        usage |= WGPUTextureUsage_CopyDst;

    WGPUTextureDimension dim = WGPUTextureDimension_2D;
    if (desc.type == TextureType::one_d) dim = WGPUTextureDimension_1D;
    else if (desc.type == TextureType::three_d) dim = WGPUTextureDimension_3D;

    const WGPUTextureFormat base_format = to_wgpu_format(desc.format);
    WGPUTextureFormat view_formats[2] = {base_format, WGPUTextureFormat_Undefined};
    size_t view_format_count = 0;
    if (desc.mutable_format)
    {
        if (base_format == WGPUTextureFormat_RGBA8Unorm || base_format == WGPUTextureFormat_RGBA8UnormSrgb)
        {
            view_formats[0] = WGPUTextureFormat_RGBA8Unorm;
            view_formats[1] = WGPUTextureFormat_RGBA8UnormSrgb;
            view_format_count = 2;
        }
        else if (base_format == WGPUTextureFormat_BGRA8Unorm || base_format == WGPUTextureFormat_BGRA8UnormSrgb)
        {
            view_formats[0] = WGPUTextureFormat_BGRA8Unorm;
            view_formats[1] = WGPUTextureFormat_BGRA8UnormSrgb;
            view_format_count = 2;
        }
    }

    const WGPUTextureDescriptor wgpu_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Texture", .length = WGPU_STRLEN },
        .usage = usage,
        .dimension = dim,
        .size = {
            .width = desc.extent.x > 0 ? desc.extent.x : 1,
            .height = desc.extent.y > 0 ? desc.extent.y : 1,
            .depthOrArrayLayers = (desc.type == TextureType::three_d) ? (desc.extent.z > 0 ? desc.extent.z : 1)
                                                                  : (desc.layer_count > 0 ? desc.layer_count : 1),
        },
        .format = base_format,
        .mipLevelCount = desc.mip_levels > 0 ? desc.mip_levels : 1,
        .sampleCount = 1,
        .viewFormatCount = view_format_count,
        .viewFormats = view_format_count ? view_formats : nullptr,
    };

    WGPUTexture tex = wgpuDeviceCreateTexture(device->device, &wgpu_desc);
    if (!tex)
    {
        report_error("create_texture", "wgpuDeviceCreateTexture failed");
        return nullptr;
    }

    Texture* result = new Texture{};
    result->device = device;
    result->texture = tex;
    result->desc = desc;
    return result;
}

void destroy_texture(Texture* texture) noexcept
{
    if (!texture) return;
    if (texture->texture)
    {
        wgpuTextureDestroy(texture->texture);
        wgpuTextureRelease(texture->texture);
    }
    delete texture;
}

void initialize_texture(CommandBuffer* commands, Texture* texture) noexcept
{
    assert(commands && commands->recording && !commands->render_pass && texture && texture->texture);
    if (commands->compute_pass)
    {
        wgpuComputePassEncoderEnd(commands->compute_pass);
        wgpuComputePassEncoderRelease(commands->compute_pass);
        commands->compute_pass = nullptr;
    }
}

WGPUTextureView create_texture_view(const Texture* texture, TextureDescriptorType type, const TextureDescriptorDesc& desc) noexcept
{
    if (!texture || !texture->texture) return nullptr;

    const WGPUTextureFormat format = (desc.format != Format::undefined) ? to_wgpu_format(desc.format) : to_wgpu_format(texture->desc.format);

    WGPUTextureViewDimension dimension = WGPUTextureViewDimension_2D;
    if (texture->desc.type == TextureType::one_d)
    {
        dimension = WGPUTextureViewDimension_1D;
    }
    else if (texture->desc.type == TextureType::three_d)
    {
        dimension = WGPUTextureViewDimension_3D;
    }
    else if (texture->desc.type == TextureType::cube_array)
    {
        dimension = WGPUTextureViewDimension_CubeArray;
    }
    else if (texture->desc.type == TextureType::cube)
    {
        const uint32 layers = desc.layer_count ? desc.layer_count : (texture->desc.layer_count - desc.base_layer);
        dimension = (layers > 6) ? WGPUTextureViewDimension_CubeArray : WGPUTextureViewDimension_Cube;
    }
    else if (texture->desc.type == TextureType::two_d_array)
    {
        dimension = WGPUTextureViewDimension_2DArray;
    }
    else
    {
        const uint32 layers = desc.layer_count ? desc.layer_count : (texture->desc.layer_count - desc.base_layer);
        dimension = (layers > 1) ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
    }

    WGPUTextureAspect aspect = WGPUTextureAspect_All;
    if (desc.aspect == TextureAspect::depth || texture->desc.format == Format::d32_float || texture->desc.format == Format::d16_unorm)
    {
        aspect = WGPUTextureAspect_DepthOnly;
    }
    else if (desc.aspect == TextureAspect::stencil || texture->desc.format == Format::s8_uint)
    {
        aspect = WGPUTextureAspect_StencilOnly;
    }

    const WGPUTextureUsage usage = (type == TextureDescriptorType::storage) ? WGPUTextureUsage_StorageBinding : WGPUTextureUsage_TextureBinding;

    const WGPUTextureViewDescriptor view_desc = {
        .nextInChain = nullptr,
        .label = { .data = "TextureView", .length = WGPU_STRLEN },
        .format = format,
        .dimension = dimension,
        .baseMipLevel = desc.base_mip,
        .mipLevelCount = desc.mip_count ? desc.mip_count :
            (texture->desc.mip_levels > desc.base_mip ? (texture->desc.mip_levels - desc.base_mip) : 1),
        .baseArrayLayer = desc.base_layer,
        .arrayLayerCount = desc.layer_count ? desc.layer_count :
            (texture->desc.layer_count > desc.base_layer ? (texture->desc.layer_count - desc.base_layer) : 1),
        .aspect = aspect,
        .usage = usage,
    };

    return wgpuTextureCreateView(texture->texture, &view_desc);
}

void destroy_texture_view(WGPUTextureView view) noexcept
{
    if (view) wgpuTextureViewRelease(view);
}

WGPUSampler create_sampler(Device* device, const SamplerDesc& desc) noexcept
{
    if (!device || !device->device) return nullptr;

    const bool aniso = desc.anisotropic;
    const WGPUFilterMode min_f = aniso ? WGPUFilterMode_Linear : to_wgpu_filter(desc.min_filter);
    const WGPUFilterMode mag_f = aniso ? WGPUFilterMode_Linear : to_wgpu_filter(desc.mag_filter);
    const WGPUMipmapFilterMode mip_f = aniso ? WGPUMipmapFilterMode_Linear : to_wgpu_mip_filter(desc.mip_filter);

    const WGPUSamplerDescriptor s_desc = {
        .nextInChain = nullptr,
        .label = { .data = "Sampler", .length = WGPU_STRLEN },
        .addressModeU = to_wgpu_address_mode(desc.address_u),
        .addressModeV = to_wgpu_address_mode(desc.address_v),
        .addressModeW = to_wgpu_address_mode(desc.address_w),
        .magFilter = mag_f,
        .minFilter = min_f,
        .mipmapFilter = mip_f,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .compare = desc.compare_enabled ? to_wgpu_compare(desc.compare) : WGPUCompareFunction_Undefined,
        .maxAnisotropy = static_cast<uint16>(aniso ? 4 : 1),
    };

    return wgpuDeviceCreateSampler(device->device, &s_desc);
}

void destroy_sampler(WGPUSampler sampler) noexcept
{
    if (sampler) wgpuSamplerRelease(sampler);
}

RenderView* create_render_view(Texture* texture, const RenderViewDesc& desc) noexcept
{
    if (!texture || !texture->texture) return nullptr;

    const WGPUTextureViewDescriptor view_desc = {
        .nextInChain = nullptr,
        .label = { .data = "RenderView", .length = WGPU_STRLEN },
        .format = to_wgpu_format(texture->desc.format),
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = desc.mip_level,
        .mipLevelCount = 1,
        .baseArrayLayer = desc.slice,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All,
        .usage = WGPUTextureUsage_RenderAttachment,
    };
    const WGPUTextureView view = wgpuTextureCreateView(texture->texture, &view_desc);
    if (!view) return nullptr;

    RenderView* rv = new RenderView{};
    rv->view = view;
    rv->texture = texture->texture;
    rv->format = texture->desc.format;
    return rv;
}

void destroy_render_view(RenderView* render_view) noexcept
{
    if (!render_view) return;
    if (render_view->view) wgpuTextureViewRelease(render_view->view);
    delete render_view;
}

TextureDescriptorHeap* create_texture_descriptor_heap(Device* device, uint32 capacity) noexcept
{
    assert(device && capacity > 0);
    assert(capacity <= 32768);
    TextureDescriptorHeap* heap = new TextureDescriptorHeap{};
    heap->device = device;
    heap->capacity = capacity;
    heap->entries = new TextureDescriptorEntry[capacity]{};
    return heap;
}

void destroy_texture_descriptor_heap(TextureDescriptorHeap* heap) noexcept
{
    if (!heap) return;
    evict_resource_tables_for_heap(heap->device, heap, nullptr);
    if (heap->entries)
    {
        for (uint32 i = 0; i < heap->capacity; ++i)
        {
            if (heap->entries[i].view)
            {
                destroy_texture_view(heap->entries[i].view);
                heap->entries[i].view = nullptr;
            }
        }
        delete[] heap->entries;
    }
    delete heap;
}

void write_texture_descriptor(TextureDescriptorHeap* heap, uint32 index, const Texture* texture, TextureDescriptorType type,
                              const TextureDescriptorDesc& desc) noexcept
{
    assert(heap && heap->device);
    assert(index < heap->capacity);
    if (!texture || !texture->texture) return;

    const WGPUTextureView view = create_texture_view(texture, type, desc);
    if (!view) return;

    if (heap->entries[index].view)
    {
        destroy_texture_view(heap->entries[index].view);
    }

    heap->entries[index].view = view;
    heap->entries[index].texture = texture;
    heap->entries[index].type = type;
    heap->entries[index].desc = desc;
    __atomic_add_fetch(&heap->generation, 1ull, __ATOMIC_RELEASE);
}

void copy_texture_descriptors(const TextureDescriptorHeap* source, uint32 source_index, TextureDescriptorHeap* destination,
                              uint32 destination_index, uint32 count) noexcept
{
    assert(source && destination);
    assert(source_index + count <= source->capacity);
    assert(destination_index + count <= destination->capacity);
    if (count == 0 || (source == destination && source_index == destination_index)) return;

    const bool backward = (source == destination && destination_index > source_index);
    for (uint32 step = 0; step < count; ++step)
    {
        const uint32 i = backward ? (count - 1 - step) : step;
        const TextureDescriptorEntry src_entry = source->entries[source_index + i];
        if (src_entry.view)
        {
            wgpuTextureViewAddRef(src_entry.view);
        }
        TextureDescriptorEntry& dst_entry = destination->entries[destination_index + i];
        if (dst_entry.view)
        {
            destroy_texture_view(dst_entry.view);
        }
        dst_entry = src_entry;
    }
    __atomic_add_fetch(&destination->generation, 1ull, __ATOMIC_RELEASE);
}

SamplerDescriptorHeap* create_sampler_descriptor_heap(Device* device, uint32 capacity) noexcept
{
    assert(device && capacity > 0);
    assert(capacity <= 32768);
    SamplerDescriptorHeap* heap = new SamplerDescriptorHeap{};
    heap->device = device;
    heap->capacity = capacity;
    heap->entries = new SamplerDescriptorEntry[capacity]{};
    return heap;
}

void destroy_sampler_descriptor_heap(SamplerDescriptorHeap* heap) noexcept
{
    if (!heap) return;
    evict_resource_tables_for_heap(heap->device, nullptr, heap);
    if (heap->entries)
    {
        for (uint32 i = 0; i < heap->capacity; ++i)
        {
            if (heap->entries[i].sampler)
            {
                destroy_sampler(heap->entries[i].sampler);
                heap->entries[i].sampler = nullptr;
            }
        }
        delete[] heap->entries;
    }
    delete heap;
}

void write_sampler_descriptor(SamplerDescriptorHeap* heap, uint32 index, const SamplerDesc& desc) noexcept
{
    assert(heap && heap->device);
    assert(index < heap->capacity);

    const WGPUSampler sampler = create_sampler(heap->device, desc);
    if (!sampler) return;

    if (heap->entries[index].sampler)
    {
        destroy_sampler(heap->entries[index].sampler);
    }

    heap->entries[index].sampler = sampler;
    heap->entries[index].desc = desc;
    __atomic_add_fetch(&heap->generation, 1ull, __ATOMIC_RELEASE);
}

void copy_sampler_descriptors(const SamplerDescriptorHeap* source, uint32 source_index, SamplerDescriptorHeap* destination,
                              uint32 destination_index, uint32 count) noexcept
{
    assert(source && destination);
    assert(source_index + count <= source->capacity);
    assert(destination_index + count <= destination->capacity);
    if (count == 0 || (source == destination && source_index == destination_index)) return;

    const bool backward = (source == destination && destination_index > source_index);
    for (uint32 step = 0; step < count; ++step)
    {
        const uint32 i = backward ? (count - 1 - step) : step;
        const SamplerDescriptorEntry src_entry = source->entries[source_index + i];
        if (src_entry.sampler)
        {
            wgpuSamplerAddRef(src_entry.sampler);
        }
        SamplerDescriptorEntry& dst_entry = destination->entries[destination_index + i];
        if (dst_entry.sampler)
        {
            destroy_sampler(dst_entry.sampler);
        }
        dst_entry = src_entry;
    }
    __atomic_add_fetch(&destination->generation, 1ull, __ATOMIC_RELEASE);
}

} // namespace gpu
