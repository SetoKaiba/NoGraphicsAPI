#include "deferred_lighting_shared.h"
#include "example_support.hpp"
#include "gbuffer_shared.h"
#include "simulation_shared.h"

#include <NoGraphicsAPIUtility/bump_allocator.hpp>
#include <NoGraphicsAPIUtility/delete_queue.hpp>
#include <NoGraphicsAPIUtility/math.hpp>
#include <NoGraphicsAPIUtility/texture_allocator.hpp>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

using namespace gpu;

namespace
{

constexpr uint32 initial_width = 1280;
constexpr uint32 initial_height = 720;
constexpr uint32 object_count = object_grid_width * object_grid_width;
constexpr uint32 frames_in_flight = 2;
constexpr uint32 gbuffer_texture_count = uint32(GBufferTexture::count);
constexpr uint64 data_heap_size = 16 * 1024 * 1024;
constexpr uint64 texture_heap_size = 256 * 1024 * 1024;
constexpr float cube_scale = 0.42f;
constexpr float cube_rotation_speed = 0.5f;
constexpr float minimum_orbit_radius = 54.0f;
constexpr float maximum_orbit_radius = 240.0f;
constexpr float3 camera_position{
    .x = 260.0f,
    .y = 210.0f,
    .z = 300.0f,
};

struct GBuffer
{
    PlacedTexture albedo{};
    PlacedTexture normal_roughness{};
    PlacedTexture depth{};
    RenderView* albedo_render_view = nullptr;
    RenderView* normal_roughness_render_view = nullptr;
    RenderView* depth_render_view = nullptr;
    uint32 width = 0;
    uint32 height = 0;
};

void destroy_gbuffer(TextureAllocator& texture_allocator, GBuffer& gbuffer) noexcept
{
    destroy_render_view(gbuffer.depth_render_view);
    destroy_render_view(gbuffer.normal_roughness_render_view);
    destroy_render_view(gbuffer.albedo_render_view);
    texture_allocator.free(gbuffer.depth);
    texture_allocator.free(gbuffer.normal_roughness);
    texture_allocator.free(gbuffer.albedo);
    gbuffer = {};
}

void recreate_gbuffer(CommandBuffer* commands, TextureAllocator& texture_allocator, GBuffer& gbuffer,
                     TextureDescriptorHeap* descriptors, uint32 first_descriptor,
                     uint32 width, uint32 height) noexcept
{
    gbuffer = {
        .albedo = texture_allocator.allocate({
            .extent = {.x = width, .y = height, .z = 1},
            .usage = TextureUsage::sampled | TextureUsage::color_attachment,
        }),
        .normal_roughness = texture_allocator.allocate({
            .extent = {.x = width, .y = height, .z = 1},
            .format = Format::rgba16_float,
            .usage = TextureUsage::sampled | TextureUsage::color_attachment,
        }),
        .depth = texture_allocator.allocate({
            .extent = {.x = width, .y = height, .z = 1},
            .format = Format::d32_float,
            .usage = TextureUsage::sampled | TextureUsage::depth_stencil_attachment,
        }),
        .width = width,
        .height = height,
    };

    initialize_texture(commands, gbuffer.albedo.texture);
    initialize_texture(commands, gbuffer.normal_roughness.texture);
    initialize_texture(commands, gbuffer.depth.texture);
    gbuffer.albedo_render_view = create_render_view(gbuffer.albedo.texture);
    gbuffer.normal_roughness_render_view = create_render_view(gbuffer.normal_roughness.texture);
    gbuffer.depth_render_view = create_render_view(gbuffer.depth.texture);

    write_texture_descriptor(descriptors, first_descriptor + uint32(GBufferTexture::albedo), gbuffer.albedo.texture, TextureDescriptorType::sampled);
    write_texture_descriptor(descriptors, first_descriptor + uint32(GBufferTexture::normal_roughness), gbuffer.normal_roughness.texture,
                             TextureDescriptorType::sampled);
    write_texture_descriptor(descriptors, first_descriptor + uint32(GBufferTexture::depth), gbuffer.depth.texture, TextureDescriptorType::sampled);
}

float random_signed(uint32& state) noexcept
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return float(state >> 8u) * (2.0f / 16777216.0f) - 1.0f;
}

void initialize_object_data(ObjectData* objects) noexcept
{
    constexpr float minimum_radius_squared = minimum_orbit_radius * minimum_orbit_radius;
    constexpr float maximum_radius_squared = maximum_orbit_radius * maximum_orbit_radius;
    const float softening_squared = gravity_softening * gravity_softening;
    uint32 random_state = 0x12345678u;

    for (uint32 i = 0; i != object_count; ++i)
    {
        float3 position{};
        float radius_squared = 0.0f;
        do
        {
            position = {
                .x = random_signed(random_state) * maximum_orbit_radius,
                .y = random_signed(random_state) * maximum_orbit_radius,
                .z = random_signed(random_state) * maximum_orbit_radius,
            };
            radius_squared = math::dot(position, position);
        } while (radius_squared < minimum_radius_squared || radius_squared > maximum_radius_squared);

        float3 tangent{};
        float axis_length_squared = 0.0f;
        do
        {
            const float3 random_axis{
                .x = random_signed(random_state),
                .y = random_signed(random_state),
                .z = random_signed(random_state),
            };
            axis_length_squared = math::dot(random_axis, random_axis);
            tangent = math::cross(position, random_axis);
        } while (axis_length_squared < 0.01f || axis_length_squared > 1.0f || math::dot(tangent, tangent) < 0.01f);
        tangent = math::normalize(tangent);

        const float inverse_softened_distance = math::rsqrt(radius_squared + softening_squared);
        const float circular_speed = sqrtf(
            central_gravity * radius_squared *
            inverse_softened_distance * inverse_softened_distance *
            inverse_softened_distance);
        const float speed_scale = 0.985f + random_signed(random_state) * 0.045f;
        objects[i] = {
            .position = position,
            .velocity = tangent * (circular_speed * speed_scale),
        };
    }
}

struct DeferredFrameState
{
    Device* device = nullptr;
    PSO* simulation_pso = nullptr;
    PSO* gbuffer_pso = nullptr;
    PSO* deferred_lighting_pso = nullptr;
    TextureDescriptorHeap* texture_descriptor_heap = nullptr;
    GpuCpuRange<byte> frame_ranges[frames_in_flight]{};
    GpuCpuRange<ObjectData> object_allocation{};
    TextureAllocator* texture_allocator = nullptr;
    GBuffer gbuffer{};
    uint32 descriptor_row = 0;
    double previous_time = 0.0;
    float rotation_angle = 0.0f;
    TimelinePoint latest_completion{};
    DeleteQueue* delete_queue = nullptr;
    CommandPool* command_pools[frames_in_flight]{};
    float4x4 view{};
    float3 view_right{};
    float3 view_up{};
    float3 view_forward{};
};

void render_deferred_frame(void* user_data) noexcept
{
    DeferredFrameState& state = *static_cast<DeferredFrameState*>(user_data);

    // Limit the application to two frames in flight so double-buffered descriptors are safe to reuse
    if (state.latest_completion.value >= frames_in_flight)
    {
        wait_timeline({.semaphore = state.latest_completion.semaphore, .value = state.latest_completion.value - frames_in_flight + 1});
    }
    state.delete_queue->tick();

    BumpAllocator frame_data(state.frame_ranges[state.latest_completion.value % frames_in_flight]);
    CommandPool* command_pool = state.command_pools[state.latest_completion.value % frames_in_flight];
    reset_command_pool(command_pool);
    CommandBuffer* commands = begin_commands(command_pool);
    const SwapchainFrame frame = acquire(commands);
    if (!frame.render_view)
        return;
    const uint32x2 extent = frame.extent;

    // Window resize?
    if (extent.x != state.gbuffer.width || extent.y != state.gbuffer.height)
    {
        if (state.gbuffer.albedo.texture)
        {
            state.delete_queue->defer(state.latest_completion.value,
                [allocator = state.texture_allocator, gbuffer = state.gbuffer]() mutable noexcept { destroy_gbuffer(*allocator, gbuffer); });
            state.descriptor_row = (state.descriptor_row + 1) % frames_in_flight;
        }
        recreate_gbuffer(commands, *state.texture_allocator, state.gbuffer,
                         state.texture_descriptor_heap, state.descriptor_row * gbuffer_texture_count, extent.x, extent.y);
    }

    set_texture_descriptor_heap(commands, state.texture_descriptor_heap);

    // Simulation
    const double current_time = example_time_seconds();
    const float delta_seconds = float(current_time - state.previous_time);
    state.previous_time = current_time;

    barrier(commands,
        Stage::compute | Stage::mesh, Access::shader_write | Access::shader_read,
        Stage::compute,  Access::shader_read | Access::shader_write);

    bind_pso(commands, state.simulation_pso);
    const GpuCpuRange<SimulationRoot> simulation_root = frame_data.allocate<SimulationRoot>();
    *simulation_root.cpu = {
        .objects = state.object_allocation.gpu,
        .delta_seconds = delta_seconds,
    };
    dispatch(commands, simulation_root.gpu, {.x = object_count / simulation_thread_count, .y = 1, .z = 1});

    // G-buffer
    barrier(commands,
        Stage::compute, Access::shader_write,
        Stage::mesh, Access::shader_read);
    barrier(commands,
        Stage::fragment | Stage::color_output | Stage::depth_stencil_tests,
        Access::shader_read | Access::color_write | Access::depth_stencil_write,
        Stage::color_output | Stage::depth_stencil_tests, Access::color_write | Access::depth_stencil_write);

    begin_render_pass(commands, {
        .colors = { {
                .render_view = state.gbuffer.albedo_render_view,
                .load = LoadOp::clear,
            }, {
                .render_view = state.gbuffer.normal_roughness_render_view,
                .load = LoadOp::clear,
            },
        },
        .depth = {
            .render_view = state.gbuffer.depth_render_view,
            .load = LoadOp::clear,
        },
    });

    set_depth_stencil(commands, {.depth_test = true, .depth_write = true});
    bind_pso(commands, state.gbuffer_pso);

    float4x4 projection = math::perspective_rh_zo(math::pi / 3.0f, float(extent.x) / float(extent.y), 0.3f, 1500.0f);
    projection.rows[1].y = -projection.rows[1].y;
    state.rotation_angle += cube_rotation_speed * delta_seconds;

    const GpuCpuRange<GBufferRoot> gbuffer_root = frame_data.allocate<GBufferRoot>();
    *gbuffer_root.cpu = {
        .objects = state.object_allocation.gpu,
        .view_projection = projection * state.view,
        .orientation = math::to_float3x4(
            math::rotation_y(state.rotation_angle) *
            math::rotation_x(state.rotation_angle * 0.5f) *
            math::scale({.x = cube_scale, .y = cube_scale, .z = cube_scale})),
    };

    draw_meshlets(commands, gbuffer_root.gpu, {.x = object_grid_width, .y = object_grid_width, .z = 1});

    end_render_pass(commands);

    // Lighting
    barrier(commands,
        Stage::color_output | Stage::depth_stencil_tests, Access::color_write | Access::depth_stencil_write,
        Stage::fragment, Access::shader_read);

    begin_render_pass(commands, {
        .colors = {{
            .render_view = frame.render_view,
            .load = LoadOp::discard,
        }},
    });

    bind_pso(commands, state.deferred_lighting_pso);

    const GpuCpuRange<DeferredLightingRoot> deferred_lighting_root = frame_data.allocate<DeferredLightingRoot>();
    *deferred_lighting_root.cpu = {
        .camera_position = math::to_float4(camera_position, 1.0f),
        .ray_center = math::to_float4(state.view_forward),
        .ray_horizontal = math::to_float4(state.view_right / projection.rows[0].x),
        .ray_vertical = math::to_float4(state.view_up / projection.rows[1].y),
        .depth_linearize =
            {
                .x = -projection.rows[2].w,
                .y = -projection.rows[2].z,
            },
        .gbuffer_pixel_scale =
            {
                .x = float(state.gbuffer.width) / float(frame.extent.x),
                .y = float(state.gbuffer.height) / float(frame.extent.y),
            },
        .gbuffer_texture_base = state.descriptor_row * gbuffer_texture_count,
    };

    draw(commands, deferred_lighting_root.gpu, 3);

    end_render_pass(commands);

    // Submit
    end_commands(commands);
    state.latest_completion.value++;
    submit_and_present(state.device, {.commands = {commands}, .completion = state.latest_completion});
}

} // namespace

int main()
{
    // Init
    void* window = open_example_window("NoGraphicsAPI deferred renderer", initial_width, initial_height);
    Device* device = create_device({.window = window, .swapchain_format = Format::bgra8_srgb}).device;

    if (!window || !device)
    {
        destroy_device(device);
        close_example_window(window);
        return 1;
    }

    const DeviceCaps& caps = get_device_caps(device);
    printf("Using %s\n", caps.device_name);

    // Shaders
    const Span<byte> simulation_code = read_shader(NOGRAPHICSAPI_SIMULATION_COMPUTE_SHADER_PATH);
    const Span<byte> gbuffer_mesh_code = read_shader(NOGRAPHICSAPI_GBUFFER_MESH_SHADER_PATH);
    const Span<byte> gbuffer_fragment_code = read_shader(NOGRAPHICSAPI_GBUFFER_FRAGMENT_SHADER_PATH);
    const Span<byte> deferred_vertex_code = read_shader(NOGRAPHICSAPI_DEFERRED_VERTEX_SHADER_PATH);
    const Span<byte> deferred_fragment_code = read_shader(NOGRAPHICSAPI_DEFERRED_FRAGMENT_SHADER_PATH);
    PSO* simulation_pso = create_compute_pso(device, {
        .code = {simulation_code.data, simulation_code.size}, .entry_point = "computeMain",
        .threadgroup_size = {.x = simulation_thread_count, .y = 1, .z = 1},
    });
    free(simulation_code.data);

    PSO* gbuffer_pso = create_mesh_pso(device, {
        .mesh = {.code = {gbuffer_mesh_code.data, gbuffer_mesh_code.size}, .entry_point = "meshMain",
            .threadgroup_size = {.x = gbuffer_thread_count, .y = 1, .z = 1}},
        .fragment = {.code = {gbuffer_fragment_code.data, gbuffer_fragment_code.size}, .entry_point = "fragmentMain"},
        .color_targets = {
            {.format = Format::rgba8_unorm},
            {.format = Format::rgba16_float},
        },
        .depth_format = Format::d32_float,
        .rasterization = { .cull = CullMode::clockwise },
    });
    free(gbuffer_fragment_code.data);
    free(gbuffer_mesh_code.data);

    PSO* deferred_lighting_pso = create_graphics_pso(device, {
        .vertex = {.code = {deferred_vertex_code.data, deferred_vertex_code.size}, .entry_point = "vertexMain"},
        .fragment = {.code = {deferred_fragment_code.data, deferred_fragment_code.size}, .entry_point = "fragmentMain"},
        .color_targets = {{.format = Format::bgra8_srgb}},
    });
    free(deferred_fragment_code.data);
    free(deferred_vertex_code.data);
    if (!simulation_pso || !gbuffer_pso || !deferred_lighting_pso)
    {
        destroy_pso(deferred_lighting_pso);
        destroy_pso(gbuffer_pso);
        destroy_pso(simulation_pso);
        destroy_device(device);
        close_example_window(window);
        return 1;
    }

    // GPU resources
    TextureDescriptorHeap* texture_descriptor_heap =
        create_texture_descriptor_heap(device, gbuffer_texture_count * frames_in_flight);
    GpuHeap data_heap = create_gpu_heap(device, data_heap_size);
    BumpAllocator data_allocator(data_heap.range);
    const GpuCpuRange<byte> frame_ranges[frames_in_flight]{data_allocator.allocate(4096), data_allocator.allocate(4096)};
    const GpuCpuRange<ObjectData> object_allocation = data_allocator.allocate<ObjectData>(object_count);
    initialize_object_data(object_allocation.cpu);
    TextureHeap texture_heap = create_texture_heap(device, texture_heap_size);
    TextureAllocator texture_allocator(device, texture_heap, 64);

    // Init camera and simulation
    const float4x4 view = math::look_at_rh(camera_position,
        {.x = 0.0f, .y = 0.0f, .z = 0.0f},
        {.x = 0.0f, .y = 1.0f, .z = 0.0f});
    const float3 view_right = math::to_float3(view.rows[0]);
    const float3 view_up = math::to_float3(view.rows[1]);
    const float3 view_forward = -math::to_float3(view.rows[2]);

    TimelinePoint latest_completion{.semaphore = create_timeline_semaphore(device)};
    DeleteQueue delete_queue(latest_completion.semaphore, frames_in_flight);
    CommandPool* command_pools[frames_in_flight] = {create_command_pool(device), create_command_pool(device)};

    DeferredFrameState state{
        .device = device,
        .simulation_pso = simulation_pso,
        .gbuffer_pso = gbuffer_pso,
        .deferred_lighting_pso = deferred_lighting_pso,
        .texture_descriptor_heap = texture_descriptor_heap,
        .frame_ranges = {frame_ranges[0], frame_ranges[1]},
        .object_allocation = object_allocation,
        .texture_allocator = &texture_allocator,
        .previous_time = example_time_seconds(),
        .latest_completion = latest_completion,
        .delete_queue = &delete_queue,
        .command_pools = {command_pools[0], command_pools[1]},
        .view = view,
        .view_right = view_right,
        .view_up = view_up,
        .view_forward = view_forward,
    };

    run_example_loop(window, render_deferred_frame, &state);

    wait_idle(device);
    delete_queue.drain();

    // Cleanup
    for (uint32 i = 0; i != frames_in_flight; ++i)
        destroy_command_pool(command_pools[i]);
    destroy_timeline_semaphore(latest_completion.semaphore);
    destroy_pso(deferred_lighting_pso);
    destroy_pso(gbuffer_pso);
    destroy_pso(simulation_pso);
    destroy_gbuffer(texture_allocator, state.gbuffer);
    destroy_texture_heap(texture_heap);
    destroy_gpu_heap(data_heap);
    destroy_texture_descriptor_heap(texture_descriptor_heap);

    destroy_device(device);
    close_example_window(window);
    return 0;
}
