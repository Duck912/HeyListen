// SPDX-License-Identifier: GPL-3.0-or-later
// (hookshot, #1001, issue 2245; the switch from Longshot 8694e3c6) How the game's task and mesh programs run: 0 Automatic
// (the GPU's own mesh shaders where it has them, the compute fallback where not, and on Linux with an AMD GPU, RADV, the
// compute path), 1 Native (the GPU's own mesh shaders, RADV's too), 2 Compute (the fallback on every GPU). Chosen by the
// launch switch RX_VK_MESH_SHADERS=native or compute; RX_VK_FORCE_MESH_FALLBACK=1 is Compute over it. Nothing of Vulkan
// here, so tests/test_mesh_mode_switch.py compiles and runs it without a GPU or a game.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>

namespace mesh_rule {

struct Choice {
    int mode;          // 0 Automatic, 1 Native, 2 Compute
    const char* why;   // where it came from, for the log
};

inline bool same_word(const char* a, const char* b) {
    for (; *a && *b; a++, b++)
        if (std::tolower((unsigned char)*a) != *b) return false;
    return !*a && !*b;
}

/// env: RX_VK_MESH_SHADERS's value or null (native or compute; anything else, Automatic). force_fallback:
/// RX_VK_FORCE_MESH_FALLBACK's value or null, a test that is Compute over it when set and not 0
inline Choice choose(const char* env, const char* force_fallback) {
    if (force_fallback && std::atoi(force_fallback) != 0) return {2, "RX_VK_FORCE_MESH_FALLBACK"};
    if (env && same_word(env, "native")) return {1, "RX_VK_MESH_SHADERS"};
    if (env && same_word(env, "compute")) return {2, "RX_VK_MESH_SHADERS"};
    if (env && *env) return {0, "the default (RX_VK_MESH_SHADERS is neither native nor compute)"};
    return {0, "the default"};
}

constexpr uint32_t kVendorAmd = 0x1002;

/// (hookshot, issue 2245) RADV's native task/mesh path deadlocks the GPU: the gfx ring and RADV's gang compute ring end up
/// waiting on each other, and a mesh-only draw hangs with the compute queue empty (devcoredumps on an RX 6800: no VM fault).
/// Task + mesh draws alone on the fallback still crash, so only the full compute path is stable there; the reports span
/// Navi 2x (RX 6800, RX 6600 XT) and the RDNA3 APUs (Steam Deck OLED, ROG Xbox Ally X). On Linux AMD's Vulkan driver is RADV,
/// so Automatic picks Compute for vendor 0x1002 there. RDNA1 and older have no mesh shaders on RADV: for them a no-op.
/// linux: the OS the game runs on (RADV is Linux's; Windows' AMD driver is not affected)
inline bool automatic_is_compute(bool linux_os, uint32_t vendor_id) {
    return linux_os && vendor_id == kVendorAmd;
}

/// The mode a device runs: the choice, with Automatic made Compute where automatic_is_compute says so. why: for the log, the
/// choice's own reason, or the rule that made Automatic Compute
inline Choice effective(Choice c, bool linux_os, uint32_t vendor_id) {
    if (c.mode == 0 && automatic_is_compute(linux_os, vendor_id))
        return {2, "the default on Linux AMD (RADV's mesh shaders stop the GPU, issue 2245; RX_VK_MESH_SHADERS=native keeps them)"};
    return c;
}

}  // namespace mesh_rule
