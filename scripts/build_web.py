#!/usr/bin/env python3
"""Compile the shared SDL game to a static GitHub Pages directory."""

from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default="build/web", help="Output folder relative to the project")
    parser.add_argument("--emxx", help="Path to Emscripten em++ (otherwise use PATH or EMSDK)")
    arguments = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    compiler = arguments.emxx or shutil.which("em++")
    if not compiler:
        sdk = os.environ.get("EMSDK")
        if sdk:
            candidate = Path(sdk) / "upstream/emscripten/em++"
            if candidate.is_file():
                compiler = str(candidate)
    if not compiler:
        print("Activate Emscripten 6.0.9 first (see README.md), or pass --emxx /path/to/em++.", file=sys.stderr)
        return 1
    output = (project / arguments.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    command = [
        compiler, "-std=c++11", "-O2", "-Wall", "-Wextra",
        "-DPOCKET_POKER_DESKTOP", "-Idesktop", "-Ilib/PokerGame/src",
        "src/main.cpp", "src/Device.cpp", "src/DesktopMain.cpp", "lib/PokerGame/src/PokerGame.cpp",
        "-sUSE_SDL=2", "-sALLOW_MEMORY_GROWTH=1", "-sENVIRONMENT=web",
        "-sEXPORTED_FUNCTIONS=['_main','_pocket_key','_pocket_release']",
        "-sASSERTIONS=1", "-o", str(output / "game.js"),
    ]
    built = subprocess.run(command, cwd=project, check=False)
    if built.returncode:
        return built.returncode
    for name in ("index.html", "style.css", "player.js"):
        shutil.copyfile(project / "web" / name, output / name)
    (output / ".nojekyll").touch()
    print("Browser demo built at", output)
    print("Preview: python3 -m http.server 8000 --directory", output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
