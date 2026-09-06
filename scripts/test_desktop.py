#!/usr/bin/env python3
"""Check the real game UI and SDL keyboard adapter without opening a window."""

from pathlib import Path
import os
import shlex
import shutil
import subprocess
import sys


def main():
    project = Path(__file__).resolve().parents[1]
    if not shutil.which("g++") or not shutil.which("pkg-config"):
        print("Desktop checks need g++ and pkg-config (see README.md).", file=sys.stderr)
        return 1
    dependencies = subprocess.run(
        ["pkg-config", "--cflags", "--libs", "sdl2"],
        text=True, stdout=subprocess.PIPE, check=False,
    )
    if dependencies.returncode:
        return dependencies.returncode
    output = project / ".pio/build/desktop-test/ui-test"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        "g++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-DPOCKET_POKER_DESKTOP", "-DPOCKET_POKER_DESKTOP_TEST",
        "-Idesktop", "-Ilib/PokerGame/src", "-Isrc",
        "desktop/test_ui.cpp", "src/Device.cpp", "src/DesktopMain.cpp",
        "lib/PokerGame/src/PokerGame.cpp", "-o", str(output),
    ] + shlex.split(dependencies.stdout)
    built = subprocess.run(command, cwd=project, check=False)
    if built.returncode:
        return built.returncode
    test_environment = dict(os.environ, SDL_VIDEODRIVER="dummy")
    return subprocess.run([str(output)], cwd=project, env=test_environment, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
