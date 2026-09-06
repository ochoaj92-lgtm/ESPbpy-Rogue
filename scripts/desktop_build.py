"""SDL2 configuration for the optional PlatformIO desktop environment."""

import os
import shutil
import subprocess

Import("env")

env.Append(CPPPATH=[os.path.join(env.subst("$PROJECT_DIR"), "desktop")])

if not shutil.which("pkg-config"):
    print("Desktop build needs pkg-config and the SDL2 development library.")
    env.Exit(1)

result = subprocess.run(
    ["pkg-config", "--cflags", "--libs", "sdl2"],
    text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
)
if result.returncode:
    print("SDL2 development files were not found by pkg-config.")
    print("On CachyOS/Arch install sdl2 and pkgconf; see README.md.")
    print(result.stderr.strip())
    env.Exit(1)

env.MergeFlags(result.stdout.strip())
