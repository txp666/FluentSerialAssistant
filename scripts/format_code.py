#!/usr/bin/env python3

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".m", ".mm"}
EXCLUDED_DIRS = {
    ".git",
    ".github",
    "build",
    "dist",
    "third_party",
}


def iter_source_files(paths):
    for raw_path in paths:
        path = (REPO_ROOT / raw_path).resolve() if not Path(raw_path).is_absolute() else Path(raw_path).resolve()
        if not path.exists():
            raise FileNotFoundError(path)
        if path.is_file():
            if path.suffix in SOURCE_SUFFIXES:
                yield path
            continue
        for child in sorted(path.rglob("*")):
            if not child.is_file() or child.suffix not in SOURCE_SUFFIXES:
                continue
            relative_parts = child.relative_to(REPO_ROOT).parts
            if any(part in EXCLUDED_DIRS for part in relative_parts):
                continue
            yield child


def main():
    parser = argparse.ArgumentParser(description="Format C/C++ source files with clang-format.")
    parser.add_argument("paths", nargs="*", default=["src"], help="Files or directories to format.")
    parser.add_argument("--check", action="store_true", help="Check formatting without modifying files.")
    args = parser.parse_args()

    clang_format = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not clang_format:
        print("clang-format was not found in PATH.", file=sys.stderr)
        return 1

    files = list(dict.fromkeys(iter_source_files(args.paths)))
    if not files:
        return 0

    command = [clang_format, "--dry-run", "--Werror"] if args.check else [clang_format, "-i"]
    result = subprocess.run(command + [str(path) for path in files], cwd=REPO_ROOT)
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
