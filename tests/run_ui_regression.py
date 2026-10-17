#!/usr/bin/env python3
"""Link the headless UI regression against an existing Unix Makefiles build."""

import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def main():
    root = Path(__file__).resolve().parent.parent
    build = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build"
    target = build / "CMakeFiles/nchat.dir"
    if not (target / "link.txt").exists():
        sys.exit("Build nchat with the Unix Makefiles generator before running this test.")

    command = shlex.split((target / "link.txt").read_text())
    command = [
        arg for arg in command
        if not arg.endswith("/src/main.cpp.o") and not arg.startswith("-Wl,--dependency-file=")
    ]
    flags = []
    for line in (target / "flags.make").read_text().splitlines():
        key, separator, value = line.partition(" = ")
        if separator and key in {"CXX_DEFINES", "CXX_INCLUDES", "CXX_FLAGS"}:
            flags.extend(shlex.split(value))

    with tempfile.TemporaryDirectory(prefix="nchat-ui-regression-") as scratch:
        binary = str(Path(scratch) / "test_ui_panes")
        command[command.index("-o") + 1] = binary
        command[1:1] = flags + ["-I" + str(root / "src"), str(root / "tests/test_ui_panes.cpp")]
        subprocess.run(command, cwd=build, check=True, timeout=45)
        result = subprocess.run([binary, scratch], cwd=scratch, env=os.environ.copy(), timeout=10)
        if result.returncode:
            snapshot = Path(scratch) / "panes-failure.txt"
            if snapshot.exists():
                print(snapshot.read_text(), file=sys.stderr)
            sys.exit(result.returncode)


if __name__ == "__main__":
    main()
