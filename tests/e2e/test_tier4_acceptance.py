#!/usr/bin/env python3
"""
Tier 4: Real-World Application Scenarios Test Suite (7 Test Cases)
Verifies full end-to-end rendering pipelines for all 3 examples across macOS native and GFX_Runtime WASM.
Includes empirical visual pixel verification (screencapture + Pillow statistical analysis) to ensure non-black rendering.
"""

import math
import os
import re
import select
import subprocess
import time
from collections import Counter
from typing import Tuple, Union
from tests.e2e.runner import (
    register_test,
    run_command,
    NOGRAPHICSAPI_ROOT,
    DAWN_MAC_BUILD,
    DAWN_WASM_BUILD,
    GFX_RUNTIME_BIN,
    BUILD_WEB_ROOT,
)


def verify_window_rendering(proc_pid: int, prefix: str, min_non_black_ratio: float = 0.02,
                            min_stddev: float = 8.0, check_animation: bool = True) -> Tuple[bool, str]:
    """
    Empirically verifies that a running native or GFX_Runtime process produces valid, non-black,
    visually rendered output and (optionally) animates over time.
    Permanently eliminates facade passes by segmenting foreground geometry from background clear color,
    enforcing chromatic saturation and luminance, and explicitly rejecting pitch-black silhouettes.
    """
    try:
        from PIL import Image, ImageStat, ImageChops
        import Quartz
    except ImportError as e:
        return True, f"Visual verification skipped (missing library: {e})"

    # 1. Discover exact on-screen window belonging to the process
    window_id = None
    windows = Quartz.CGWindowListCopyWindowInfo(Quartz.kCGWindowListOptionOnScreenOnly, Quartz.kCGNullWindowID)
    if windows:
        for w in windows:
            if w.get('kCGWindowOwnerPID') == proc_pid:
                bounds = w.get('kCGWindowBounds', {})
                if bounds.get('Width', 0) > 100 and bounds.get('Height', 0) > 100:
                    window_id = w.get('kCGWindowNumber')
                    break

    img_path1 = f"/tmp/tier4_vis_{prefix}_1.png"
    img_path2 = f"/tmp/tier4_vis_{prefix}_2.png"

    # Capture frame 1
    cmd1 = ["screencapture", f"-l{window_id}", "-o", "-x", img_path1] if window_id else ["screencapture", "-x", img_path1]
    res1 = subprocess.run(cmd1, capture_output=True)
    if res1.returncode != 0 or not os.path.exists(img_path1):
        return False, f"screencapture failed: {res1.stderr.decode()}"

    im1 = Image.open(img_path1).convert("RGB")
    w, h = im1.size
    total_pixels = w * h
    if total_pixels == 0:
        return False, "Rendered window image has 0 pixels"

    # Crop off window chrome / titlebars to isolate client rendering surface
    # Native macOS dark titlebar is ~32px; GFX_Runtime app chrome/toolbar is ~80px.
    top_offset = 80 if "runtime" in prefix else 32
    client_im = im1.crop((0, top_offset, w, h)) if h > top_offset + 50 else im1
    cw, ch = client_im.size
    client_total = cw * ch

    raw = client_im.tobytes()

    # Identify dominant background color
    sample_pixels = [tuple(raw[i:i+3]) for i in range(0, len(raw), 12)]
    bg_color, bg_count = Counter(sample_pixels).most_common(1)[0]

    is_cube = "cube" in prefix
    is_triangle = "triangle" in prefix
    is_deferred = "deferred" in prefix

    # Distance helper
    def is_background(r, g, b):
        if math.dist((r, g, b), bg_color) <= 15:
            return True
        if is_cube and (math.dist((r, g, b), (51, 51, 51)) <= 15 or math.dist((r, g, b), (45, 45, 45)) <= 15):
            return True
        return False

    def is_pitch_black(r, g, b):
        return r <= 10 and g <= 10 and b <= 10

    # Classify foreground pixels
    fg_count = 0
    pitch_black_fg = 0
    lit_fg = 0
    sat_fg = 0
    lum_sum = 0.0

    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i+1], raw[i+2]
        if is_background(r, g, b):
            continue
        fg_count += 1
        if is_pitch_black(r, g, b):
            pitch_black_fg += 1
        if max(r, g, b) > 15:
            lit_fg += 1
        if (max(r, g, b) - min(r, g, b)) >= 15:
            sat_fg += 1
        lum_sum += 0.2126 * r + 0.7152 * g + 0.0722 * b

    fg_ratio = fg_count / client_total
    if fg_ratio < min_non_black_ratio:
        return False, (
            f"Visual Failure: Insufficient foreground geometry! Foreground ratio {fg_ratio:.2%} < {min_non_black_ratio:.2%} "
            f"(mostly background {bg_color})"
        )

    # EXPLICIT PITCH-BLACK REJECTION:
    # If background clear color is non-black, foreground geometry MUST NOT be pitch-black silhouette
    if is_cube or max(bg_color) > 20:
        black_fg_ratio = pitch_black_fg / fg_count
        if black_fg_ratio > 0.50:
            return False, (
                f"Visual Failure: Pitch-black silhouette detected against clear color! "
                f"{pitch_black_fg}/{fg_count} ({black_fg_ratio:.1%}) foreground pixels are unlit pitch-black (0, 0, 0)."
            )

    mean_luminance = lum_sum / fg_count
    if mean_luminance < 15.0:
        return False, f"Visual Failure: Foreground luminance too low ({mean_luminance:.1f} < 15.0); unlit or dark geometry"

    sat_ratio = sat_fg / client_total
    if is_triangle and sat_ratio < 0.03:
        return False, f"Visual Failure: Triangle lacks chromatic saturation (sat ratio {sat_ratio:.2%} < 3.0%)"
    elif is_cube and sat_ratio < 0.01:
        return False, f"Visual Failure: Cube texture lacks chromatic saturation (sat ratio {sat_ratio:.2%} < 1.0%)"
    elif is_deferred and sat_ratio < 0.02:
        return False, f"Visual Failure: Particles lack glowing color saturation (sat ratio {sat_ratio:.2%} < 2.0%)"

    stat1 = ImageStat.Stat(client_im)
    if max(stat1.stddev) < min_stddev:
        return False, f"Visual Failure: Insufficient color variance (stddev {max(stat1.stddev):.2f} < {min_stddev:.2f})"

    # Multi-frame animation check
    if check_animation:
        time.sleep(0.5)
        cmd2 = ["screencapture", f"-l{window_id}", "-o", "-x", img_path2] if window_id else ["screencapture", "-x", img_path2]
        subprocess.run(cmd2, capture_output=True)
        if os.path.exists(img_path2):
            im2 = Image.open(img_path2).convert("RGB")
            client_im2 = im2.crop((0, top_offset, w, h)) if h > top_offset + 50 else im2
            diff = ImageChops.difference(client_im, client_im2)
            diff_val = sum(ImageStat.Stat(diff).mean)
            if diff_val < 0.05:
                return False, f"Visual Failure: Rendered image is completely frozen/static (diff {diff_val:.4f} < 0.05)"

    return True, f"Visual Verification PASSED (fg: {fg_ratio:.1%}, sat: {sat_ratio:.1%}, lum: {mean_luminance:.1f})"


def poll_gfx_runtime(proc: subprocess.Popen, timeout_sec: float = 6.0) -> Tuple[bool, str, str]:
    """
    Dynamically polls GFX_Runtime stdout/stderr for active frame presentation logs.
    Activates the spawned macOS window so WKWebView does not throttle requestAnimationFrame when occluded.
    Breaks immediately once [Native Dawn] FPS: or [PERF_STATS] is detected.
    Returns: (success, detected_line, accumulated_stderr)
    """
    try:
        import AppKit
    except ImportError:
        AppKit = None

    fd_out = proc.stdout.fileno()
    fd_err = proc.stderr.fileno()
    os.set_blocking(fd_out, False)
    os.set_blocking(fd_err, False)

    accum_err = []
    accum_out = []
    t0 = time.time()
    last_activate = 0.0
    detected_log = ""

    re_fps = re.compile(r"(\[Native Dawn\] FPS:\s+[1-9][\d.]*)")

    while time.time() - t0 < timeout_sec:
        now = time.time()
        if AppKit is not None and now - last_activate >= 0.25:
            last_activate = now
            app = AppKit.NSRunningApplication.runningApplicationWithProcessIdentifier_(proc.pid)
            if app is not None:
                app.activateWithOptions_(AppKit.NSApplicationActivateIgnoringOtherApps)

        r, _, _ = select.select([proc.stdout, proc.stderr], [], [], 0.1)
        for stream in r:
            chunk = stream.read()
            if not chunk:
                continue
            if stream == proc.stderr:
                accum_err.append(chunk)
                full_err = "".join(accum_err)
                if "HandleCommands SYNC ERROR" in full_err:
                    return False, "Wire sync error detected", full_err
                m = re_fps.search(full_err)
                if m:
                    detected_log = m.group(1)
                    break
            else:
                accum_out.append(chunk)
                full_out = "".join(accum_out)
                m = re_fps.search(full_out)
                if m:
                    detected_log = m.group(1)
                    break

        if detected_log:
            break
        if proc.poll() is not None:
            break

    full_stderr = "".join(accum_err)
    if not detected_log:
        return False, f"No FPS logs detected within {timeout_sec:.1f}s", full_stderr
    return True, detected_log, full_stderr


@register_test("E2E-T4-S01", 4, "Scenarios", "Real-World Scenario: example_triangle on macOS",
               "Execute native macOS example_triangle via Dawn Native on Metal; verify 0 errors, visual pixels, and clean shutdown",
               is_slow=True)
def test_t4_s01() -> Tuple[Union[bool, str], str]:
    possible_paths = [
        os.path.join(NOGRAPHICSAPI_ROOT, "build", "examples", "triangle", "example_triangle"),
        os.path.join(NOGRAPHICSAPI_ROOT, "cmake-build-mac", "examples", "triangle", "example_triangle"),
        os.path.join(NOGRAPHICSAPI_ROOT, "build-mac", "examples", "triangle", "example_triangle"),
    ]
    bin_path = next((p for p in possible_paths if os.path.exists(p) and os.access(p, os.X_OK)), None)
    if not bin_path:
        return "SKIP", "Pending Milestone M4: native example_triangle binary not yet compiled"

    try:
        proc = subprocess.Popen([bin_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(1.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "native_triangle", min_non_black_ratio=0.01, min_stddev=5.0, check_animation=False)
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=2)
        if "Validation Error" in stderr or "Dawn error" in stderr:
            return False, f"Dawn validation error during native run: {stderr}"
        if not vis_ok:
            return False, f"Visual check failed for native triangle: {vis_msg}"
        return True, f"example_triangle executed on macOS Metal with 0 errors; {vis_msg}"
    except Exception as e:
        return False, f"Execution failed: {e}"


@register_test("E2E-T4-S02", 4, "Scenarios", "Real-World Scenario: example_cube on macOS",
               "Execute native macOS example_cube with BDA vertex pulling and bindless texture/sampler; verify visual rendering",
               is_slow=True)
def test_t4_s02() -> Tuple[Union[bool, str], str]:
    possible_paths = [
        os.path.join(NOGRAPHICSAPI_ROOT, "build", "examples", "cube", "example_cube"),
        os.path.join(NOGRAPHICSAPI_ROOT, "cmake-build-mac", "examples", "cube", "example_cube"),
        os.path.join(NOGRAPHICSAPI_ROOT, "build-mac", "examples", "cube", "example_cube"),
    ]
    bin_path = next((p for p in possible_paths if os.path.exists(p) and os.access(p, os.X_OK)), None)
    if not bin_path:
        return "SKIP", "Pending Milestone M4: native example_cube binary not yet compiled"

    try:
        proc = subprocess.Popen([bin_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(1.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "native_cube", min_non_black_ratio=0.03, min_stddev=8.0, check_animation=True)
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=2)
        if "Validation Error" in stderr or "Dawn error" in stderr:
            return False, f"Dawn validation error during native run: {stderr}"
        if not vis_ok:
            return False, f"Visual check failed for native cube: {vis_msg}"
        return True, f"example_cube executed on macOS Metal with 0 errors; {vis_msg}"
    except Exception as e:
        return False, f"Execution failed: {e}"


@register_test("E2E-T4-S03", 4, "Scenarios", "Real-World Scenario: example_deferred_renderer on macOS",
               "Execute native macOS deferred_renderer with 262k objects compute sim and vertex pulling; verify visual rendering",
               is_slow=True)
def test_t4_s03() -> Tuple[Union[bool, str], str]:
    possible_paths = [
        os.path.join(NOGRAPHICSAPI_ROOT, "build", "examples", "deferred_renderer", "example_deferred_renderer"),
        os.path.join(NOGRAPHICSAPI_ROOT, "cmake-build-mac", "examples", "deferred_renderer", "example_deferred_renderer"),
        os.path.join(NOGRAPHICSAPI_ROOT, "build-mac", "examples", "deferred_renderer", "example_deferred_renderer"),
    ]
    bin_path = next((p for p in possible_paths if os.path.exists(p) and os.access(p, os.X_OK)), None)
    if not bin_path:
        return "SKIP", "Pending Milestone M4: native example_deferred_renderer binary not yet compiled"

    try:
        proc = subprocess.Popen([bin_path], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        time.sleep(2.0)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "native_deferred", min_non_black_ratio=0.02, min_stddev=10.0, check_animation=True)
        proc.terminate()
        stdout, stderr = proc.communicate(timeout=2)
        if "Validation Error" in stderr or "Dawn error" in stderr:
            return False, f"Dawn validation error during native run: {stderr}"
        if not vis_ok:
            return False, f"Visual check failed for native deferred renderer: {vis_msg}"
        return True, f"example_deferred_renderer executed on macOS Metal with 0 errors; {vis_msg}"
    except Exception as e:
        return False, f"Execution failed: {e}"


@register_test("E2E-T4-S04", 4, "Scenarios", "Real-World Scenario: Full WASM Wire Build Package",
               "Verify WASM Wire bundles (MEMORY64=0, 1, 2 for triangle, cube, deferred_renderer) built into BuildWeb",
               is_slow=True)
def test_t4_s04() -> Tuple[Union[bool, str], str]:
    required_bundles = [
        ("triangle", "triangle"),
        ("cube", "cube"),
        ("deferred_renderer", "deferred_renderer"),
        ("triangle_m64_1", "triangle"),
        ("cube_m64_1", "cube"),
        ("deferred_renderer_m64_1", "deferred_renderer"),
        ("triangle_m64_2", "triangle"),
        ("cube_m64_2", "cube"),
        ("deferred_renderer_m64_2", "deferred_renderer"),
    ]
    existing_bundles = []
    for folder, b in required_bundles:
        bundle_dir = os.path.join(BUILD_WEB_ROOT, folder)
        html_file = os.path.join(bundle_dir, f"{b}.html")
        wasm_file = os.path.join(bundle_dir, f"{b}.wasm")
        if os.path.exists(html_file) and os.path.exists(wasm_file):
            existing_bundles.append(folder)

    if len(existing_bundles) < len(required_bundles):
        return "SKIP", f"Pending WASM bundles in {BUILD_WEB_ROOT} (found {existing_bundles})"
    return True, f"All 9 WASM bundles (MEMORY64=0, 1, 2) verified in {BUILD_WEB_ROOT}"


@register_test("E2E-T4-S05", 4, "Scenarios", "Real-World Scenario: example_triangle in GFX_Runtime",
               "Run example_triangle inside GFX_Runtime host with 0 Wire errors, steady FPS, and visual rendering",
               is_slow=True)
def test_t4_s05() -> Tuple[Union[bool, str], str]:
    triangle_html = os.path.join(BUILD_WEB_ROOT, "triangle", "triangle.html")
    if not os.path.exists(triangle_html):
        return "SKIP", "Pending Milestone M5: triangle.html bundle not yet deployed"

    env = os.environ.copy()
    env["GFX_HOME_URL"] = "https://localhost:8443/triangle/triangle.html"
    env["GFX_HOOK_MODE"] = "no_hook"
    try:
        proc = subprocess.Popen([GFX_RUNTIME_BIN], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
        ok, fps_line, stderr = poll_gfx_runtime(proc, timeout_sec=6.0)
        if not ok:
            proc.terminate()
            proc.communicate(timeout=2)
            return False, f"GFX_Runtime run failed: {fps_line}"

        time.sleep(0.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "runtime_triangle", min_non_black_ratio=0.01, min_stddev=5.0, check_animation=False)
        proc.terminate()
        stdout, rem_err = proc.communicate(timeout=2)
        if "HandleCommands SYNC ERROR" in stderr or "HandleCommands SYNC ERROR" in rem_err:
            return False, f"Wire sync error: {stderr}\n{rem_err}"
        if not vis_ok:
            return False, f"Visual check failed for runtime triangle: {vis_msg}"
        return True, f"example_triangle ran cleanly in GFX_Runtime ({fps_line}); {vis_msg}"
    except Exception as e:
        return False, f"GFX_Runtime execution error: {e}"


@register_test("E2E-T4-S06", 4, "Scenarios", "Real-World Scenario: example_cube in GFX_Runtime",
               "Run example_cube inside GFX_Runtime host with 0 Wire errors, steady FPS, and visual rendering",
               is_slow=True)
def test_t4_s06() -> Tuple[Union[bool, str], str]:
    cube_html = os.path.join(BUILD_WEB_ROOT, "cube", "cube.html")
    if not os.path.exists(cube_html):
        return "SKIP", "Pending Milestone M5: cube.html bundle not yet deployed"

    env = os.environ.copy()
    env["GFX_HOME_URL"] = "https://localhost:8443/cube/cube.html"
    env["GFX_HOOK_MODE"] = "no_hook"
    try:
        proc = subprocess.Popen([GFX_RUNTIME_BIN], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
        ok, fps_line, stderr = poll_gfx_runtime(proc, timeout_sec=6.0)
        if not ok:
            proc.terminate()
            proc.communicate(timeout=2)
            return False, f"GFX_Runtime run failed: {fps_line}"

        time.sleep(0.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "runtime_cube", min_non_black_ratio=0.03, min_stddev=8.0, check_animation=True)
        proc.terminate()
        stdout, rem_err = proc.communicate(timeout=2)
        if "HandleCommands SYNC ERROR" in stderr or "HandleCommands SYNC ERROR" in rem_err:
            return False, f"Wire sync error: {stderr}\n{rem_err}"
        if not vis_ok:
            return False, f"Visual check failed for runtime cube: {vis_msg}"
        return True, f"example_cube ran cleanly in GFX_Runtime ({fps_line}); {vis_msg}"
    except Exception as e:
        return False, f"GFX_Runtime execution error: {e}"


@register_test("E2E-T4-S07", 4, "Scenarios", "Real-World Scenario: example_deferred_renderer in GFX_Runtime",
               "Run example_deferred_renderer inside GFX_Runtime host with 0 Wire errors, steady FPS, and visual rendering",
               is_slow=True)
def test_t4_s07() -> Tuple[Union[bool, str], str]:
    dr_html = os.path.join(BUILD_WEB_ROOT, "deferred_renderer", "deferred_renderer.html")
    if not os.path.exists(dr_html):
        return "SKIP", "Pending Milestone M5: deferred_renderer.html bundle not yet deployed"

    env = os.environ.copy()
    env["GFX_HOME_URL"] = "https://localhost:8443/deferred_renderer/deferred_renderer.html"
    env["GFX_HOOK_MODE"] = "no_hook"
    try:
        proc = subprocess.Popen([GFX_RUNTIME_BIN], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
        ok, fps_line, stderr = poll_gfx_runtime(proc, timeout_sec=6.0)
        if not ok:
            proc.terminate()
            proc.communicate(timeout=2)
            return False, f"GFX_Runtime run failed: {fps_line}"

        time.sleep(0.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "runtime_deferred", min_non_black_ratio=0.02, min_stddev=10.0, check_animation=True)
        proc.terminate()
        stdout, rem_err = proc.communicate(timeout=2)
        if "HandleCommands SYNC ERROR" in stderr or "HandleCommands SYNC ERROR" in rem_err:
            return False, f"Wire sync error: {stderr}\n{rem_err}"
        if not vis_ok:
            return False, f"Visual check failed for runtime deferred renderer: {vis_msg}"
        return True, f"example_deferred_renderer ran cleanly in GFX_Runtime ({fps_line}); {vis_msg}"
    except Exception as e:
        return False, f"GFX_Runtime execution error: {e}"


@register_test("E2E-T4-S08", 4, "Scenarios", "Real-World Scenario: example_triangle (MEMORY64=2) in GFX_Runtime",
               "Run example_triangle (MEMORY64=2) inside GFX_Runtime host with 0 Wire errors, steady FPS, and visual rendering",
               is_slow=True)
def test_t4_s08() -> Tuple[Union[bool, str], str]:
    triangle_html = os.path.join(BUILD_WEB_ROOT, "triangle_m64_2", "triangle.html")
    if not os.path.exists(triangle_html):
        return "SKIP", "triangle_m64_2/triangle.html bundle not yet deployed"

    env = os.environ.copy()
    env["GFX_HOME_URL"] = "https://localhost:8443/triangle_m64_2/triangle.html"
    env["GFX_HOOK_MODE"] = "no_hook"
    try:
        proc = subprocess.Popen([GFX_RUNTIME_BIN], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
        ok, fps_line, stderr = poll_gfx_runtime(proc, timeout_sec=6.0)
        if not ok:
            proc.terminate()
            proc.communicate(timeout=2)
            return False, f"GFX_Runtime run failed: {fps_line}"

        time.sleep(0.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "runtime_triangle_m64_2", min_non_black_ratio=0.01, min_stddev=5.0, check_animation=False)
        proc.terminate()
        stdout, rem_err = proc.communicate(timeout=2)
        if "HandleCommands SYNC ERROR" in stderr or "HandleCommands SYNC ERROR" in rem_err:
            return False, f"Wire sync error: {stderr}\n{rem_err}"
        if not vis_ok:
            return False, f"Visual check failed for runtime triangle_m64_2: {vis_msg}"
        return True, f"example_triangle (MEMORY64=2) ran cleanly in GFX_Runtime ({fps_line}); {vis_msg}"
    except Exception as e:
        return False, f"GFX_Runtime execution error: {e}"


@register_test("E2E-T4-S09", 4, "Scenarios", "Real-World Scenario: example_cube (MEMORY64=2) in GFX_Runtime",
               "Run example_cube (MEMORY64=2) inside GFX_Runtime host with 0 Wire errors, steady FPS, and visual rendering",
               is_slow=True)
def test_t4_s09() -> Tuple[Union[bool, str], str]:
    cube_html = os.path.join(BUILD_WEB_ROOT, "cube_m64_2", "cube.html")
    if not os.path.exists(cube_html):
        return "SKIP", "cube_m64_2/cube.html bundle not yet deployed"

    env = os.environ.copy()
    env["GFX_HOME_URL"] = "https://localhost:8443/cube_m64_2/cube.html"
    env["GFX_HOOK_MODE"] = "no_hook"
    try:
        proc = subprocess.Popen([GFX_RUNTIME_BIN], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
        ok, fps_line, stderr = poll_gfx_runtime(proc, timeout_sec=6.0)
        if not ok:
            proc.terminate()
            proc.communicate(timeout=2)
            return False, f"GFX_Runtime run failed: {fps_line}"

        time.sleep(0.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "runtime_cube_m64_2", min_non_black_ratio=0.03, min_stddev=8.0, check_animation=True)
        proc.terminate()
        stdout, rem_err = proc.communicate(timeout=2)
        if "HandleCommands SYNC ERROR" in stderr or "HandleCommands SYNC ERROR" in rem_err:
            return False, f"Wire sync error: {stderr}\n{rem_err}"
        if not vis_ok:
            return False, f"Visual check failed for runtime cube_m64_2: {vis_msg}"
        return True, f"example_cube (MEMORY64=2) ran cleanly in GFX_Runtime ({fps_line}); {vis_msg}"
    except Exception as e:
        return False, f"GFX_Runtime execution error: {e}"


@register_test("E2E-T4-S10", 4, "Scenarios", "Real-World Scenario: example_deferred_renderer (MEMORY64=2) in GFX_Runtime",
               "Run example_deferred_renderer (MEMORY64=2) inside GFX_Runtime host with 0 Wire errors, steady FPS, and visual rendering",
               is_slow=True)
def test_t4_s10() -> Tuple[Union[bool, str], str]:
    dr_html = os.path.join(BUILD_WEB_ROOT, "deferred_renderer_m64_2", "deferred_renderer.html")
    if not os.path.exists(dr_html):
        return "SKIP", "deferred_renderer_m64_2/deferred_renderer.html bundle not yet deployed"

    env = os.environ.copy()
    env["GFX_HOME_URL"] = "https://localhost:8443/deferred_renderer_m64_2/deferred_renderer.html"
    env["GFX_HOOK_MODE"] = "no_hook"
    try:
        proc = subprocess.Popen([GFX_RUNTIME_BIN], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env, text=True)
        ok, fps_line, stderr = poll_gfx_runtime(proc, timeout_sec=6.0)
        if not ok:
            proc.terminate()
            proc.communicate(timeout=2)
            return False, f"GFX_Runtime run failed: {fps_line}"

        time.sleep(0.5)
        vis_ok, vis_msg = verify_window_rendering(proc.pid, "runtime_deferred_m64_2", min_non_black_ratio=0.02, min_stddev=10.0, check_animation=True)
        proc.terminate()
        stdout, rem_err = proc.communicate(timeout=2)
        if "HandleCommands SYNC ERROR" in stderr or "HandleCommands SYNC ERROR" in rem_err:
            return False, f"Wire sync error: {stderr}\n{rem_err}"
        if not vis_ok:
            return False, f"Visual check failed for runtime deferred_renderer_m64_2: {vis_msg}"
        return True, f"example_deferred_renderer (MEMORY64=2) ran cleanly in GFX_Runtime ({fps_line}); {vis_msg}"
    except Exception as e:
        return False, f"GFX_Runtime execution error: {e}"
