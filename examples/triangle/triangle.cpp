#include "example_support.hpp"

#include <stdio.h>
#include <stdlib.h>

using namespace gpu;

namespace
{

struct TriangleFrameState
{
    Device* device = nullptr;
    PSO* triangle_pso = nullptr;
    TimelinePoint latest_completion{};
    CommandPool* command_pools[2]{};
};

void render_triangle_frame(void* user_data) noexcept
{
    TriangleFrameState& state = *static_cast<TriangleFrameState*>(user_data);
    if (state.latest_completion.value >= 2)
        wait_timeline({.semaphore = state.latest_completion.semaphore, .value = state.latest_completion.value - 1});
    CommandPool* command_pool = state.command_pools[state.latest_completion.value % 2];
    reset_command_pool(command_pool);
    CommandBuffer* commands = begin_commands(command_pool);
    const SwapchainFrame frame = acquire(commands);
    if (!frame.render_view)
        return;
    begin_render_pass(commands, {
        .colors = { { .render_view = frame.render_view, .load = LoadOp::clear } },
    });
    bind_pso(commands, state.triangle_pso);
    draw(commands, {}, 3);
    end_render_pass(commands);
    end_commands(commands);
    state.latest_completion.value++;
    submit_and_present(state.device, {.commands = {commands}, .completion = state.latest_completion});
}

} // namespace

int main()
{
    constexpr uint32 width = 512;
    constexpr uint32 height = 512;

    void* window = open_example_window("NoGraphicsAPI triangle", width, height);
    Device* device = create_device({.window = window, .swapchain_format = Format::bgra8_srgb}).device;

    if (!window || !device)
    {
        destroy_device(device);
        close_example_window(window);
        return 1;
    }

    printf("Using %s\n", get_device_caps(device).device_name);

    const Span<byte> vertex_code = read_shader(NOGRAPHICSAPI_VERTEX_SHADER_PATH);
    const Span<byte> fragment_code = read_shader(NOGRAPHICSAPI_FRAGMENT_SHADER_PATH);
    PSO* triangle_pso = create_graphics_pso(device, {
        .vertex = {.code = {vertex_code.data, vertex_code.size}, .entry_point = "vertexMain"},
        .fragment = {.code = {fragment_code.data, fragment_code.size}, .entry_point = "fragmentMain"},
        .color_targets = { { .format = Format::bgra8_srgb } }
    });
    free(fragment_code.data);
    free(vertex_code.data);
    if (!triangle_pso)
    {
        destroy_device(device);
        close_example_window(window);
        return 1;
    }

    TimelinePoint latest_completion{ .semaphore = create_timeline_semaphore(device) };
    CommandPool* command_pools[] = {create_command_pool(device), create_command_pool(device)};
    TriangleFrameState state{
        .device = device,
        .triangle_pso = triangle_pso,
        .latest_completion = latest_completion,
        .command_pools = {command_pools[0], command_pools[1]},
    };

    run_example_loop(window, render_triangle_frame, &state);

    wait_idle(device);

    destroy_command_pool(command_pools[1]);
    destroy_command_pool(command_pools[0]);
    destroy_timeline_semaphore(latest_completion.semaphore);
    destroy_pso(triangle_pso);

    destroy_device(device);
    close_example_window(window);
    return 0;
}
