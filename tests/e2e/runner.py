#!/usr/bin/env python3
"""
Test Framework & Runner for NoGraphicsAPI WebGPU Backend E2E Test Suite.
"""

import sys
import os
import time
import json
import subprocess
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple, Dict, Any, Union

# Root directory paths
NOGRAPHICSAPI_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DAWN_DIR = "/Volumes/Acer/GFX_Engine/third_party/dawn"
DAWN_MAC_BUILD = "/Volumes/Acer/GFX_Engine/third_party/dawn/cmake-build-mac-release"
DAWN_WASM_BUILD = "/Volumes/Acer/GFX_Engine/third_party/dawn/cmake-build-wasm-release"
EMDAWNWEBGPU_PORT = os.path.join(DAWN_WASM_BUILD, "emdawnwebgpu_pkg", "emdawnwebgpu.port.py")
EMSDK_EMPP = os.path.join(DAWN_DIR, "third_party", "emsdk", "upstream", "emscripten", "em++")
GFX_RUNTIME_BIN = "/Volumes/Acer/GFX_Engine/GFX_Runtime/cmake-build-mac/Release/GFX_Runtime.app/Contents/MacOS/GFX_Runtime"
TEST_WEB_ROOT = "/Users/seto/TestWeb"
BUILD_WEB_ROOT = "/Users/seto/TestWeb/BuildWeb"


@dataclass
class TestCase:
    id: str
    tier: int
    feature: str
    title: str
    description: str
    func: Callable[[], Tuple[Union[bool, str, None], str]]
    is_slow: bool = False


@dataclass
class TestResult:
    test_id: str
    tier: int
    feature: str
    title: str
    status: str  # 'PASS', 'FAIL', 'SKIP'
    duration_s: float
    message: str = ""


class TestRegistry:
    _instance = None

    def __init__(self):
        self.tests: List[TestCase] = []

    @classmethod
    def get_instance(cls) -> "TestRegistry":
        if cls._instance is None:
            cls._instance = TestRegistry()
        return cls._instance

    def register(self, test: TestCase):
        for existing in self.tests:
            if existing.id == test.id:
                raise ValueError(f"Duplicate test ID registered: {test.id}")
        self.tests.append(test)


def register_test(test_id: str, tier: int, feature: str, title: str, description: str, is_slow: bool = False):
    def decorator(fn: Callable[[], Tuple[Union[bool, str, None], str]]):
        test_case = TestCase(
            id=test_id,
            tier=tier,
            feature=feature,
            title=title,
            description=description,
            func=fn,
            is_slow=is_slow,
        )
        TestRegistry.get_instance().register(test_case)
        return fn
    return decorator


class ANSI:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    GREEN = "\033[32m"
    RED = "\033[31m"
    YELLOW = "\033[33m"
    BLUE = "\033[34m"
    CYAN = "\033[36m"
    GRAY = "\033[90m"


def run_command(cmd: List[str], cwd: Optional[str] = None, timeout: int = 30) -> Tuple[int, str, str]:
    try:
        proc = subprocess.Popen(
            cmd,
            cwd=cwd or NOGRAPHICSAPI_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        stdout, stderr = proc.communicate(timeout=timeout)
        return proc.returncode, stdout, stderr
    except subprocess.TimeoutExpired:
        proc.kill()
        return -1, "", f"Command timed out after {timeout} seconds"
    except Exception as e:
        return -1, "", str(e)


def run_tests(
    tier: Optional[int] = None,
    feature: Optional[str] = None,
    test_id: Optional[str] = None,
    skip_builds: bool = False,
    verbose: bool = False,
    json_output: Optional[str] = None,
) -> int:
    registry = TestRegistry.get_instance()
    filtered_tests: List[TestCase] = []

    for t in registry.tests:
        if tier is not None and t.tier != tier:
            continue
        if feature is not None and t.feature.upper() != feature.upper():
            continue
        if test_id is not None and t.id.upper() != test_id.upper():
            continue
        if skip_builds and t.is_slow:
            continue
        filtered_tests.append(t)

    # Sort test cases logically: tier -> feature -> test ID
    def sort_key(t: TestCase):
        f_str = t.feature.upper()
        if f_str.startswith("F") and f_str[1:].isdigit():
            f_num = int(f_str[1:])
        elif f_str.startswith("R") and f_str[1:].isdigit():
            f_num = int(f_str[1:])
        else:
            f_num = 99
        return (t.tier, f_num, t.id)

    filtered_tests.sort(key=sort_key)

    total_count = len(filtered_tests)
    print(f"\n{ANSI.BOLD}{ANSI.CYAN}========================================================================{ANSI.RESET}")
    print(f"{ANSI.BOLD}  NoGraphicsAPI WebGPU Backend — 4-Tier E2E Test Suite{ANSI.RESET}")
    print(f"{ANSI.BOLD}{ANSI.CYAN}========================================================================{ANSI.RESET}")
    print(f"Target Repository: {NOGRAPHICSAPI_ROOT}")
    print(f"Selected Tests:    {total_count} / {len(registry.tests)} registered")
    if tier:
        print(f"Filter Tier:       Tier {tier}")
    if feature:
        print(f"Filter Feature:    {feature}")
    if test_id:
        print(f"Filter Test ID:    {test_id}")
    print(f"{ANSI.CYAN}------------------------------------------------------------------------{ANSI.RESET}\n")

    if total_count == 0:
        print(f"{ANSI.YELLOW}No tests matched the specified criteria.{ANSI.RESET}")
        return 0

    results: List[TestResult] = []
    passed = 0
    failed = 0
    skipped = 0
    start_total_time = time.time()

    current_tier = None
    for i, test in enumerate(filtered_tests, 1):
        if test.tier != current_tier:
            current_tier = test.tier
            tier_names = {
                1: "Tier 1: Feature Coverage",
                2: "Tier 2: Boundary & Corner Cases",
                3: "Tier 3: Cross-Feature Pairwise Integrations",
                4: "Tier 4: Real-World Application Scenarios",
            }
            print(f"\n{ANSI.BOLD}{ANSI.BLUE}[{tier_names.get(current_tier, f'Tier {current_tier}')}]{ANSI.RESET}")

        start_time = time.time()
        try:
            status_val, msg = test.func()
        except Exception as e:
            status_val = False
            msg = f"Unhandled Exception: {type(e).__name__}: {str(e)}"
        duration = time.time() - start_time

        if status_val is True:
            status = "PASS"
            passed += 1
            status_str = f"{ANSI.GREEN}[PASS]{ANSI.RESET}"
        elif status_val == "SKIP" or status_val is None:
            status = "SKIP"
            skipped += 1
            status_str = f"{ANSI.YELLOW}[SKIP]{ANSI.RESET}"
        else:
            status = "FAIL"
            failed += 1
            status_str = f"{ANSI.RED}[FAIL]{ANSI.RESET}"

        res = TestResult(
            test_id=test.id,
            tier=test.tier,
            feature=test.feature,
            title=test.title,
            status=status,
            duration_s=round(duration, 4),
            message=msg,
        )
        results.append(res)

        print(f"  {status_str} {test.id:<16} {test.title[:62]:<62} {ANSI.GRAY}({duration:.3f}s){ANSI.RESET}")
        if (verbose or status == "FAIL") and msg:
            color = ANSI.RED if status == "FAIL" else ANSI.GRAY
            for line in msg.split("\n"):
                if line.strip():
                    print(f"        {color}| {line}{ANSI.RESET}")

    total_duration = time.time() - start_total_time

    # Summary breakdown by tier
    tier_summary: Dict[int, Dict[str, int]] = {}
    for r in results:
        if r.tier not in tier_summary:
            tier_summary[r.tier] = {"PASS": 0, "SKIP": 0, "FAIL": 0}
        tier_summary[r.tier][r.status] += 1

    print(f"\n{ANSI.BOLD}{ANSI.CYAN}========================================================================{ANSI.RESET}")
    print(f"{ANSI.BOLD}  E2E Test Execution Summary{ANSI.RESET}")
    print(f"{ANSI.BOLD}{ANSI.CYAN}========================================================================{ANSI.RESET}")
    for t_num in sorted(tier_summary.keys()):
        stats = tier_summary[t_num]
        print(f"  Tier {t_num}:  PASS: {stats['PASS']:<3}  SKIP: {stats['SKIP']:<3}  FAIL: {stats['FAIL']:<3}")
    print(f"{ANSI.CYAN}------------------------------------------------------------------------{ANSI.RESET}")
    pass_color = ANSI.GREEN if failed == 0 else ANSI.RED
    print(f"  {ANSI.BOLD}Total: {total_count} | {pass_color}Passed: {passed}{ANSI.RESET} | "
          f"{ANSI.YELLOW}Skipped: {skipped}{ANSI.RESET} | {ANSI.RED}Failed: {failed}{ANSI.RESET} | Duration: {total_duration:.2f}s")
    print(f"{ANSI.BOLD}{ANSI.CYAN}========================================================================{ANSI.RESET}\n")

    if json_output:
        json_dir = os.path.dirname(os.path.abspath(json_output))
        if not os.path.exists(json_dir):
            try:
                os.makedirs(json_dir, exist_ok=True)
            except Exception as e:
                print(f"{ANSI.RED}Failed to create JSON report directory: {e}{ANSI.RESET}")
                return 2

        report_data = {
            "summary": {
                "total": total_count,
                "passed": passed,
                "skipped": skipped,
                "failed": failed,
                "duration_seconds": round(total_duration, 4),
                "tier_breakdown": {str(k): v for k, v in tier_summary.items()},
            },
            "results": [
                {
                    "id": r.test_id,
                    "tier": r.tier,
                    "feature": r.feature,
                    "title": r.title,
                    "status": r.status,
                    "duration_s": r.duration_s,
                    "message": r.message,
                }
                for r in results
            ],
        }
        try:
            with open(json_output, "w", encoding="utf-8") as f:
                json.dump(report_data, f, indent=2)
            print(f"JSON test results report written to: {json_output}")
        except Exception as e:
            print(f"{ANSI.RED}Failed to write JSON report: {e}{ANSI.RESET}")
            return 2

    return 0 if failed == 0 else 1
