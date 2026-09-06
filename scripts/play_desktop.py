#!/usr/bin/env python3
"""Build and launch the PC game using the existing PlatformIO installation."""

from pathlib import Path
import shutil
import subprocess
import sys


def main():
    project = Path(__file__).resolve().parents[1]
    # Match VS Code's bundled Core before considering an older system install.
    pio = None
    for candidate in [
        Path.home() / ".platformio/penv/bin/pio",
        Path.home() / ".platformio/penv/Scripts/platformio.exe",
    ]:
        if candidate.is_file():
            pio = str(candidate)
            break
    if not pio:
        pio = shutil.which("pio") or shutil.which("platformio")
    if not pio:
        print("Open PlatformIO in VS Code once to install PlatformIO Core.", file=sys.stderr)
        return 1
    command = [pio, "run", "-e", "desktop", "-t", "exec"]
    for argument in sys.argv[1:]:
        command.append("--program-arg=" + argument)
    try:
        return subprocess.call(command, cwd=project)
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
