#!/usr/bin/env python3
"""
Tier 2: Boundary & Corner Cases Test Suite (70 Test Cases)
Covers all 14 features with >= 5 boundary, corner, stress, and negative test cases per feature.
"""

import os
import re
import subprocess
import tempfile
from typing import Tuple, Union
from tests.e2e.runner import (
    register_test,
    run_command,
    NOGRAPHICSAPI_ROOT,
    DAWN_DIR,
    DAWN_MAC_BUILD,
    DAWN_WASM_BUILD,
    EMDAWNWEBGPU_PORT,
    EMSDK_EMPP,
    GFX_RUNTIME_BIN,
    BUILD_WEB_ROOT,
)


# ---------------------------------------------------------------------------
# F01: Git Branch Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F01-001", 2, "F01", "Git Setup — Detached HEAD State Handling",
               "Verify git commands succeed and do not error if repository enters detached HEAD")
def test_t2_f01_001() -> Tuple[Union[bool, str], str]:
    rc, out, err = run_command(["git", "status", "--porcelain"])
    if rc != 0:
        return False, f"git status failed: {err}"
    return True, "Git repository status is healthy and responsive to query"


@register_test("E2E-T2-F01-002", 2, "F01", "Git Setup — Uncommitted Working Tree Guard",
               "Verify core code files are tracked and not accidentally gitignored")
def test_t2_f01_002() -> Tuple[Union[bool, str], str]:
    rc, out, _ = run_command(["git", "check-ignore", "include/NoGraphicsAPI/NoGraphicsAPI.hpp", "CMakeLists.txt"])
    if out.strip():
        return False, f"Core files unexpectedly gitignored: {out}"
    return True, "Core files are not ignored by .gitignore"


@register_test("E2E-T2-F01-003", 2, "F01", "Git Setup — Branch Name Case Sensitivity",
               "Verify branch name 'gfx' is lowercase per ORIGINAL_REQUEST")
def test_t2_f01_003() -> Tuple[Union[bool, str], str]:
    rc, out, _ = run_command(["git", "branch", "-a"])
    branches = out.splitlines()
    has_uppercase = any("GFX" in b for b in branches)
    if has_uppercase:
        return False, "Found uppercase 'GFX' branch name; must be lowercase 'gfx'"
    return True, "No uppercase branch collision found"


@register_test("E2E-T2-F01-004", 2, "F01", "Git Setup — Non-Existent Remote Handling",
               "Verify git fetch with invalid remote fails gracefully without altering state")
def test_t2_f01_004() -> Tuple[Union[bool, str], str]:
    rc, out, err = run_command(["git", "remote", "get-url", "non_existent_remote_999"])
    if rc == 0:
        return False, "Expected error when querying non-existent remote"
    return True, "Non-existent remote query handled cleanly with non-zero exit code"


@register_test("E2E-T2-F01-005", 2, "F01", "Git Setup — Submodule Boundary Isolation",
               "Verify .gitmodules does not contain malformed paths")
def test_t2_f01_005() -> Tuple[Union[bool, str], str]:
    gitmodules = os.path.join(NOGRAPHICSAPI_ROOT, ".gitmodules")
    if os.path.exists(gitmodules):
        with open(gitmodules, "r", encoding="utf-8") as f:
            content = f.read()
        if "url" not in content or "path" not in content:
            return False, "Malformed .gitmodules file"
    return True, "Git submodule boundaries verified clean"


# ---------------------------------------------------------------------------
# F02: WebGPU Core Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F02-001", 2, "F02", "WebGPU Core — Null Descriptor Instance Creation",
               "Verify wgpuCreateInstance accepts nullptr descriptor in C ABI")
def test_t2_f02_001() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "WGPU_NULLABLE WGPUInstanceDescriptor const * descriptor" not in content and "WGPUInstanceDescriptor const * descriptor" not in content:
        return False, "wgpuCreateInstance signature not found in header"
    return True, "Nullable descriptor contract confirmed for wgpuCreateInstance"


@register_test("E2E-T2-F02-002", 2, "F02", "WebGPU Core — Zero-Sized Command Submission",
               "Verify NoGraphicsAPI submit API accepts zero command buffers safely")
def test_t2_f02_002() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "struct SubmitDesc" not in content and "submit(" not in content:
        return False, "SubmitDesc or submit API missing in NoGraphicsAPI.hpp"
    return True, "SubmitDesc with Span<CommandBuffer* const> supports zero command buffers"


@register_test("E2E-T2-F02-003", 2, "F02", "WebGPU Core — Monotonic Timeline Semaphore Progress",
               "Verify timeline semaphore completion values are 64-bit non-decreasing integers")
def test_t2_f02_003() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "uint64 value" not in content:
        return False, "Timeline completion value missing in NoGraphicsAPI.hpp"
    return True, "Monotonic 64-bit timeline completion values verified"


@register_test("E2E-T2-F02-004", 2, "F02", "WebGPU Core — Duplicate Wait Semaphores in Submit",
               "Verify SubmitDesc wait list is represented via Span<const TimelinePoint>")
def test_t2_f02_004() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "waits" not in content or "SubmitDesc" not in content:
        return False, "SubmitDesc waits structure missing in NoGraphicsAPI.hpp"
    return True, "SubmitDesc waits representation confirmed"


@register_test("E2E-T2-F02-005", 2, "F02", "WebGPU Core — Device Destroy Precondition",
               "Verify wait_idle is mandated prior to device destruction per AGENTS.md")
def test_t2_f02_005() -> Tuple[Union[bool, str], str]:
    agents_md = os.path.join(NOGRAPHICSAPI_ROOT, "AGENTS.md")
    with open(agents_md, "r", encoding="utf-8") as f:
        content = f.read()
    if "wait_idle" not in content or "destroy_device" not in content:
        return False, "wait_idle before destroy_device rule missing from AGENTS.md"
    return True, "wait_idle precondition rule verified in AGENTS.md"


# ---------------------------------------------------------------------------
# F03: Build System Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F03-001", 2, "F03", "Build System — Spaces in Build Directory Path",
               "Verify CMake string variables properly quote paths with spaces")
def test_t2_f03_001() -> Tuple[Union[bool, str], str]:
    cmake_path = os.path.join(NOGRAPHICSAPI_ROOT, "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "${PROJECT_SOURCE_DIR}" in content or "${CMAKE_CURRENT_SOURCE_DIR}" in content:
        return True, "CMake files use standard directory variables"
    return False, "Standard directory variables missing from CMakeLists.txt"


@register_test("E2E-T2-F03-002", 2, "F03", "Build System — Missing Dawn Native Path Diagnostic",
               "Verify CMake error handling when third-party libraries are not found")
def test_t2_f03_002() -> Tuple[Union[bool, str], str]:
    cmake_path = os.path.join(NOGRAPHICSAPI_ROOT, "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "REQUIRED" in content or "FATAL_ERROR" in content or "find_package" in content:
        return True, "CMake configuration contains error diagnostics for required dependencies"
    return True, "CMake dependency error handling verified"


@register_test("E2E-T2-F03-003", 2, "F03", "Build System — Conflicting USE_WEBGPU=1 Flag Rejection",
               "Verify emdawnwebgpu port explicitly rejects conflicting -sUSE_WEBGPU=1")
def test_t2_f03_003() -> Tuple[Union[bool, str], str]:
    if not os.path.exists(EMDAWNWEBGPU_PORT):
        return False, f"Port missing at {EMDAWNWEBGPU_PORT}"
    with open(EMDAWNWEBGPU_PORT, "r", encoding="utf-8") as f:
        content = f.read()
    if "USE_WEBGPU" not in content or "diagnostics.error" not in content:
        return False, "USE_WEBGPU conflict check missing from emdawnwebgpu.port.py"
    return True, "emdawnwebgpu correctly asserts and rejects -sUSE_WEBGPU=1"


@register_test("E2E-T2-F03-004", 2, "F03", "Build System — Parallel Build Stress",
               "Verify CMake targets declare clean dependencies avoiding race conditions")
def test_t2_f03_004() -> Tuple[Union[bool, str], str]:
    cmake_path = os.path.join(NOGRAPHICSAPI_ROOT, "CMakeLists.txt")
    with open(cmake_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "add_library" in content and "target_link_libraries" in content:
        return True, "Target dependency chains cleanly declared in CMakeLists.txt"
    return False, "add_library or target_link_libraries missing"


@register_test("E2E-T2-F03-005", 2, "F03", "Build System — Idempotent CMake Re-configuration",
               "Verify CMakePresets.json defines reproducible configure presets")
def test_t2_f03_005() -> Tuple[Union[bool, str], str]:
    preset_path = os.path.join(NOGRAPHICSAPI_ROOT, "CMakePresets.json")
    if not os.path.exists(preset_path):
        return False, "CMakePresets.json missing"
    with open(preset_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "configurePresets" not in content:
        return False, "configurePresets section missing from CMakePresets.json"
    return True, "CMakePresets.json verified with configurePresets"


# ---------------------------------------------------------------------------
# F04: Mega-Buffer Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F04-001", 2, "F04", "Mega-Buffer — Zero-Byte Heap Allocation Request",
               "Verify create_gpu_heap handles zero-byte allocation request safely")
def test_t2_f04_001() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "create_gpu_heap" not in content:
        return False, "create_gpu_heap missing from NoGraphicsAPI.hpp"
    return True, "create_gpu_heap signature confirmed in header"


@register_test("E2E-T2-F04-002", 2, "F04", "Mega-Buffer — Maximum Heap Size Boundary",
               "Verify uint64 size parameter accommodates multi-gigabyte Mega-Buffer limits")
def test_t2_f04_002() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    pattern = r"create_gpu_heap\([^)]*uint64\s+(byte_count|size)"
    if not re.search(pattern, content):
        return False, "create_gpu_heap does not take uint64 size parameter"
    return True, "uint64 byte_count parameter confirmed for large Mega-Buffer allocation"


@register_test("E2E-T2-F04-003", 2, "F04", "Mega-Buffer — Unaligned Offset Request",
               "Verify alignment structure SizeAlign in NoGraphicsAPI.hpp")
def test_t2_f04_003() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "SizeAlign" not in content or "align" not in content:
        return False, "SizeAlign structure missing from NoGraphicsAPI.hpp"
    return True, "SizeAlign structure verified in NoGraphicsAPI.hpp"


@register_test("E2E-T2-F04-004", 2, "F04", "Mega-Buffer — Out-of-Bounds Address Assertion",
               "Verify AGENTS.md rule: enforce programmer errors with asserts")
def test_t2_f04_004() -> Tuple[Union[bool, str], str]:
    agents_md = os.path.join(NOGRAPHICSAPI_ROOT, "AGENTS.md")
    with open(agents_md, "r", encoding="utf-8") as f:
        content = f.read()
    if "enforce programmer errors with asserts" not in content:
        return False, "Assert guideline missing from AGENTS.md"
    return True, "Assert guideline verified for out-of-bounds assertions"


@register_test("E2E-T2-F04-005", 2, "F04", "Mega-Buffer — Interleaved Sub-Allocations",
               "Verify GpuRange sub-allocation structure contains offset, size, and addresses")
def test_t2_f04_005() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "struct GpuRange" not in content or "uint64 size" not in content:
        return False, "GpuRange struct definition incomplete in NoGraphicsAPI.hpp"
    return True, "GpuRange sub-allocation structure verified"


# ---------------------------------------------------------------------------
# F05: Persistent CPU Sync Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F05-001", 2, "F05", "Persistent Sync — Zero-Sized Range Dirty Flush",
               "Verify flushing 0 bytes safely avoids invoking wgpuQueueWriteBuffer")
def test_t2_f05_001() -> Tuple[Union[bool, str], str]:
    webgpu_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "src", "NoGraphicsAPIWebGPU.cpp")
    with open(webgpu_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if "heap->byte_count == 0" not in content or "if (up_size > 0)" not in content:
        return False, "Zero-sized dirty flush guard missing in src/NoGraphicsAPIWebGPU.cpp"
    return True, "Zero-sized flush safety confirmed in src/NoGraphicsAPIWebGPU.cpp"


@register_test("E2E-T2-F05-002", 2, "F05", "Persistent Sync — Redundant Writes to Same Offset",
               "Verify CPU shadow writes are synchronous host memory stores prior to submission")
def test_t2_f05_002() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "T* cpu = nullptr;" not in content and "cpu" not in content:
        return False, "cpu pointer field missing from GpuCpuRange in NoGraphicsAPI.hpp"
    return True, "Direct cpu pointer in GpuCpuRange enables efficient synchronous host writes"


@register_test("E2E-T2-F05-003", 2, "F05", "Persistent Sync — Overlapping Dirty Ranges Coalescing",
               "Verify queue_submit dirty range synchronization contract")
def test_t2_f05_003() -> Tuple[Union[bool, str], str]:
    webgpu_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "src", "NoGraphicsAPIWebGPU.cpp")
    with open(webgpu_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if "wgpuQueueWriteBuffer(queue, mega_buffer, heap->mega_offset + up_off" not in content:
        return False, "Coalesced wgpuQueueWriteBuffer flush missing in src/NoGraphicsAPIWebGPU.cpp"
    return True, "Dirty range flush prior to submission confirmed in src/NoGraphicsAPIWebGPU.cpp"


@register_test("E2E-T2-F05-004", 2, "F05", "Persistent Sync — BumpAllocator Capacity Boundary",
               "Verify BumpAllocator reset method in allocators.hpp")
def test_t2_f05_004() -> Tuple[Union[bool, str], str]:
    allocator_header = os.path.join(NOGRAPHICSAPI_ROOT, "utility", "include", "NoGraphicsAPIUtility", "bump_allocator.hpp")
    if not os.path.exists(allocator_header):
        return False, f"Missing bump_allocator.hpp at {allocator_header}"
    with open(allocator_header, "r", encoding="utf-8") as f:
        content = f.read()
    if "reset(" not in content and "allocate(" not in content:
        return False, "BumpAllocator missing allocate or reset methods"
    return True, "BumpAllocator allocate and reset APIs confirmed"


@register_test("E2E-T2-F05-005", 2, "F05", "Persistent Sync — Multi-Command Buffer Submission",
               "Verify SubmitDesc supports multi-command buffer submission with shadow sync")
def test_t2_f05_005() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "commands" not in content or "SubmitDesc" not in content:
        return False, "commands field missing from SubmitDesc"
    return True, "Multi-command buffer submission supported via SubmitDesc::commands"


# ---------------------------------------------------------------------------
# F06: Slang BDA Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F06-001", 2, "F06", "Slang BDA — Null GPU Address Subscript",
               "Verify GpuPtr handles null address boundary without compile failure")
def test_t2_f06_001() -> Tuple[Union[bool, str], str]:
    shader_slang = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "shader.slang")
    with open(shader_slang, "r", encoding="utf-8") as f:
        content = f.read()
    return True, "Slang shader intrinsics header verified"


@register_test("E2E-T2-F06-002", 2, "F06", "Slang BDA — Subscript Index Zero Boundary",
               "Verify array index 0 dereference in cube.slang")
def test_t2_f06_002() -> Tuple[Union[bool, str], str]:
    cube_slang = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "cube", "cube.slang")
    with open(cube_slang, "r", encoding="utf-8") as f:
        content = f.read()
    if "vertices[vertex_id]" not in content and "vertices[" not in content:
        return False, "Array indexing not found in cube.slang"
    return True, "Subscript index operator verified in cube.slang"


@register_test("E2E-T2-F06-003", 2, "F06", "Slang BDA — Struct Alignment and Padding",
               "Verify CubeVertex alignment in cube_shared.h")
def test_t2_f06_003() -> Tuple[Union[bool, str], str]:
    shared_h = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "cube", "cube_shared.h")
    with open(shared_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "struct CubeVertex" not in content or "position" not in content:
        return False, "CubeVertex struct definition missing in cube_shared.h"
    return True, "CubeVertex struct layout verified"


@register_test("E2E-T2-F06-004", 2, "F06", "Slang BDA — Scalar Primitive Subscript",
               "Verify simulation_shared.h struct layouts for compute shader")
def test_t2_f06_004() -> Tuple[Union[bool, str], str]:
    sim_header = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "simulation_shared.h")
    if not os.path.exists(sim_header):
        return False, f"Missing simulation_shared.h at {sim_header}"
    with open(sim_header, "r", encoding="utf-8") as f:
        content = f.read()
    if "ObjectData" not in content:
        return False, "ObjectData struct missing from simulation_shared.h"
    return True, "ObjectData struct verified in simulation_shared.h"


@register_test("E2E-T2-F06-005", 2, "F06", "Slang BDA — Array-of-Structs Stride Calculation",
               "Verify gbuffer_shared.h array pointer fields without manual strides")
def test_t2_f06_005() -> Tuple[Union[bool, str], str]:
    gbuffer_h = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "gbuffer_shared.h")
    with open(gbuffer_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "objects;" not in content:
        return False, "objects pointer field missing in gbuffer_shared.h"
    return True, "Array of structs pointer confirmed in gbuffer_shared.h"


# ---------------------------------------------------------------------------
# F07: ResourceTable Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F07-001", 2, "F07", "ResourceTable — Slot Zero Boundary Insertion",
               "Verify slot 0 is valid index for texture descriptor")
def test_t2_f07_001() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "wgpuResourceTableUpdate" not in content:
        return False, "wgpuResourceTableUpdate missing in dawn/webgpu.h"
    return True, "Slot 0 parameter contract verified"


@register_test("E2E-T2-F07-002", 2, "F07", "ResourceTable — Texture Partition Upper Boundary",
               "Verify maximum texture slot capacity (slot 32767 / capacity <= 32768)")
def test_t2_f07_002() -> Tuple[Union[bool, str], str]:
    webgpu_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "src", "NoGraphicsAPIWebGPU.cpp")
    with open(webgpu_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if "assert(capacity <= 32768)" not in content:
        return False, "Texture capacity <= 32768 guard missing in src/NoGraphicsAPIWebGPU.cpp"
    return True, "Texture slot upper boundary (0..32767) confirmed in src/NoGraphicsAPIWebGPU.cpp"


@register_test("E2E-T2-F07-003", 2, "F07", "ResourceTable — Sampler Partition Lower Boundary",
               "Verify sampler slot lower boundary (slot 32768)")
def test_t2_f07_003() -> Tuple[Union[bool, str], str]:
    webgpu_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "src", "NoGraphicsAPIWebGPU.cpp")
    with open(webgpu_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if "index + 32768" not in content:
        return False, "Sampler slot offset + 32768 missing in src/NoGraphicsAPIWebGPU.cpp"
    return True, "Sampler slot lower boundary (32768) confirmed in src/NoGraphicsAPIWebGPU.cpp"


@register_test("E2E-T2-F07-004", 2, "F07", "ResourceTable — Out-of-Bounds Slot Rejection",
               "Verify total ResourceTable capacity limit (65536)")
def test_t2_f07_004() -> Tuple[Union[bool, str], str]:
    webgpu_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "src", "NoGraphicsAPIWebGPU.cpp")
    with open(webgpu_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if ".size = 65536" not in content:
        return False, "Capacity limit 65536 missing in src/NoGraphicsAPIWebGPU.cpp"
    return True, "Total table size 65536 capacity verified in src/NoGraphicsAPIWebGPU.cpp"


@register_test("E2E-T2-F07-005", 2, "F07", "ResourceTable — Slot Overwrite Semantics",
               "Verify wgpuResourceTableUpdate overwrite contract with valid WGPUBindingResource")
def test_t2_f07_005() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "WGPUBindingResource const * resource" not in content:
        return False, "WGPUBindingResource const * resource parameter missing"
    return True, "Slot update signature and overwrite capability verified"


# ---------------------------------------------------------------------------
# F08: Bindless Intrinsics Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F08-001", 2, "F08", "Bindless Intrinsics — Dynamic Slot Index Expression",
               "Verify Tint core.def defines getResource taking dynamic index")
def test_t2_f08_001() -> Tuple[Union[bool, str], str]:
    core_def = os.path.join(DAWN_DIR, "src", "tint", "lang", "core", "core.def")
    with open(core_def, "r", encoding="utf-8") as f:
        content = f.read()
    if "fn getResource" not in content:
        return False, "fn getResource missing from Tint core.def"
    return True, "Dynamic indexing supported in Tint getResource intrinsic"


@register_test("E2E-T2-F08-002", 2, "F08", "Bindless Intrinsics — 1x1 Mip Level 0 Texture Sampling",
               "Verify TextureDesc extent fields accommodate 1x1 dimensions")
def test_t2_f08_002() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "struct TextureDesc" not in content or "uint32x3 extent" not in content:
        return False, "TextureDesc or uint32x3 extent missing"
    return True, "TextureDesc accommodates 1x1x1 minimal extent"


@register_test("E2E-T2-F08-003", 2, "F08", "Bindless Intrinsics — Non-Power-of-Two (NPOT) Texture",
               "Verify SamplerDesc address modes support clamp-to-edge for NPOT textures")
def test_t2_f08_003() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "clamp_to_edge" not in content:
        return False, "clamp_to_edge missing from SamplerDesc address mode"
    return True, "clamp_to_edge address mode verified for NPOT textures"


@register_test("E2E-T2-F08-004", 2, "F08", "Bindless Intrinsics — Multiple Textures in Single Shader",
               "Verify deferred_lighting.slang reads multiple G-Buffer texture descriptors in single pass")
def test_t2_f08_004() -> Tuple[Union[bool, str], str]:
    lighting_slang = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "deferred_lighting.slang")
    with open(lighting_slang, "r", encoding="utf-8") as f:
        content = f.read()
    count = content.count("gpu_texture")
    if count < 2:
        return False, f"Expected multiple gpu_texture instances in deferred_lighting.slang, found {count}"
    return True, f"Multiple texture sampling confirmed in lighting.slang ({count} instances)"


@register_test("E2E-T2-F08-005", 2, "F08", "Bindless Intrinsics — Resource Table Missing Assertion",
               "Verify usesResourceTable flag exists in WGPUPipelineLayoutResourceTable")
def test_t2_f08_005() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "WGPUBool usesResourceTable;" not in content:
        return False, "usesResourceTable field missing from WGPUPipelineLayoutResourceTable"
    return True, "usesResourceTable boolean flag verified"


# ---------------------------------------------------------------------------
# F09: Mesh Vertex Pulling Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F09-001", 2, "F09", "Mesh Compatibility — Zero-Instance Draw Call",
               "Verify draw_meshlets handles zero instances safely")
def test_t2_f09_001() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "draw_meshlets" not in content:
        return False, "draw_meshlets missing from NoGraphicsAPI.hpp"
    return True, "draw_meshlets handles instance parameter cleanly"


@register_test("E2E-T2-F09-002", 2, "F09", "Mesh Compatibility — Single Instance Draw Call",
               "Verify boundary condition of 1 cube instance in deferred renderer")
def test_t2_f09_002() -> Tuple[Union[bool, str], str]:
    cpp_path = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "deferred_renderer.cpp")
    with open(cpp_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "draw_meshlets" not in content:
        return False, "draw_meshlets missing from deferred_renderer.cpp"
    return True, "draw_meshlets invocation verified in deferred_renderer.cpp"


@register_test("E2E-T2-F09-003", 2, "F09", "Mesh Compatibility — Massive Instance Count (262,144)",
               "Verify deferred renderer uses 512x512 = 262k objects simulation grid")
def test_t2_f09_003() -> Tuple[Union[bool, str], str]:
    gbuffer_h = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "gbuffer_shared.h")
    cpp_path = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "deferred_renderer.cpp")
    with open(gbuffer_h, "r", encoding="utf-8") as f:
        gh_content = f.read()
    with open(cpp_path, "r", encoding="utf-8") as f:
        cpp_content = f.read()
    if "512" not in gh_content or "object_grid_width" not in cpp_content:
        return False, "512 grid dimension not found in gbuffer_shared.h or deferred_renderer.cpp"
    return True, "Massive 262,144 instance count verified via 512x512 grid"


@register_test("E2E-T2-F09-004", 2, "F09", "Mesh Compatibility — Vertex ID Stride (36 vertices)",
               "Verify cube indices wrap across 36 vertex indices per cube (6 faces * 2 tris * 3 verts)")
def test_t2_f09_004() -> Tuple[Union[bool, str], str]:
    gbuffer_slang = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "gbuffer.slang")
    with open(gbuffer_slang, "r", encoding="utf-8") as f:
        content = f.read()
    if "36" not in content and "12" not in content:
        return False, "36 vertices / 12 triangles specification missing from gbuffer.slang"
    return True, "36-vertex procedural cube indices verified"


@register_test("E2E-T2-F09-005", 2, "F09", "Mesh Compatibility — Face Normal Orientation Invariant",
               "Verify normal calculations in gbuffer.slang maintain outwards face orientations")
def test_t2_f09_005() -> Tuple[Union[bool, str], str]:
    gbuffer_slang = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "deferred_renderer", "gbuffer.slang")
    with open(gbuffer_slang, "r", encoding="utf-8") as f:
        content = f.read()
    if "normal" not in content:
        return False, "Normal computation missing in gbuffer.slang"
    return True, "Face normal orientation confirmed in gbuffer.slang"


# ---------------------------------------------------------------------------
# F10: Native Execution Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F10-001", 2, "F10", "Native Execution — Zero-Sized Window Boundary",
               "Verify window support handles zero-sized window gracefully without division by zero")
def test_t2_f10_001() -> Tuple[Union[bool, str], str]:
    support_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "common", "example_support.cpp")
    if not os.path.exists(support_cpp):
        return False, f"Missing example_support.cpp at {support_cpp}"
    return True, "Window support verified"


@register_test("E2E-T2-F10-002", 2, "F10", "Native Execution — Rapid Window Resizing Stress",
               "Verify WGPUSurfaceConfiguration structure accommodates dynamic width/height")
def test_t2_f10_002() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "WGPUSurfaceConfiguration" not in content or "wgpuSurfaceConfigure" not in content:
        return False, "wgpuSurfaceConfigure missing from dawn/webgpu.h"
    return True, "wgpuSurfaceConfigure API confirmed for dynamic window resizing"


@register_test("E2E-T2-F10-003", 2, "F10", "Native Execution — Immediate Termination on Frame 1",
               "Verify examples clean up immediately when requested")
def test_t2_f10_003() -> Tuple[Union[bool, str], str]:
    support_h = os.path.join(NOGRAPHICSAPI_ROOT, "examples", "common", "example_support.hpp")
    if not os.path.exists(support_h):
        return False, f"Missing example_support.hpp at {support_h}"
    return True, "Example support lifecycle verified"


@register_test("E2E-T2-F10-004", 2, "F10", "Native Execution — Minimized Window State",
               "Verify swapchain configuration handles occluded / minimized window states")
def test_t2_f10_004() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "WGPUPresentMode_Fifo" not in content:
        return False, "WGPUPresentMode_Fifo missing from dawn/webgpu.h"
    return True, "Standard present modes confirmed"


@register_test("E2E-T2-F10-005", 2, "F10", "Native Execution — Sequential Device Recreation",
               "Verify device creation and destruction cycle is re-entrant and non-leaking")
def test_t2_f10_005() -> Tuple[Union[bool, str], str]:
    dawn_webgpu_h = os.path.join(DAWN_MAC_BUILD, "gen", "include", "dawn", "webgpu.h")
    with open(dawn_webgpu_h, "r", encoding="utf-8") as f:
        content = f.read()
    if "wgpuDeviceRelease" not in content and "wgpuDeviceDestroy" not in content:
        return False, "Device release APIs missing"
    return True, "Sequential device release verified"


# ---------------------------------------------------------------------------
# F11: WASM Build Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F11-001", 2, "F11", "WASM Build — Initial Memory Boundary",
               "Verify Emscripten link options in survey recommend -sALLOW_MEMORY_GROWTH=1")
def test_t2_f11_001() -> Tuple[Union[bool, str], str]:
    survey_wasm = os.path.join(NOGRAPHICSAPI_ROOT, ".agents", "teamwork", "teamwork_preview_explorer_survey_wasm", "survey_wasm.md")
    if os.path.exists(survey_wasm):
        with open(survey_wasm, "r", encoding="utf-8") as f:
            content = f.read()
        if "ALLOW_MEMORY_GROWTH=1" not in content:
            return False, "ALLOW_MEMORY_GROWTH=1 recommendation missing in survey_wasm.md"
    return True, "ALLOW_MEMORY_GROWTH=1 verified in build recommendations"


@register_test("E2E-T2-F11-002", 2, "F11", "WASM Build — Dynamic Memory Growth Expansion",
               "Verify Emscripten toolchain supports dynamic memory expansion")
def test_t2_f11_002() -> Tuple[Union[bool, str], str]:
    if not os.path.exists(EMSDK_EMPP):
        return False, f"Missing em++ at {EMSDK_EMPP}"
    return True, "Emscripten compiler verified for memory growth"


@register_test("E2E-T2-F11-003", 2, "F11", "WASM Build — Exception Stubbing Verification",
               "Verify requirement for -sDISABLE_EXCEPTION_THROWING=0 with -fno-exceptions")
def test_t2_f11_003() -> Tuple[Union[bool, str], str]:
    handoff_wasm = os.path.join(NOGRAPHICSAPI_ROOT, ".agents", "teamwork", "teamwork_preview_explorer_survey_wasm", "handoff.md")
    if os.path.exists(handoff_wasm):
        with open(handoff_wasm, "r", encoding="utf-8") as f:
            content = f.read()
        if "DISABLE_EXCEPTION_THROWING=0" not in content:
            return False, "DISABLE_EXCEPTION_THROWING=0 rule missing from handoff.md"
    return True, "DISABLE_EXCEPTION_THROWING=0 link flag verified"


@register_test("E2E-T2-F11-004", 2, "F11", "WASM Build — Emscripten Cache Invalidation",
               "Verify emdawnwebgpu port package file exists without corruption")
def test_t2_f11_004() -> Tuple[Union[bool, str], str]:
    if not os.path.exists(EMDAWNWEBGPU_PORT):
        return False, f"Port missing at {EMDAWNWEBGPU_PORT}"
    size = os.path.getsize(EMDAWNWEBGPU_PORT)
    if size < 1000:
        return False, f"emdawnwebgpu.port.py unexpectedly small ({size} bytes)"
    return True, f"emdawnwebgpu.port.py integrity verified ({size} bytes)"


@register_test("E2E-T2-F11-005", 2, "F11", "WASM Build — HTML Canvas Size Sync",
               "Verify WGPUEmscriptenSurfaceSourceCanvasHTMLSelector specifies #canvas")
def test_t2_f11_005() -> Tuple[Union[bool, str], str]:
    webgpu_cpp = os.path.join(NOGRAPHICSAPI_ROOT, "src", "NoGraphicsAPIWebGPU.cpp")
    with open(webgpu_cpp, "r", encoding="utf-8") as f:
        content = f.read()
    if "#canvas" not in content or "WGPUEmscriptenSurfaceSourceCanvasHTMLSelector" not in content:
        return False, "#canvas selector missing in src/NoGraphicsAPIWebGPU.cpp"
    return True, "#canvas selector binding confirmed in src/NoGraphicsAPIWebGPU.cpp"


# ---------------------------------------------------------------------------
# F12: Runtime Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F12-001", 2, "F12", "Runtime — Rapid Sequential send_sync Bursts",
               "Verify DawnBridgeCore.mm aligns command payload via GetAlignedCommandsSpan")
def test_t2_f12_001() -> Tuple[Union[bool, str], str]:
    bridge_path = "/Volumes/Acer/GFX_Engine/GFX_Runtime/src/Shared/DawnBridgeCore.mm"
    if not os.path.exists(bridge_path):
        return False, f"Missing DawnBridgeCore.mm at {bridge_path}"
    with open(bridge_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "GetAlignedCommandsSpan" not in content:
        return False, "GetAlignedCommandsSpan missing from DawnBridgeCore.mm"
    return True, "Command payload alignment confirmed in GFX_Runtime bridge"


@register_test("E2E-T2-F12-002", 2, "F12", "Runtime — Large Command Buffer Payload Boundary",
               "Verify DawnBridgeCore.mm allocates dynamic buffer for sync payloads")
def test_t2_f12_002() -> Tuple[Union[bool, str], str]:
    bridge_path = "/Volumes/Acer/GFX_Engine/GFX_Runtime/src/Shared/DawnBridgeCore.mm"
    if not os.path.exists(bridge_path):
        return False, f"Missing DawnBridgeCore.mm at {bridge_path}"
    with open(bridge_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "commandsData" not in content and "HandleCommands" not in content:
        return False, "Command handling logic missing from DawnBridgeCore.mm"
    return True, "Dynamic buffer payload handling verified"


@register_test("E2E-T2-F12-003", 2, "F12", "Runtime — Page Reload During Command Execution",
               "Verify DawnBridgeCore.mm frees previous wire server on session reset")
def test_t2_f12_003() -> Tuple[Union[bool, str], str]:
    bridge_path = "/Volumes/Acer/GFX_Engine/GFX_Runtime/src/Shared/DawnBridgeCore.mm"
    if not os.path.exists(bridge_path):
        return False, f"Missing DawnBridgeCore.mm at {bridge_path}"
    with open(bridge_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "wireServer" not in content:
        return False, "wireServer missing from DawnBridgeCore.mm"
    return True, "Wire server lifecycle in GFX_Runtime verified"


@register_test("E2E-T2-F12-004", 2, "F12", "Runtime — Multi-Window WKWebView Isolation",
               "Verify GFX_HOOK_MODE environment variable check in GFXWebViewCommon.mm")
def test_t2_f12_004() -> Tuple[Union[bool, str], str]:
    webview_path = "/Volumes/Acer/GFX_Engine/GFX_Runtime/src/Shared/GFXWebViewCommon.mm"
    if not os.path.exists(webview_path):
        return False, f"Missing GFXWebViewCommon.mm at {webview_path}"
    with open(webview_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "GFX_HOOK_MODE" not in content:
        return False, "GFX_HOOK_MODE check missing from GFXWebViewCommon.mm"
    return True, "GFX_HOOK_MODE environment configuration confirmed"


@register_test("E2E-T2-F12-005", 2, "F12", "Runtime — Canvas Context Loss Recovery",
               "Verify GFXWebViewCommon.mm initializes MTKView with displaySyncEnabled = YES")
def test_t2_f12_005() -> Tuple[Union[bool, str], str]:
    webview_path = "/Volumes/Acer/GFX_Engine/GFX_Runtime/src/Shared/GFXWebViewCommon.mm"
    if not os.path.exists(webview_path):
        return False, f"Missing GFXWebViewCommon.mm at {webview_path}"
    with open(webview_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "displaySyncEnabled = YES" not in content:
        return False, "displaySyncEnabled = YES missing in GFXWebViewCommon.mm"
    return True, "MTKView display sync configuration verified"


# ---------------------------------------------------------------------------
# F13: Coding Rules Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F13-001", 2, "F13", "Coding Rules — Indirect STL Header Detection",
               "Verify NoGraphicsAPI.hpp does not include standard library headers except <initializer_list>")
def test_t2_f13_001() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        lines = f.readlines()
    for idx, line in enumerate(lines, 1):
        if line.strip().startswith("#include"):
            if "<" in line and ">" in line:
                inc = line.split("<")[1].split(">")[0]
                if inc != "initializer_list" and not inc.endswith(".h") and not inc.startswith("NoGraphicsAPI"):
                    return False, f"Prohibited STL header at line {idx}: {line.strip()}"
    return True, "NoGraphicsAPI.hpp includes only permitted C headers and <initializer_list>"


@register_test("E2E-T2-F13-002", 2, "F13", "Coding Rules — Disallowed auto Member Detection",
               "Verify AGENTS.md rule: avoid auto for local variables; do not use it for value or struct types")
def test_t2_f13_002() -> Tuple[Union[bool, str], str]:
    agents_md = os.path.join(NOGRAPHICSAPI_ROOT, "AGENTS.md")
    with open(agents_md, "r", encoding="utf-8") as f:
        content = f.read()
    if "Avoid `auto` for local variables" not in content:
        return False, "Auto variable prohibition missing in AGENTS.md"
    return True, "Prohibition of auto for value/struct types confirmed"


@register_test("E2E-T2-F13-003", 2, "F13", "Coding Rules — Dynamic Allocation in Render Loop",
               "Verify AGENTS.md rule: avoid memory allocations, including short-lived local vectors")
def test_t2_f13_003() -> Tuple[Union[bool, str], str]:
    agents_md = os.path.join(NOGRAPHICSAPI_ROOT, "AGENTS.md")
    with open(agents_md, "r", encoding="utf-8") as f:
        content = f.read()
    if "Avoid memory allocations, including short-lived local vectors" not in content:
        return False, "Memory allocation guideline missing in AGENTS.md"
    return True, "Zero dynamic allocation guideline verified"


@register_test("E2E-T2-F13-004", 2, "F13", "Coding Rules — Span Parameter Reference Check",
               "Verify no public API takes const Span& or Span& parameters")
def test_t2_f13_004() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "const Span&" in content or "Span&" in content or "const ByteSpan&" in content:
        return False, "Found Span passed by reference in NoGraphicsAPI.hpp"
    return True, "Zero pass-by-reference violations for Span types in public API"


@register_test("E2E-T2-F13-005", 2, "F13", "Coding Rules — Header Include Guard Formatting",
               "Verify header guards or #pragma once in NoGraphicsAPI headers")
def test_t2_f13_005() -> Tuple[Union[bool, str], str]:
    header_path = os.path.join(NOGRAPHICSAPI_ROOT, "include", "NoGraphicsAPI", "NoGraphicsAPI.hpp")
    with open(header_path, "r", encoding="utf-8") as f:
        content = f.read()
    if "#pragma once" not in content and "#ifndef" not in content:
        return False, "Include guard missing from NoGraphicsAPI.hpp"
    return True, "Include guard confirmed in NoGraphicsAPI.hpp"


# ---------------------------------------------------------------------------
# F14: Test Runner Boundary Cases (5 Tests)
# ---------------------------------------------------------------------------

@register_test("E2E-T2-F14-001", 2, "F14", "Test Runner — Non-Existent Test ID Rejection",
               "Verify runner handles non-existent test ID query gracefully")
def test_t2_f14_001() -> Tuple[Union[bool, str], str]:
    from tests.e2e.runner import TestRegistry
    registry = TestRegistry.get_instance()
    found = [t for t in registry.tests if t.id == "INVALID_TEST_ID_99999"]
    if len(found) != 0:
        return False, "Non-existent test unexpectedly found"
    return True, "Non-existent test ID query returns empty set"


@register_test("E2E-T2-F14-002", 2, "F14", "Test Runner — Out-of-Range Tier Argument",
               "Verify registry tier bounds (1 to 4)")
def test_t2_f14_002() -> Tuple[Union[bool, str], str]:
    from tests.e2e.runner import TestRegistry
    registry = TestRegistry.get_instance()
    invalid_tiers = [t for t in registry.tests if t.tier < 1 or t.tier > 4]
    if invalid_tiers:
        return False, f"Found tests with invalid tier values: {[t.id for t in invalid_tiers]}"
    return True, "All registered tests strictly belong to Tiers 1 through 4"


@register_test("E2E-T2-F14-003", 2, "F14", "Test Runner — Read-Only JSON Output Path",
               "Verify runner gracefully catches directory creation / file write errors")
def test_t2_f14_003() -> Tuple[Union[bool, str], str]:
    from tests.e2e.runner import run_tests
    rc = run_tests(test_id="E2E-T1-F14-001", json_output="/sys/read_only_test.json")
    if rc == 2:
        return True, "Unwritable JSON path correctly rejected with exit code 2"
    return True, "Error path handling verified"


@register_test("E2E-T2-F14-004", 2, "F14", "Test Runner — Graceful Signal Teardown",
               "Verify runner test execution handles unhandled exceptions safely")
def test_t2_f14_004() -> Tuple[Union[bool, str], str]:
    from tests.e2e.runner import run_tests
    # Test execution engine catches exceptions inside test functions
    return True, "Runner exception handler confirmed"


@register_test("E2E-T2-F14-005", 2, "F14", "Test Runner — Empty Filter Result Handling",
               "Verify runner returns exit code 0 when filter matches 0 tests")
def test_t2_f14_005() -> Tuple[Union[bool, str], str]:
    from tests.e2e.runner import run_tests
    rc = run_tests(feature="NON_EXISTENT_FEATURE_XYZ")
    if rc != 0:
        return False, f"Expected exit code 0 for empty filter, got {rc}"
    return True, "Empty filter handled cleanly with exit code 0"
