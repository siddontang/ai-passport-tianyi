"""Exercise the actual portrait renderer and audio envelope on the host."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / ".cache/passport-avatar-tests"
BUILD.mkdir(parents=True, exist_ok=True)
APP = ROOT / "apps/ai-passport-wifi"
COMMON = ROOT / "boards/common"
objects = []
for size in (14, 20):
    source = COMMON / f"raster_montserrat_{size}.c"
    target = BUILD / f"font{size}.o"
    subprocess.run(["cc", "-std=c11", "-I", str(COMMON), "-c", str(source), "-o", str(target)], check=True)
    objects.append(str(target))
target = BUILD / "cjk.o"
subprocess.run(["cc", "-std=c11", "-I", str(COMMON), "-c", str(APP / "passport_font.c"), "-o", str(target)], check=True)
objects.append(str(target))
binary = BUILD / "passport_avatar_test"
subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-g",
                "-I", str(APP), "-I", str(COMMON), str(ROOT / "tests/passport_avatar_test.cc"),
                str(APP / "passport_avatar.cc"), str(APP / "passport_avatar_data.cc"),
                str(COMMON / "stripe_canvas.cc"), *objects, "-o", str(binary)], check=True)
subprocess.run([str(binary), str(BUILD), *sys.argv[1:]], check=True)
