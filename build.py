#!/usr/bin/env python3
"""Thin CMake wrapper for the root katane build."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build"
DEFAULT_MODE = "release"
VALID_MODES = ("debug", "release")
VALID_TOOLCHAINS = ("auto", "mingw", "msys", "msvc")


def run_command(command: list[str], *, verbose: bool) -> None:
    if verbose:
        print("[cmd]", " ".join(command))

    completed = subprocess.run(command, cwd=ROOT)
    if completed.returncode != 0:
        raise SystemExit(completed.returncode)


def require_cmake() -> str:
    cmake = shutil.which("cmake")
    if cmake is None:
        raise SystemExit(
            "CMake is required but was not found on PATH. "
            "Install CMake and rerun this command."
        )
    return cmake


def has_command(command: str) -> bool:
    return shutil.which(command) is not None


def remove_path(path: Path) -> None:
    if not path.exists():
        return

    try:
        if path.is_dir():
            shutil.rmtree(path)
        else:
            path.unlink()
    except PermissionError:
        if path.is_file() and path.name == "compile_commands.json":
            path.write_text("[]\n", encoding="utf-8")
            return
        raise


def clean() -> None:
    remove_path(BUILD_DIR)
    remove_path(ROOT / "compile_commands.json")


def sync_compile_commands(mode: str, toolchain: str) -> None:
    source = BUILD_DIR / toolchain / mode / "compile_commands.json"
    target = ROOT / "compile_commands.json"

    if not source.exists():
        return

    shutil.copy2(source, target)


def resolve_toolchain(requested_toolchain: str) -> str:
    if requested_toolchain != "auto":
        return requested_toolchain

    if sys.platform == "win32":
        if has_command("gcc") and has_command("g++") and (
            has_command("mingw32-make") or has_command("ninja")
        ):
            return "mingw"
        if has_command("gcc") and has_command("g++") and has_command("make"):
            return "msys"
        return "msvc"

    return "msys" if has_command("make") else "mingw"


def preset_name(mode: str, toolchain: str) -> str:
    return f"{toolchain}-{mode}"


def configure_and_build(*, cmake: str, mode: str, toolchain: str, verbose: bool) -> None:
    resolved_toolchain = resolve_toolchain(toolchain)
    preset = preset_name(mode, resolved_toolchain)

    print(f"[build] configuring {mode} ({resolved_toolchain})")
    run_command([cmake, "--preset", preset], verbose=verbose)

    sync_compile_commands(mode, resolved_toolchain)

    print(f"[build] building {mode} ({resolved_toolchain})")
    build_command = [cmake, "--build", "--preset", preset]
    if verbose:
        build_command.extend(["--verbose"])
    run_command(build_command, verbose=verbose)

    sync_compile_commands(mode, resolved_toolchain)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the root katane interpreter with CMake presets."
    )
    parser.add_argument(
        "--mode",
        choices=VALID_MODES,
        default=DEFAULT_MODE,
        help="Build mode to configure and build.",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove CMake build outputs and the generated compile_commands.json.",
    )
    parser.add_argument(
        "--rebuild",
        action="store_true",
        help="Clean first, then configure and build the selected mode.",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print the underlying CMake commands.",
    )
    parser.add_argument(
        "--toolchain",
        choices=VALID_TOOLCHAINS,
        default="auto",
        help="Compiler setup to use. 'auto' prefers MinGW/GCC on Windows when available.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if args.clean:
        print("[build] cleaning")
        clean()
        if not args.rebuild:
            return 0

    cmake = require_cmake()
    configure_and_build(
        cmake=cmake,
        mode=args.mode,
        toolchain=args.toolchain,
        verbose=args.verbose,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
