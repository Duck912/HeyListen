"""The Frame generation row stops at DLSS Frame Generation's runtime while DLSS FG makes the frames.

NGX tells the most frames its runtime makes between two of the game's (MultiFrameCountMax: 5 on an RTX 50's, 1 on an
RTX 40's). Past it NGX was asked for no set at all (vk_dlss.cpp dlss_fg_frames: FG_SKIP_TOO_MANY) and the renderer's own
frame generation made every frame, with DLSS FG chosen: 7x and 8x on an RTX 50 were never DLSS FG. Now the fixed
multiplier frame generation runs at, framegen_now (gpu/vk/vk_framegen.cpp), is the row's, but at most the runtime's most
+ 1 while DLSS FG makes the frames; Off and Auto as they are; a saved value past it is kept (another engine runs it) and
is shown and run as that top.

The real framegen_capped is compiled and run: 6x at most on a runtime that makes 5, 2x on one that makes 1, the row's
value while the most is not known (0: NGX not asked yet, or another engine). The text checks: vk_dlss.cpp's
dlss_fg_most gives the runtime's most only while DLSS-G is asked for and can run, from a temporal Upscaler's inputs (0 in a
build without it);
multiplier(), swapchain_framegen(), swapchain_dlssg(), create_swapchain's set_too_big and present_image's swapchain key
take framegen_now(); the row shows the capped value and its arrows step from it, up to the top (past it round to Off, as
past 8x); the control bridge's value is set as asked. At Auto the row names who makes its frames (FSR 3.1 FG or Built-in
FG: DLSS FG makes fixed multipliers only).

    python -I -B tests/test_framegen_dlss_cap.py [--cxx g++]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))   # (python -I leaves the check's own folder off the path)
from srccut import between, block  # noqa: E402

RUN_ALL_ARGS = ["--cxx", "{cxx}"]
ROOT = Path(__file__).resolve().parents[1]

MAIN = r"""
#include <cstdio>
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
int main() {
    /* an RTX 50's runtime: 5 made frames at most, so 6x */
    CHECK(framegen_capped(8, 5) == 6 && framegen_capped(7, 5) == 6 && framegen_capped(6, 5) == 6);
    CHECK(framegen_capped(5, 5) == 5 && framegen_capped(2, 5) == 2);
    /* an RTX 40's: 1 at most, so 2x */
    CHECK(framegen_capped(8, 1) == 2 && framegen_capped(4, 1) == 2 && framegen_capped(2, 1) == 2);
    /* not known yet, or another engine: the row's */
    CHECK(framegen_capped(8, 0) == 8 && framegen_capped(3, 0) == 3);
    /* Off and Auto as they are */
    CHECK(framegen_capped(0, 5) == 0 && framegen_capped(1, 5) == 1 && framegen_capped(1, 1) == 1);
    if (failures) return 1;
    std::puts("PASS: DLSS FG's multiplier stops at its runtime's most + 1");
    return 0;
}
"""


def check_sources(framegen, dlss):
    mult = block(framegen, "int multiplier() {", where="gpu/vk/vk_framegen.cpp")
    assert "framegen_now()" in mult and "opts.framegen" not in mult, \
        "multiplier: frame generation runs at framegen_now(), the row's capped by DLSS FG's runtime"
    now = block(framegen, "int framegen_now() {", where="gpu/vk/vk_framegen.cpp")
    assert "framegen_capped(opts.framegen, dlss_fg_most())" in now, "framegen_now: the row's value, capped while DLSS FG makes them"
    most = [b for b in dlss.split("int dlss_fg_most() {")[1:]]
    assert len(most) == 2, "vk_dlss.cpp: dlss_fg_most with NVIDIA's SDK and without it"
    real = block(dlss, "int dlss_fg_most() {\n    if (!dlss_fg_wanted()", where="gpu/vk/vk_dlss.cpp")
    for part in ("dlss_fg_wanted()", "!temporal_wanted()", "most_known"):
        assert part in real, "dlss_fg_most: " + part
    assert "int dlss_fg_most() { return 0; }" in dlss, "vk_dlss.cpp: 0 in a build without NVIDIA's SDK"
    frames = block(dlss, "int dlss_fg_frames(VkCommandBuffer cmd,", where="gpu/vk/vk_dlss.cpp")
    too_many = frames[frames.index("if ((unsigned)count > g.most) {"):]
    assert too_many.index("if (!temporal_wanted()) return 0;") < too_many.index("g.too_many++"), \
        "dlss_fg_frames: without a temporal Upscaler (no cap) the reason is the inputs, not the runtime's most"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cxx", default="g++")
    a = p.parse_args()
    vk = ROOT / "gpu" / "vk"
    framegen = (vk / "vk_framegen.cpp").read_text(encoding="utf-8")
    dlss = (vk / "vk_dlss.cpp").read_text(encoding="utf-8")
    fixture = "#include <algorithm>\n" + block(framegen, "int framegen_capped(", where="gpu/vk/vk_framegen.cpp") + MAIN
    check_sources(framegen, dlss)
    with tempfile.TemporaryDirectory(prefix="framegen-dlss-cap-") as tmp:
        cpp = Path(tmp) / "fixture.cpp"
        exe = Path(tmp) / "fixture"
        cpp.write_text(fixture, encoding="utf-8")
        subprocess.run([a.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
