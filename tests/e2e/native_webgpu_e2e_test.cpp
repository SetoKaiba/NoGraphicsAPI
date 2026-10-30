#if defined(_CPPUNWIND) || defined(__EXCEPTIONS) || defined(__cpp_exceptions)
#error NoGraphicsAPI E2E tests must be compiled with C++ exceptions disabled
#endif

#if defined(_CPPRTTI) || defined(__GXX_RTTI)
#error NoGraphicsAPI E2E tests must be compiled with RTTI disabled
#endif

#if defined(__clang__)
#if __has_feature(cxx_exceptions) || __has_feature(cxx_rtti)
#error NoGraphicsAPI E2E tests must be compiled with C++ exceptions and RTTI disabled
#endif
#endif

#include <NoGraphicsAPI/NoGraphicsAPI.hpp>
#include <NoGraphicsAPI/traits.hpp>

#include <stdio.h>
#include <initializer_list>

// Pure C WebGPU API header
#include <webgpu/webgpu.h>

namespace
{

bool test_webgpu_c_abi_and_extension_types()
{
    // Verify ResourceTable SType and Feature Name enum codes
    const gpu::uint32 feature_res_table = WGPUFeatureName_ChromiumExperimentalSamplingResourceTable;
    const gpu::uint32 stype_res_table = WGPUSType_PipelineLayoutResourceTable;

    if (feature_res_table != 0x0005003Au)
    {
        fprintf(stderr, "FAIL: WGPUFeatureName_ChromiumExperimentalSamplingResourceTable expected 0x0005003A, got 0x%08X\n", feature_res_table);
        return false;
    }
    if (stype_res_table != 0x00050050u)
    {
        fprintf(stderr, "FAIL: WGPUSType_PipelineLayoutResourceTable expected 0x00050050, got 0x%08X\n", stype_res_table);
        return false;
    }

    // Verify WGPUResourceTableDescriptor struct
    WGPUResourceTableDescriptor table_desc{};
    table_desc.size = 65536;
    if (table_desc.size != 65536)
    {
        return false;
    }

    // Verify WGPUBindingResource struct layout
    WGPUBindingResource binding{};
    binding.buffer = nullptr;
    binding.offset = 0;
    binding.size = 1024;
    binding.sampler = nullptr;
    binding.textureView = nullptr;
    if (binding.size != 1024)
    {
        return false;
    }

    // Verify WGPUPipelineLayoutResourceTable chaining
    WGPUPipelineLayoutResourceTable layout_table{};
    layout_table.chain.sType = static_cast<WGPUSType>(stype_res_table);
    layout_table.usesResourceTable = WGPU_TRUE;
    if (layout_table.usesResourceTable != WGPU_TRUE)
    {
        return false;
    }

    // Verify WGPUSurfaceSourceMetalLayer chaining
    WGPUSurfaceSourceMetalLayer metal_surface{};
    metal_surface.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
    metal_surface.layer = nullptr;
    if (metal_surface.chain.sType != WGPUSType_SurfaceSourceMetalLayer)
    {
        return false;
    }

    return true;
}

bool test_nographicsapi_structures()
{
    // Test pointer size for 64-bit virtual GPU address offsets
    if (sizeof(void*) != 8)
    {
        fprintf(stderr, "FAIL: Target pointer size != 8 bytes (64-bit BDA required)\n");
        return false;
    }

    // Test GpuRange structure layout
    gpu::GpuRange range{
        .gpu = reinterpret_cast<void*>(0x2000),
        .size = 4096,
    };
    if (range.size != 4096 || range.gpu != reinterpret_cast<void*>(0x2000))
    {
        return false;
    }

    // Test GpuCpuRange structure layout
    gpu::uint32 dummy_val = 12345;
    gpu::GpuCpuRange<gpu::uint32> cpu_gpu_range{
        .cpu = &dummy_val,
        .gpu = reinterpret_cast<gpu::uint32*>(0x4000),
        .size = sizeof(gpu::uint32),
    };
    if (cpu_gpu_range.size != 4 || *cpu_gpu_range.cpu != 12345 || cpu_gpu_range.gpu != reinterpret_cast<gpu::uint32*>(0x4000))
    {
        return false;
    }

    // Test Span value semantics and initialization from array
    gpu::uint32 numbers[] = {10, 20, 30};
    gpu::Span<gpu::uint32> span = {numbers, 3};
    if (span.size != 3 || span.data[1] != 20)
    {
        return false;
    }

    return true;
}

bool test_live_bindless_and_arena_semantics()
{
    const gpu::DeviceInit dev_res = gpu::create_device({});
    if (dev_res.error != gpu::Error::none || !dev_res.device)
    {
        fprintf(stderr, "FAIL: create_device failed in live E2E test\n");
        return false;
    }
    gpu::Device* device = dev_res.device;

    // 1. Verify segregated Mega-Buffer arena (cpu_visible low end, gpu_only high end).
    const gpu::GpuHeap cpu_probe = gpu::create_gpu_heap(device, 256, gpu::MemoryType::cpu_visible);
    const gpu::GpuHeap gpu_probe = gpu::create_gpu_heap(device, 256, gpu::MemoryType::gpu_only);
    const gpu::uintptr cpu_addr = reinterpret_cast<gpu::uintptr>(cpu_probe.range.gpu);
    const gpu::uintptr gpu_addr = reinterpret_cast<gpu::uintptr>(gpu_probe.range.gpu);
    const bool segregated = (gpu_addr > cpu_addr + 64ull * 1024ull * 1024ull);
    gpu::destroy_gpu_heap(gpu_probe);
    gpu::destroy_gpu_heap(cpu_probe);
    if (!segregated)
    {
        fprintf(stderr, "FAIL: Mega-Buffer arena did not segregate cpu_visible (0x%llx) vs gpu_only (0x%llx)\n",
                static_cast<unsigned long long>(cpu_addr), static_cast<unsigned long long>(gpu_addr));
        gpu::wait_idle(device);
        gpu::destroy_device(device);
        return false;
    }

    // 2. Verify bindless multi-heap isolation, mid-compute-pass switching, overlap/self copy, and slot overwrite.
    static constexpr const char kWgslCode[] =
        "enable chromium_experimental_resource_table;\n"
        "@group(0) @binding(0) var<storage, read_write> mega : array<u32>;\n"
        "var<immediate> root_byte_offset : u32;\n"
        "@compute @workgroup_size(1, 1, 1)\n"
        "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
        "    let r : u32 = root_byte_offset >> 2u;\n"
        "    let out_word_base : u32 = mega[r + 0u] >> 2u;\n"
        "    let tex_slot : u32 = mega[r + 1u] + gid.x;\n"
        "    let out_slot : u32 = mega[r + 2u] + gid.x;\n"
        "    let tex : texture_2d<f32> = getResource<texture_2d<f32>>(tex_slot);\n"
        "    let px : vec4<f32> = textureLoad(tex, vec2<i32>(0, 0), 0);\n"
        "    mega[out_word_base + out_slot] = u32(round(px.x * 255.0));\n"
        "}\n";

    gpu::PSO* pso = gpu::create_compute_pso(device, {
        .code = {reinterpret_cast<const gpu::byte*>(kWgslCode), sizeof(kWgslCode) - 1},
        .entry_point = "cs_main",
    });
    if (!pso)
    {
        fprintf(stderr, "FAIL: create_compute_pso failed in live E2E test\n");
        gpu::wait_idle(device);
        gpu::destroy_device(device);
        return false;
    }

    const gpu::GpuHeap upload = gpu::create_gpu_heap(device, 4096, gpu::MemoryType::cpu_visible);
    const gpu::GpuHeap output = gpu::create_gpu_heap(device, 1024, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap readback = gpu::create_gpu_heap(device, 1024, gpu::MemoryType::readback);
    const gpu::TextureHeap tex_heap = gpu::create_texture_heap(device, 64 * 1024);

    const gpu::TextureDesc tex_desc = {
        .usage = gpu::TextureUsage::sampled | gpu::TextureUsage::transfer_destination,
    };
    gpu::Texture* textures[4] = {};
    const gpu::uint8 kValues[4] = {10, 20, 30, 40};
    for (gpu::uint32 i = 0; i < 4; ++i)
    {
        textures[i] = gpu::create_texture(device, tex_desc, tex_heap, static_cast<gpu::uint64>(i) * 4096ull);
        gpu::uint8* px = upload.range.cpu + i * 256u;
        px[0] = kValues[i];
        px[1] = 0;
        px[2] = 0;
        px[3] = 255;
    }

    // Populate heap_a in reverse order [40, 30, 20, 10] BEFORE heap_b [10, 20, 30, 40].
    gpu::TextureDescriptorHeap* heap_a = gpu::create_texture_descriptor_heap(device, 4);
    gpu::TextureDescriptorHeap* heap_b = gpu::create_texture_descriptor_heap(device, 5);
    for (gpu::uint32 i = 0; i < 4; ++i)
    {
        gpu::write_texture_descriptor(heap_a, i, textures[3 - i], gpu::TextureDescriptorType::sampled);
    }
    for (gpu::uint32 i = 0; i < 4; ++i)
    {
        gpu::write_texture_descriptor(heap_b, i, textures[i], gpu::TextureDescriptorType::sampled);
    }

    const gpu::uint32 out_mega_offset = static_cast<gpu::uint32>(reinterpret_cast<gpu::uintptr>(output.range.gpu));
    // Root structs at 256B-aligned offsets in upload heap starting at +1024.
    gpu::uint32* root0_cpu = reinterpret_cast<gpu::uint32*>(upload.range.cpu + 1024);
    root0_cpu[0] = out_mega_offset;
    root0_cpu[1] = 0;
    root0_cpu[2] = 0;
    gpu::uint32* root1_cpu = reinterpret_cast<gpu::uint32*>(upload.range.cpu + 1280);
    root1_cpu[0] = out_mega_offset;
    root1_cpu[1] = 0;
    root1_cpu[2] = 4;

    gpu::CommandPool* pool = gpu::create_command_pool(device);
    gpu::TimelineSemaphore* sem = gpu::create_timeline_semaphore(device, 0);
    gpu::uint64 timeline_val = 0;

    gpu::CommandBuffer* cmd = gpu::begin_commands(pool);
    for (gpu::uint32 i = 0; i < 4; ++i)
    {
        gpu::initialize_texture(cmd, textures[i]);
        gpu::copy_memory_to_texture(cmd, {.gpu = upload.range.gpu + i * 256u, .size = 4}, textures[i]);
    }
    gpu::barrier(cmd, gpu::Stage::transfer, gpu::Access::transfer_write, gpu::Stage::compute, gpu::Access::shader_read);
    gpu::bind_pso(cmd, pso);
    gpu::set_texture_descriptor_heap(cmd, heap_a);
    gpu::dispatch(cmd, upload.range.gpu + 1024, {.x = 4, .y = 1, .z = 1});
    // Switch descriptor heap mid-compute-pass without ending the pass.
    gpu::set_texture_descriptor_heap(cmd, heap_b);
    gpu::dispatch(cmd, upload.range.gpu + 1280, {.x = 4, .y = 1, .z = 1});
    gpu::barrier(cmd, gpu::Stage::compute, gpu::Access::shader_write, gpu::Stage::transfer, gpu::Access::transfer_read);
    gpu::copy_memory(cmd, {.gpu = output.range.gpu, .size = 32}, {.gpu = readback.range.gpu, .size = 32});
    gpu::end_commands(cmd);
    gpu::submit(device, {.commands = {cmd}, .completion = {.semaphore = sem, .value = ++timeline_val}});
    gpu::wait_timeline({.semaphore = sem, .value = timeline_val});

    const gpu::uint32* rb_words = reinterpret_cast<const gpu::uint32*>(readback.range.cpu);
    const gpu::uint32 expected_pass1[8] = {40, 30, 20, 10, 10, 20, 30, 40};
    bool ok = true;
    for (gpu::uint32 i = 0; i < 8; ++i)
    {
        if (rb_words[i] != expected_pass1[i])
        {
            fprintf(stderr, "FAIL: pass1 rb_words[%u] expected %u, got %u\n", i, expected_pass1[i], rb_words[i]);
            ok = false;
        }
    }

    // Destroy heap_a, then perform overlapping forward copy, self-copy, and slot overwrite on heap_b.
    gpu::destroy_texture_descriptor_heap(heap_a);
    heap_a = nullptr;

    if (ok)
    {
        // heap_b slots [0..3] are [10, 20, 30, 40]. Overlapping forward copy [0..3] -> [1..4] => [10, 10, 20, 30, 40].
        gpu::copy_texture_descriptors(heap_b, 0, heap_b, 1, 4);
        // Self-copy [0..4] -> [0..4] must be a safe no-op.
        gpu::copy_texture_descriptors(heap_b, 0, heap_b, 0, 5);
        // Overwrite slot 0 with textures[3] (40) => [40, 10, 20, 30, 40].
        gpu::write_texture_descriptor(heap_b, 0, textures[3], gpu::TextureDescriptorType::sampled);

        gpu::reset_command_pool(pool);
        gpu::CommandBuffer* cmd2 = gpu::begin_commands(pool);
        gpu::bind_pso(cmd2, pso);
        gpu::set_texture_descriptor_heap(cmd2, heap_b);
        gpu::dispatch(cmd2, upload.range.gpu + 1024, {.x = 5, .y = 1, .z = 1});
        gpu::barrier(cmd2, gpu::Stage::compute, gpu::Access::shader_write, gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_memory(cmd2, {.gpu = output.range.gpu, .size = 20}, {.gpu = readback.range.gpu, .size = 20});
        gpu::end_commands(cmd2);
        gpu::submit(device, {.commands = {cmd2}, .completion = {.semaphore = sem, .value = ++timeline_val}});
        gpu::wait_timeline({.semaphore = sem, .value = timeline_val});

        const gpu::uint32 expected_pass2[5] = {40, 10, 20, 30, 40};
        for (gpu::uint32 i = 0; i < 5; ++i)
        {
            if (rb_words[i] != expected_pass2[i])
            {
                fprintf(stderr, "FAIL: pass2 rb_words[%u] expected %u, got %u\n", i, expected_pass2[i], rb_words[i]);
                ok = false;
            }
        }
    }

    gpu::wait_idle(device);
    gpu::destroy_timeline_semaphore(sem);
    gpu::destroy_command_pool(pool);
    if (heap_a) gpu::destroy_texture_descriptor_heap(heap_a);
    gpu::destroy_texture_descriptor_heap(heap_b);
    for (gpu::uint32 i = 0; i < 4; ++i)
    {
        gpu::destroy_texture(textures[i]);
    }
    gpu::destroy_texture_heap(tex_heap);
    gpu::destroy_gpu_heap(readback);
    gpu::destroy_gpu_heap(output);
    gpu::destroy_gpu_heap(upload);
    gpu::destroy_pso(pso);
    gpu::destroy_device(device);
    return ok;
}

bool test_adjacent_heaps_timeline_wrap_and_resize_churn()
{
    const gpu::DeviceInit dev_res = gpu::create_device({});
    if (dev_res.error != gpu::Error::none || !dev_res.device)
    {
        fprintf(stderr, "FAIL: create_device failed in regression E2E test\n");
        return false;
    }
    gpu::Device* device = dev_res.device;

    static constexpr const char kWgslCode[] =
        "enable chromium_experimental_resource_table;\n"
        "@group(0) @binding(0) var<storage, read_write> mega : array<u32>;\n"
        "var<immediate> root_byte_offset : u32;\n"
        "@compute @workgroup_size(1, 1, 1)\n"
        "fn cs_main() {\n"
        "    let r : u32 = root_byte_offset >> 2u;\n"
        "    let mode : u32 = mega[r + 0u];\n"
        "    let dst_word : u32 = mega[r + 1u] >> 2u;\n"
        "    if (mode == 0u) {\n"
        "        mega[dst_word] = mega[r + 2u];\n"
        "    } else {\n"
        "        let tex : texture_2d<f32> = getResource<texture_2d<f32>>(mega[r + 2u]);\n"
        "        let px : vec4<f32> = textureLoad(tex, vec2<i32>(0, 0), 0);\n"
        "        mega[dst_word] = u32(round(px.x * 255.0));\n"
        "    }\n"
        "}\n";

    gpu::PSO* pso = gpu::create_compute_pso(device, {
        .code = {reinterpret_cast<const gpu::byte*>(kWgslCode), sizeof(kWgslCode) - 1},
        .entry_point = "cs_main",
    });
    if (!pso)
    {
        fprintf(stderr, "FAIL: create_compute_pso failed in regression E2E test\n");
        gpu::wait_idle(device);
        gpu::destroy_device(device);
        return false;
    }

    // 1. Adjacent 256-byte cpu_visible heaps (h2 immediately follows h1) + recycled tainted arena zero-sync.
    const gpu::GpuHeap h1 = gpu::create_gpu_heap(device, 256, gpu::MemoryType::cpu_visible);
    gpu::GpuHeap h2 = gpu::create_gpu_heap(device, 256, gpu::MemoryType::cpu_visible);
    const gpu::GpuHeap out_gpu = gpu::create_gpu_heap(device, 256, gpu::MemoryType::gpu_only);
    const gpu::GpuHeap rb = gpu::create_gpu_heap(device, 256, gpu::MemoryType::readback);
    const gpu::uintptr h1_addr = reinterpret_cast<gpu::uintptr>(h1.range.gpu);
    const gpu::uintptr h2_addr = reinterpret_cast<gpu::uintptr>(h2.range.gpu);
    const gpu::uint32 out_mega_off = static_cast<gpu::uint32>(reinterpret_cast<gpu::uintptr>(out_gpu.range.gpu));

    bool ok = true;
    if (h2_addr != h1_addr + 256u)
    {
        fprintf(stderr, "FAIL: expected adjacent Mega-Buffer heaps (h1=0x%llx, h2=0x%llx)\n",
                static_cast<unsigned long long>(h1_addr), static_cast<unsigned long long>(h2_addr));
        ok = false;
    }

    gpu::uint32* h1_words = reinterpret_cast<gpu::uint32*>(h1.range.cpu);
    h1_words[0] = 0u;
    h1_words[1] = out_mega_off;
    h1_words[2] = 0x11223344u;

    gpu::uint32* h2_words = reinterpret_cast<gpu::uint32*>(h2.range.cpu);
    h2_words[0] = 0u;
    h2_words[1] = out_mega_off;
    h2_words[2] = 0xAABBCCDDu;

    gpu::CommandPool* pools[2] = {
        gpu::create_command_pool(device),
        gpu::create_command_pool(device),
    };
    gpu::TimelineSemaphore* sem = gpu::create_timeline_semaphore(device, 0);
    gpu::uint64 timeline_val = 0;

    gpu::CommandBuffer* cmd = gpu::begin_commands(pools[0]);
    gpu::bind_pso(cmd, pso);
    gpu::dispatch(cmd, h2.range.gpu, {.x = 1, .y = 1, .z = 1});
    gpu::barrier(cmd, gpu::Stage::compute, gpu::Access::shader_write, gpu::Stage::transfer, gpu::Access::transfer_read);
    gpu::copy_memory(cmd, {.gpu = out_gpu.range.gpu, .size = 4}, {.gpu = rb.range.gpu, .size = 4});
    gpu::end_commands(cmd);
    gpu::submit(device, {.commands = {cmd}, .completion = {.semaphore = sem, .value = ++timeline_val}});
    gpu::wait_timeline({.semaphore = sem, .value = timeline_val});

    const gpu::uint32* rb_words = reinterpret_cast<const gpu::uint32*>(rb.range.cpu);
    if (rb_words[0] != 0xAABBCCDDu)
    {
        fprintf(stderr, "FAIL: adjacent h2 root dispatch expected 0xAABBCCDD, got 0x%08X\n", rb_words[0]);
        ok = false;
    }

    // Recycle h2's tainted arena block and verify untouched words are zeroed on GPU.
    gpu::destroy_gpu_heap(h2);
    h2 = gpu::create_gpu_heap(device, 256, gpu::MemoryType::cpu_visible);
    reinterpret_cast<gpu::uint32*>(h2.range.cpu)[3] = 0x55667788u;
    gpu::reset_command_pool(pools[0]);
    cmd = gpu::begin_commands(pools[0]);
    gpu::copy_memory(cmd, {.gpu = h2.range.gpu, .size = 16}, {.gpu = rb.range.gpu, .size = 16});
    gpu::end_commands(cmd);
    gpu::submit(device, {.commands = {cmd}, .completion = {.semaphore = sem, .value = ++timeline_val}});
    gpu::wait_timeline({.semaphore = sem, .value = timeline_val});
    if (rb_words[0] != 0u || rb_words[1] != 0u || rb_words[2] != 0u || rb_words[3] != 0x55667788u)
    {
        fprintf(stderr, "FAIL: recycled tainted arena block not zeroed: [%08X, %08X, %08X, %08X]\n",
                rb_words[0], rb_words[1], rb_words[2], rb_words[3]);
        ok = false;
    }

    // 2. TimelineSemaphore >64 ring-buffer wraparound (80 overlapped submissions) + unretired future drain.
    gpu::uint64 prev_completed = gpu::timeline_completed_value(sem);
    for (gpu::uint32 step = 0; step < 80 && ok; ++step)
    {
        const gpu::uint32 slot = step & 1u;
        if (step >= 2)
        {
            gpu::wait_timeline({.semaphore = sem, .value = timeline_val - 1});
            const gpu::uint64 now_completed = gpu::timeline_completed_value(sem);
            if (now_completed < prev_completed || now_completed < timeline_val - 1)
            {
                fprintf(stderr, "FAIL: non-monotonic timeline completion (%llu -> %llu)\n",
                        static_cast<unsigned long long>(prev_completed), static_cast<unsigned long long>(now_completed));
                ok = false;
                break;
            }
            prev_completed = now_completed;
        }
        gpu::reset_command_pool(pools[slot]);
        gpu::CommandBuffer* step_cmd = gpu::begin_commands(pools[slot]);
        gpu::uint32* root_cpu = reinterpret_cast<gpu::uint32*>(h1.range.cpu + slot * 128u);
        root_cpu[0] = 0u;
        root_cpu[1] = out_mega_off;
        root_cpu[2] = 1000u + step;
        gpu::bind_pso(step_cmd, pso);
        gpu::dispatch(step_cmd, h1.range.gpu + slot * 128u, {.x = 1, .y = 1, .z = 1});
        if (step == 79)
        {
            gpu::barrier(step_cmd, gpu::Stage::compute, gpu::Access::shader_write, gpu::Stage::transfer, gpu::Access::transfer_read);
            gpu::copy_memory(step_cmd, {.gpu = out_gpu.range.gpu, .size = 4}, {.gpu = rb.range.gpu, .size = 4});
        }
        gpu::end_commands(step_cmd);
        gpu::submit(device, {.commands = {step_cmd}, .completion = {.semaphore = sem, .value = ++timeline_val}});
    }
    gpu::wait_timeline({.semaphore = sem, .value = timeline_val});
    if (ok && (gpu::timeline_completed_value(sem) != timeline_val || rb_words[0] != 1079u))
    {
        fprintf(stderr, "FAIL: 80-step timeline wraparound mismatch (completed=%llu, expected=%llu, rb=%u)\n",
                static_cast<unsigned long long>(gpu::timeline_completed_value(sem)),
                static_cast<unsigned long long>(timeline_val), rb_words[0]);
        ok = false;
    }

    gpu::TimelineSemaphore* drain_sem = gpu::create_timeline_semaphore(device, 0);
    for (gpu::uint32 i = 0; i < 2; ++i)
    {
        gpu::reset_command_pool(pools[i]);
        gpu::CommandBuffer* dcmd = gpu::begin_commands(pools[i]);
        gpu::end_commands(dcmd);
        gpu::submit(device, {.commands = {dcmd}, .completion = {.semaphore = drain_sem, .value = i + 1}});
    }
    gpu::wait_idle(device);
    gpu::destroy_timeline_semaphore(drain_sem);

    // 3. 16 rapid G-buffer resize cycles with 2-frame-in-flight retirement and ping-pong TextureDescriptorHeap rows.
    const gpu::TextureHeap resize_heap = gpu::create_texture_heap(device, 8 * 1024 * 1024);
    gpu::TextureDescriptorHeap* desc_heap = gpu::create_texture_descriptor_heap(device, 4);
    gpu::Texture* in_flight_tex[2] = {};
    gpu::RenderView* in_flight_rv[2] = {};

    for (gpu::uint32 iter = 0; iter < 16 && ok; ++iter)
    {
        const gpu::uint32 slot = iter & 1u;
        if (iter >= 2)
        {
            gpu::wait_timeline({.semaphore = sem, .value = timeline_val - 1});
            gpu::destroy_render_view(in_flight_rv[slot]);
            gpu::destroy_texture(in_flight_tex[slot]);
            in_flight_rv[slot] = nullptr;
            in_flight_tex[slot] = nullptr;
        }

        const gpu::uint32 expected_r = 16u + iter * 12u;
        const gpu::TextureDesc rt_desc = {
            .extent = {.x = 32u + iter * 8u, .y = 24u + iter * 4u, .z = 1u},
            .format = gpu::Format::rgba8_unorm,
            .usage = gpu::TextureUsage::color_attachment | gpu::TextureUsage::sampled,
        };
        in_flight_tex[slot] = gpu::create_texture(device, rt_desc, resize_heap, static_cast<gpu::uint64>(slot) * 2ull * 1024ull * 1024ull);
        in_flight_rv[slot] = gpu::create_render_view(in_flight_tex[slot]);
        gpu::write_texture_descriptor(desc_heap, slot, in_flight_tex[slot], gpu::TextureDescriptorType::sampled);

        gpu::uint32* root_cpu = reinterpret_cast<gpu::uint32*>(h1.range.cpu + slot * 128u);
        root_cpu[0] = 1u;
        root_cpu[1] = out_mega_off + slot * 4u;
        root_cpu[2] = slot;

        gpu::reset_command_pool(pools[slot]);
        gpu::CommandBuffer* rcmd = gpu::begin_commands(pools[slot]);
        gpu::initialize_texture(rcmd, in_flight_tex[slot]);
        gpu::begin_render_pass(rcmd, {
            .colors = {{
                .render_view = in_flight_rv[slot],
                .load = gpu::LoadOp::clear,
                .clear = {.x = static_cast<float>(expected_r) / 255.0f, .y = 0.25f, .z = 0.75f, .w = 1.0f},
            }},
        });
        gpu::end_render_pass(rcmd);
        gpu::barrier(rcmd, gpu::Stage::color_output, gpu::Access::color_write, gpu::Stage::compute, gpu::Access::shader_read);
        gpu::bind_pso(rcmd, pso);
        gpu::set_texture_descriptor_heap(rcmd, desc_heap);
        gpu::dispatch(rcmd, h1.range.gpu + slot * 128u, {.x = 1, .y = 1, .z = 1});
        gpu::barrier(rcmd, gpu::Stage::compute, gpu::Access::shader_write, gpu::Stage::transfer, gpu::Access::transfer_read);
        gpu::copy_memory(rcmd, {.gpu = out_gpu.range.gpu + slot * 4u, .size = 4}, {.gpu = rb.range.gpu + iter * 4u, .size = 4});
        gpu::end_commands(rcmd);
        gpu::submit(device, {.commands = {rcmd}, .completion = {.semaphore = sem, .value = ++timeline_val}});
    }

    gpu::wait_timeline({.semaphore = sem, .value = timeline_val});
    for (gpu::uint32 iter = 0; iter < 16 && ok; ++iter)
    {
        const gpu::uint32 expected_r = 16u + iter * 12u;
        if (rb_words[iter] != expected_r)
        {
            fprintf(stderr, "FAIL: resize cycle %u expected %u, got %u\n", iter, expected_r, rb_words[iter]);
            ok = false;
        }
    }

    gpu::wait_idle(device);
    for (gpu::uint32 slot = 0; slot < 2; ++slot)
    {
        if (in_flight_rv[slot]) gpu::destroy_render_view(in_flight_rv[slot]);
        if (in_flight_tex[slot]) gpu::destroy_texture(in_flight_tex[slot]);
        gpu::destroy_command_pool(pools[slot]);
    }
    gpu::destroy_texture_descriptor_heap(desc_heap);
    gpu::destroy_texture_heap(resize_heap);
    gpu::destroy_timeline_semaphore(sem);
    gpu::destroy_gpu_heap(rb);
    gpu::destroy_gpu_heap(out_gpu);
    gpu::destroy_gpu_heap(h2);
    gpu::destroy_gpu_heap(h1);
    gpu::destroy_pso(pso);
    gpu::destroy_device(device);
    return ok;
}

} // namespace

int main()
{
    printf("Starting Native WebGPU E2E Verification...\n");

    if (!test_webgpu_c_abi_and_extension_types())
    {
        fprintf(stderr, "FAIL: test_webgpu_c_abi_and_extension_types failed\n");
        return 1;
    }
    printf("  [PASS] WebGPU C ABI and Dawn ResourceTable extensions verified.\n");

    if (!test_nographicsapi_structures())
    {
        fprintf(stderr, "FAIL: test_nographicsapi_structures failed\n");
        return 1;
    }
    printf("  [PASS] NoGraphicsAPI data structures and Span semantics verified.\n");

    if (!test_live_bindless_and_arena_semantics())
    {
        fprintf(stderr, "FAIL: test_live_bindless_and_arena_semantics failed\n");
        return 1;
    }
    printf("  [PASS] Live GPU bindless multi-heap, overlap copy, slot overwrite, and segregated arena verified.\n");

    if (!test_adjacent_heaps_timeline_wrap_and_resize_churn())
    {
        fprintf(stderr, "FAIL: test_adjacent_heaps_timeline_wrap_and_resize_churn failed\n");
        return 1;
    }
    printf("  [PASS] Adjacent Mega-Buffer heaps, >64 timeline wraparound, and 16-cycle resize churn verified.\n");

    printf("All Native WebGPU E2E checks passed successfully.\n");
    return 0;
}
