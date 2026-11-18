"""The Insert menu's V-Sync and VRR rows (gpu/vk/present_mode.h, their use in vk_main.cpp's create_swapchain and vrr_resolved,
vk_replay.cpp's present_image), frame generation under FIFO, and the flip-eligible fullscreen window (host/display.c), no game or GPU needed.

    python -I -B tests/test_present_mode.py [--cxx g++]
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
fails = []


def need(ok, what):
    if not ok:
        fails.append(what)


def read(rel):
    return (ROOT / rel).read_text(encoding="utf-8")


def body(src, head):
    i = src.find(head)
    if i < 0:
        fails.append("no " + head)
        return ""
    return src[i:src.find("\n}\n", i) + 3]


def check_source():
    main = read("gpu/vk/vk_main.cpp")
    sc = body(main, "bool create_swapchain(")
    need("present_mode::pick(" in sc, "create_swapchain: the mode from present_mode::pick")
    need("pin.row = opts.vsync;" in sc, "create_swapchain: the V-Sync row is the choice")
    need("pin.vrr = vrr.active;" in sc and "vrr_resolved(true)" in sc, "create_swapchain: VRR from vrr_resolved")
    need('getenv("RX_VK_FIFO")' in sc and 'getenv("RX_VK_IMMEDIATE")' in sc, "create_swapchain: both switches kept")
    need("VK_PRESENT_MODE_FIFO_RELAXED_KHR" in sc, "create_swapchain: FIFO_RELAXED for Adaptive")
    need("dev.present_modes_offered = offered;" in sc, "create_swapchain: the offered modes kept for the row")
    vr = body(main, "present_mode::VrrState vrr_resolved(bool fresh) {")
    need("if (opts.vrr == present_mode::kVrrOff && !vrr_touched" in vr or
         "opts.vrr == kVrrOff && !vrr_touched" in vr, "vrr_resolved: Off before any VRR leaves everything alone")
    need("WM_APP + 10" in vr, "vrr_resolved: the window's style follows (WM_APP + 10)")
    need("logf(" in vr and "RX_VK_FIFO" in vr, "vrr_resolved: logs the choice and why")
    cap = body(main, "int vrr_fps_cap() {")
    need("return 0;" in cap, "vrr_fps_cap: Off answers 0: no cap")

    menu = read("gpu/vk/vk_menu.cpp")
    for name, field in (('"V-Sync"', "&opts.vsync,"), ('"VRR (G-Sync / FreeSync)"', "&opts.vrr,")):
        start = menu.index(field, menu.index("Row rows[] = {"))
        row = menu[start:menu.index("},", menu.index("hn_", start))]
        need(name in menu[start - 60:start], "vk_menu.cpp: a %s row" % name)
        need("TAB_GRAPHICS" in menu[start - 80:start], "vk_menu.cpp: %s on the Graphics tab" % name)
        need(row.rstrip().endswith(", true"), "vk_menu.cpp: %s leaves the quality preset alone" % name)
    need('r.value == &opts.vsync) return "vsync"' in menu and 'r.value == &opts.vrr) return "vrr"' in menu,
         "vk_menu.cpp: the ini keys vsync and vrr")

    replay = read("gpu/vk/vk_replay.cpp")
    pi = body(replay, "void present_image(")
    need("present_was != present_now" in pi and "|| present_changed" in pi,
         "present_image: the swapchain made again when the rows change")
    need("present_was = present_now;" in pi, "present_image: the rows' state noted once the swapchain is made")
    need("id == HL_OPT_VRR_CAP ? vrr_fps_cap()" in replay, "vk_replay.cpp: the VRR cap is answered")
    nn_vi = read("host/nn_vi.c")
    need("rx_option(HL_OPT_VRR_CAP, 0)" in nn_vi, "nn_vi.c: a limit above 60 is held to the VRR cap (0: none)")

    menu_h = read("gpu/vk/vk_menu.h")
    need("int vsync = 0;" in menu_h, "vk_menu.h: V-Sync Off by default")
    need("int vrr = 0;" in menu_h, "vk_menu.h: VRR Off by default")

    disp = read("host/display.c")
    style = body(disp, "static void fullscreen_style(HWND w) {")
    need("popup_wanted" in style and "WS_POPUP" in style and "WS_OVERLAPPEDWINDOW" in style, "fullscreen_style: WS_POPUP only when asked")
    need("mi.rcMonitor.left" in style and "mi.rcMonitor.right - mi.rcMonitor.left" in style, "fullscreen_style: the monitor's rect")
    need("HWND_TOP" in style, "fullscreen_style: on top")
    need("popup_wanted ? SWP_SHOWWINDOW : 0" in style, "fullscreen_style: nothing added to the flags when VRR is off")
    need("CreateWindowExA" not in disp, "display.c: a window with no extended styles (layered ones block flip)")
    fs = body(disp, "static void set_fullscreen(HWND w, int on) {")
    off = fs[fs.index("} else {"):]
    need("~(LONG)WS_POPUP" in off and "WS_OVERLAPPEDWINDOW" in off and "SetWindowPlacement" in off,
         "set_fullscreen: windowed again drops WS_POPUP and restores the placement")
    need("case WM_APP + 10:" in disp and "popup_wanted = wp != 0;" in disp, "display.c: WM_APP + 10 sets the popup style")

    fg = read("gpu/vk/vk_framegen.cpp")
    need("vrr_min_gap_ns()" in fg and "keep[kept++]" in fg and "present_mode_name[0] == 'f'" in fg,
         "vk_framegen.cpp: FIFO leaves out the made frames the refresh has no room for; VRR spaces the presents")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    check_source()
    with tempfile.TemporaryDirectory(prefix="present-mode-") as tmp:
        exe = Path(tmp) / "present-mode"
        subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests" / "test_present_mode.cpp"), "-o", str(exe)], check=True)
        run = subprocess.run([str(exe)], text=True, capture_output=True)
        need(run.returncode == 0, "present_mode.h: " + (run.stdout + run.stderr).strip())
    if fails:
        for f in fails:
            print("FAIL:", f)
        raise SystemExit(1)
    print("test_present_mode: ok")


if __name__ == "__main__":
    main()
