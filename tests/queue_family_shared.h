#pragma once

#include <NoGraphicsAPIUtility/shader_types.h>

static const uint32 queue_test_width = 17;
static const uint32 queue_test_height = 9;

struct QueueFamilyRoot
{
    uint32* source;
    uint32* destination;
    uint32 source_texture;
    uint32 destination_texture;
#if defined(__wasm32__)
    uint32 root_alignment_padding0[4];
#else
    uint32 root_alignment_padding0[2];
#endif
};

#if !defined(__SLANG__)
static_assert(sizeof(QueueFamilyRoot) == 32);
#endif
