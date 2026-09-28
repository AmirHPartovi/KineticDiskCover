#!/usr/bin/env python3
"""Configure, build, test, and run the kdc-solver pre-flight checks."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
RESULTS = ROOT / "results" / "preflight"
REPORT = RESULTS / "preflight_report.md"
SUMMARY = RESULTS / "00_preflight_summary.md"


def run(command: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(command), flush=True)
    return subprocess.run(command, cwd=cwd, text=True, check=False)


def test_counts(output: str) -> tuple[int, int]:
    match = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", output)
    if match:
        failed = int(match.group(2))
        total = int(match.group(3))
        return total - failed, failed
    match = re.search(r"100% tests passed out of (\d+)", output)
    if match:
        return int(match.group(1)), 0
    if "No tests were found" in output:
        return 0, 1
    return 0, 1


def write_summary(status: dict[str, str], passed: int, failed: int,
                  instance_paths: list[Path], preflight_status: str) -> None:
    RESULTS.mkdir(parents=True, exist_ok=True)
    relative_report = REPORT.relative_to(ROOT)
    with SUMMARY.open("w", encoding="utf-8") as output:
        output.write("# Pre-flight Workflow Summary\n\n")
        output.write(f"- **Configure:** {status['configure']}\n")
        output.write(f"- **Build:** {status['build']}\n")
        output.write(f"- **Tests:** {status['tests']} ({passed} passed, "
                     f"{failed} failed)\n")
        output.write(f"- **Pre-flight command:** {preflight_status}\n")
        output.write(f"- **Pre-flight report:** `{relative_report}`\n")
        output.write(f"- **JSON dataset check:** "
                     f"{'PASS' if instance_paths else 'FAIL'}\n\n")
        output.write("## JSON instances found\n\n")
        if instance_paths:
            for path in instance_paths:
                output.write(f"- `{path.relative_to(ROOT)}`\n")
        else:
            output.write("No `.json` files found under `data/instances/`.\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clean", action="store_true",
                        help="remove the project's build/ directory first")
    parser.add_argument("--kont-root", default=os.environ.get("KONT_ROOT", ""),
                        help="KONT installation root (defaults to KONT_ROOT)")
    args = parser.parse_args()

    if args.clean and BUILD.exists():
        shutil.rmtree(BUILD)
    BUILD.mkdir(parents=True, exist_ok=True)
    RESULTS.mkdir(parents=True, exist_ok=True)

    status = {"configure": "FAIL", "build": "SKIPPED", "tests": "SKIPPED"}
    passed_tests = 0
    failed_tests = 0
    preflight_status = "SKIPPED"

    configure_command = [
        "cmake", "-S", str(ROOT), "-B", str(BUILD),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DKONT_ROOT={args.kont_root}",
    ]
    configure = run(configure_command, ROOT)
    if configure.returncode == 0:
        status["configure"] = "PASS"
        build = run(["cmake", "--build", str(BUILD), "--parallel",
                     str(max(1, os.cpu_count() or 1))], ROOT)
        status["build"] = "PASS" if build.returncode == 0 else "FAIL"

        if build.returncode == 0:
            test = subprocess.run(
                ["ctest", "--output-on-failure"], cwd=BUILD, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
            print(test.stdout, end="", flush=True)
            passed_tests, failed_tests = test_counts(test.stdout)
            status["tests"] = "PASS" if test.returncode == 0 else "FAIL"

            preflight = run(
                [str(BUILD / "kdc-solver"), "preflight", "--output",
                 "../results/preflight/preflight_report.md"],
                BUILD)
            preflight_status = "PASS" if preflight.returncode == 0 else "FAIL"

    instance_paths = sorted((ROOT / "data" / "instances").glob("*.json"))
    write_summary(status, passed_tests, failed_tests, instance_paths,
                  preflight_status)

    success = (
        status["configure"] == "PASS"
        and status["build"] == "PASS"
        and status["tests"] == "PASS"
        and preflight_status == "PASS"
        and bool(instance_paths)
    )
    print(f"Pre-flight workflow: {'PASS' if success else 'FAIL'}")
    print(f"Summary: {SUMMARY}")
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
