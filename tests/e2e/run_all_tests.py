#!/usr/bin/env python3
"""
CLI Runner for the NoGraphicsAPI WebGPU Backend 4-Tier E2E Test Suite.
"""

import sys
import os
import argparse

# Add repository root and tests/e2e directory to python path
current_dir = os.path.dirname(os.path.abspath(__file__))
repo_root = os.path.abspath(os.path.join(current_dir, "..", ".."))
if repo_root not in sys.path:
    sys.path.insert(0, repo_root)
if current_dir not in sys.path:
    sys.path.insert(0, current_dir)

# Import runner framework
from tests.e2e.runner import run_tests, TestRegistry

# Import all test suites to register all 159 test cases
import tests.e2e.test_tier1_features
import tests.e2e.test_tier2_boundaries
import tests.e2e.test_tier3_integration
import tests.e2e.test_tier4_acceptance


def main():
    parser = argparse.ArgumentParser(
        description="E2E Test Runner for NoGraphicsAPI WebGPU Backend",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--tier",
        type=int,
        choices=[1, 2, 3, 4],
        help="Run only tests belonging to the specified tier (1, 2, 3, or 4)",
    )
    parser.add_argument(
        "--feature",
        type=str,
        help="Run only tests for a specific feature (e.g. F01, F02, ... F14, Integration, Scenarios)",
    )
    parser.add_argument(
        "--test",
        "--id",
        type=str,
        dest="test_id",
        help="Run a specific test case by ID (e.g. E2E-T1-F01-001)",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List all registered test cases and exit",
    )
    parser.add_argument(
        "--quick",
        "--skip-builds",
        dest="skip_builds",
        action="store_true",
        help="Skip slow compilation/execution tests",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Print verbose diagnostic details for each test",
    )
    parser.add_argument(
        "--json-output",
        type=str,
        help="Path to write JSON test results report",
    )

    args = parser.parse_args()

    registry = TestRegistry.get_instance()

    if args.list:
        print(f"\nRegistered Test Cases ({len(registry.tests)} total):")
        print(f"{'Test ID':<18} | {'Tier':<6} | {'Feature':<12} | {'Title'}")
        print("-" * 90)

        def sort_key(t):
            f_str = t.feature.upper()
            if f_str.startswith("F") and f_str[1:].isdigit():
                f_num = int(f_str[1:])
            else:
                f_num = 99
            return (t.tier, f_num, t.id)

        for t in sorted(registry.tests, key=sort_key):
            print(f"{t.id:<18} | Tier {t.tier:<1} | {t.feature:<12} | {t.title}")
        print(f"\nTotal: {len(registry.tests)} registered test cases.\n")
        return 0

    exit_code = run_tests(
        tier=args.tier,
        feature=args.feature,
        test_id=args.test_id,
        skip_builds=args.skip_builds,
        verbose=args.verbose,
        json_output=args.json_output,
    )
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
