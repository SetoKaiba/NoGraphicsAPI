#pragma once

#include "object_data.h"

struct SimulationRoot
{
    ObjectData* objects;
    float delta_seconds;
#if defined(__wasm32__)
    uint32 root_alignment_padding0[2];
#else
    uint32 root_alignment_padding0;
#endif
};

static const uint32 simulation_thread_count = 256;
static const float central_gravity = 81000.0f;
static const float gravity_softening = 12.0f;

#if !defined(__SLANG__)
static_assert(sizeof(SimulationRoot) == 16);
#endif
