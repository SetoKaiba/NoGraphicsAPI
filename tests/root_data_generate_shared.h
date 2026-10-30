#pragma once
#include "root_data_shared.h"

struct RootDataGenerate
{
    RootData* destination;
    uint32* output;
    uint32* dispatch_arguments;
    uint32 first_index;
    uint32 seed;
#if defined(__wasm32__)
    uint32 root_alignment_padding0[3];
#endif
};

#if !defined(__SLANG__)
static_assert(sizeof(RootDataGenerate) == 32);
#endif
