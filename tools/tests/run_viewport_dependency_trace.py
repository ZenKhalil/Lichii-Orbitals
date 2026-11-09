#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile the actual observer header and test nine fresh Windows environments."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="clang-cl", help="clang-cl path or command on PATH")
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="Parent for a unique retained test-results directory")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("This runner targets Windows and requires the x64 MSVC developer environment")
    compiler = shutil.which(args.compiler)
    if not compiler:
        parser.error("Compiler not found; specify --compiler or add clang-cl to PATH")
    root = Path(__file__).resolve().parents[2]
    source = Path(__file__).with_name("viewport_dependency_trace.cpp")
    header = root / "src/video_core/renderer_vulkan/viewport_dependency_trace.h"
    args.build_dir.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="viewport-observer-", dir=args.build_dir.resolve()))
    exe = output / "observer.exe"
    command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2", "/MD", "/W4", "/WX",
               "/D_CRT_SECURE_NO_WARNINGS", "/I" + str(root / "src"),
               "/Fe" + str(exe), "/Fo" + str(output / "observer.obj"), str(source)]
    inputs = {str(path.relative_to(root)): digest(path) for path in (source, header)}
    receipt = {"inputs": inputs, "command": command, "cases": [], "success": False}
    try:
        with (output / "compile.log").open("w") as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=40, check=True)
        receipt["exe_sha256"] = digest(exe)
        for index, value in enumerate((None, "", "0", "1", "01", "10", "true", " 1", "1 ")):
            env = os.environ.copy()
            env.pop("EDEN_NX2_VIEWPORT_DEPENDENCY_TRACE", None)
            if value is not None:
                env["EDEN_NX2_VIEWPORT_DEPENDENCY_TRACE"] = value
            child = subprocess.run([str(exe), "1" if value == "1" else "0"], env=env,
                                   capture_output=True, text=True, timeout=10)
            text = child.stdout + child.stderr
            (output / f"case-{index}.log").write_text(text)
            receipt["cases"].append({"value": value, "exit": child.returncode, "output": text})
            if child.returncode or "checks=157 failed=0" not in text:
                raise RuntimeError(f"Observer checks failed for environment variant {index}")
        for relative, expected in inputs.items():
            if digest(root / relative) != expected:
                raise RuntimeError(f"Input changed during the run: {relative}")
        receipt["success"] = True
    finally:
        (output / "results.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(output)


if __name__ == "__main__":
    main()
