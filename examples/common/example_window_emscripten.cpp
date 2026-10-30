#include "example_support.hpp"

#include <emscripten.h>
#include <emscripten/html5.h>

double example_time_seconds() noexcept
{
    return emscripten_get_now() * 1.0e-3;
}

void* open_example_window(const char* title, uint32 width, uint32 height) noexcept
{
    emscripten_set_canvas_element_size("#canvas", width, height);
    emscripten_set_window_title(title);
    return const_cast<char*>("#canvas");
}

bool pump_example_window(void* window) noexcept
{
    (void)window;
    return true;
}

void close_example_window(void*& window) noexcept
{
    window = nullptr;
}

void run_example_loop(void* window, ExampleFrameFn frame_fn, void* user_data) noexcept
{
    (void)window;
    emscripten_set_main_loop_arg(frame_fn, user_data, 0, true);
}
