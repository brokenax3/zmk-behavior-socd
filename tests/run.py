#!/usr/bin/env python3
"""Compile the actual SOCD driver using a temporary, minimal ZMK API surface."""

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = (
    "drivers/behavior.h",
    "zephyr/device.h",
    "zephyr/logging/log.h",
    "zephyr/sys/util.h",
    "zmk/behavior.h",
    "zmk/events/keycode_state_changed.h",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="socd-tests-") as directory:
        temporary = Path(directory)
        for header in HEADERS:
            path = temporary / header
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "test_zmk.h"\n')
        command = shlex.split(os.environ.get("CC", "cc"))
        command += ["-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g"]
        command += shlex.split(os.environ.get("CFLAGS", ""))
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        command += [
            f"-I{temporary}",
            f"-I{ROOT / 'tests/include'}",
            f"-I{ROOT / 'include'}",
            str(ROOT / "tests/test_socd.c"),
            "-o", str(temporary / "test_socd"),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(temporary / "test_socd")], check=True)


if __name__ == "__main__":
    main()
