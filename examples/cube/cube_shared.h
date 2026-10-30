#pragma once

#include <NoGraphicsAPIUtility/shader_types.h>

struct CubeVertex
{
    float4 position;
    float2 uv;
};

struct CubeRootArguments
{
    CubeVertex* vertices;
#if defined(__wasm32__)
    uint32 root_alignment_padding0[3];
#else
    uint32 root_alignment_padding0[2];
#endif
    float4x4 transform;
};

#if !defined(__SLANG__)
static_assert(sizeof(CubeRootArguments) == 80);
#endif
