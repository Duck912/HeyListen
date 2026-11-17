# SPDX-License-Identifier: GPL-3.0-or-later
"""How task and mesh programs run: the default by driver and OS, and the switch's precedence (gpu/vk/mesh_mode.h, vk_main.cpp).

Issue 51 (an RX 6800 on Linux with RADV loses the GPU in native task/mesh draws, upstream Hookshot issue 2245): on Linux with
an AMD GPU (vendor 0x1002, RADV) Automatic is the compute path. RX_VK_MESH_SHADERS=native keeps the GPU's own mesh shaders
there, compute forces the compute path on any GPU, and RX_VK_FORCE_MESH_FALLBACK=1 is Compute over both (as before). The rule is
compiled and run alone; the source checks read vk_main.cpp. No game or GPU is needed.

    python -I -B tests/test_mesh_mode_switch.py
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

RUN_ALL_ARGS = ["--cxx", "{cxx}"]

ROOT = Path(__file__).resolve().parents[1]

RULE = r'''#include "mesh_mode.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL:%d %s\n", __LINE__, #x); return 1; } } while (0)
using namespace mesh_rule;
static Choice run(const char* env, const char* force, bool linux_os, uint32_t vendor) {
    return effective(choose(env, force), linux_os, vendor);
}
int main() {
    const uint32_t amd = 0x1002, nvidia = 0x10DE, intel = 0x8086;
    // (the default by driver and OS: only Linux AMD, RADV, is Compute; Windows AMD, and every other vendor, are Automatic)
    CHECK(run(nullptr, nullptr, true, amd).mode == 2);
    CHECK(strstr(run(nullptr, nullptr, true, amd).why, "issue 2245"));
    CHECK(run(nullptr, nullptr, false, amd).mode == 0);
    CHECK(run(nullptr, nullptr, true, nvidia).mode == 0);
    CHECK(run(nullptr, nullptr, true, intel).mode == 0);
    CHECK(run(nullptr, nullptr, false, nvidia).mode == 0 && !strcmp(run(nullptr, nullptr, false, nvidia).why, "the default"));
    // (an empty value, and any word that is neither native nor compute, are Automatic: Compute on Linux AMD, the default's why kept)
    for (const char* other : {"", "1", "2", "0", "automatic", "nativ", "compute2", " native", "fallback"}) {
        CHECK(run(other, nullptr, true, amd).mode == 2);
        CHECK(run(other, nullptr, true, nvidia).mode == 0);
        CHECK(run(other, nullptr, false, amd).mode == 0);
    }
    // (the switch: native keeps the GPU's own mesh shaders on Linux AMD too; compute is Compute on any GPU; case does not matter)
    CHECK(run("native", nullptr, true, amd).mode == 1 && !strcmp(run("native", nullptr, true, amd).why, "RX_VK_MESH_SHADERS"));
    CHECK(run("Native", nullptr, true, amd).mode == 1 && run("NATIVE", nullptr, true, nvidia).mode == 1);
    CHECK(run("native", nullptr, false, amd).mode == 1);
    CHECK(run("compute", nullptr, true, nvidia).mode == 2 && run("COMPUTE", nullptr, false, amd).mode == 2);
    CHECK(!strcmp(run("compute", nullptr, true, amd).why, "RX_VK_MESH_SHADERS"));
    // (RX_VK_FORCE_MESH_FALLBACK, set and not 0, is Compute over the switch, on Linux AMD too; 0 leaves the choice to the rest)
    CHECK(run("native", "1", true, amd).mode == 2 && !strcmp(run("native", "1", true, amd).why, "RX_VK_FORCE_MESH_FALLBACK"));
    CHECK(run("native", "1", false, nvidia).mode == 2 && run(nullptr, "1", false, nvidia).mode == 2);
    CHECK(run("native", "7", true, amd).mode == 2);
    CHECK(run("native", "0", true, amd).mode == 1 && run(nullptr, "0", true, amd).mode == 2 && run(nullptr, "0", true, nvidia).mode == 0);
    puts("mesh mode: Linux AMD is Compute by default, RX_VK_MESH_SHADERS chooses, RX_VK_FORCE_MESH_FALLBACK is over it");
    return 0;
}
'''


def block(text, marker):
    start = text.index(marker)
    depth = 0
    for at in range(text.index("{", start), len(text)):
        depth += text[at] == "{"
        depth -= text[at] == "}"
        if depth == 0:
            return text[start:at + 1]
    raise SystemExit("unterminated: " + marker)


def check_source():
    vk = ROOT / "gpu" / "vk"
    main_cpp = (vk / "vk_main.cpp").read_text(encoding="utf-8")
    device = block(main_cpp, "bool create_device() {")
    if 'mesh_rule::choose(getenv("RX_VK_MESH_SHADERS"), getenv("RX_VK_FORCE_MESH_FALLBACK"))' not in device or \
            "mesh_rule::effective(" not in device or "dev.props.vendorID" not in device:
        raise SystemExit("FAIL: create_device does not take the mode from mesh_mode.h's rule, with the device's vendor")
    if "if (mesh_choice.mode == 2) dev.mesh_shader = false;" not in device:
        raise SystemExit("FAIL: the Compute choice does not leave the GPU's mesh shaders unused")
    if 'getenv("RX_VK_FORCE_MESH_FALLBACK")) dev.mesh_shader' in device:
        raise SystemExit("FAIL: RX_VK_FORCE_MESH_FALLBACK is read outside the rule")
    if 'logf("vk: mesh shaders: %s, from %s: %s"' not in device or "mesh_choice.why" not in device:
        raise SystemExit("FAIL: the log line does not say which path was chosen and why")
    if "#ifdef _WIN32" not in device or "constexpr bool linux_os = false;" not in device:
        raise SystemExit("FAIL: the OS is not told to the rule (Windows AMD must keep the mesh shaders)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    check_source()
    with tempfile.TemporaryDirectory(prefix="mesh-mode-") as tmp:
        src = Path(tmp) / "mesh_mode_test.cpp"
        src.write_text(RULE, encoding="utf-8")
        exe = Path(tmp) / "mesh-mode"
        subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "gpu" / "vk"), str(src),
                        "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True, timeout=60)
    print("PASS: mesh mode default by driver and OS, switch precedence")


if __name__ == "__main__":
    main()
