/* Which present mode the swapchain uses (vk_main.cpp create_swapchain): the Insert menu's V-Sync row, the VRR row, RX_VK_FIFO and
   RX_VK_IMMEDIATE, and the modes the surface offers. Plain ints, no Vulkan header: tests/test_present_mode.cpp compiles it. */
#pragma once

namespace mkwvk::present_mode {

/// The V-Sync row's values (opts.vsync, saved as vsync). Off is what the game always did: mailbox where offered
enum Row { kOff = 0, kOn = 1, kAdaptive = 2 };
/// What is used
enum Mode { kFifo = 0, kMailboxMode = 1, kImmediateMode = 2, kFifoRelaxedMode = 3 };
/// The modes a surface offers, as bits (FIFO is always there: the spec makes it mandatory)
enum Offered : unsigned { kHasMailbox = 1u, kHasImmediate = 2u, kHasRelaxed = 4u };

struct Input {
    int row = kOff;              // opts.vsync
    bool vrr = false;            // the VRR row resolved to On (vrr_resolve below): FIFO, whatever the V-Sync row says
    bool env_fifo = false;       // RX_VK_FIFO set
    bool env_immediate = false;  // RX_VK_IMMEDIATE set
    unsigned offered = 0;        // Offered bits
    bool windows = false;
};

/// The mode to create the swapchain with. RX_VK_FIFO wins over everything, then RX_VK_IMMEDIATE, then VRR (FIFO, or the
/// relaxed FIFO when the V-Sync row asks for it), then the row. Off is what it always was: mailbox when offered; with none,
/// immediate on Windows (AMD has no mailbox, and FIFO held anything under 60 fps at 30), FIFO elsewhere. On is FIFO;
/// Adaptive is FIFO_RELAXED where offered (a late frame tears instead of waiting a whole refresh), else FIFO.
inline Mode pick(const Input& in) {
    if (in.env_fifo) return kFifo;
    const bool imm = (in.offered & kHasImmediate) != 0;
    if (in.env_immediate && imm) return kImmediateMode;
    if (in.vrr) return in.row == kAdaptive && (in.offered & kHasRelaxed) ? kFifoRelaxedMode : kFifo;
    if (in.row == kOn) return kFifo;
    if (in.row == kAdaptive) return (in.offered & kHasRelaxed) ? kFifoRelaxedMode : kFifo;
    if (in.offered & kHasMailbox) return kMailboxMode;
    return in.windows && imm ? kImmediateMode : kFifo;
}

/// The row's value in words for the overlay and the log
inline const char* mode_name(Mode m) {
    return m == kMailboxMode ? "mailbox" : m == kImmediateMode ? "immediate" : m == kFifoRelaxedMode ? "fifo_relaxed" : "fifo";
}

/// Whether the V-Sync row's value can be had here as it says: Adaptive needs FIFO_RELAXED (else it is plain FIFO), Off needs
/// mailbox or immediate. 0 offered: not asked yet
inline bool row_exact(int row, unsigned offered) {
    if (!offered) return true;
    if (row == kAdaptive) return (offered & kHasRelaxed) != 0;
    if (row == kOff) return (offered & (kHasMailbox | kHasImmediate)) != 0;
    return true;
}

// ---- VRR (G-Sync / FreeSync) ----

/// The VRR row's values (opts.vrr, saved as vrr). Off is the default: nothing changes for a player who leaves it
enum VrrRow { kVrrOff = 0, kVrrOn = 1, kVrrAuto = 2 };
/// Why the VRR row resolved as it did (logged once per change)
enum VrrWhy {
    kWhyOff,            // the row is Off
    kWhyOn,             // the row is On
    kWhyAutoDetected,   // Auto, and the display's EDID lists a refresh range wide enough to be variable
    kWhyAutoNotFound,   // Auto, and no such range (or it could not be read): off
    kWhyAutoWindowed,   // Auto and not fullscreen: off (the window cannot flip)
    kWhyForcedByEnv,    // RX_VK_FIFO / RX_VK_IMMEDIATE decide the mode: VRR leaves the presents alone
};

struct VrrInput {
    int row = kVrrOff;
    bool fullscreen = false;
    int detected = -1;           // -1 unknown, 0 not a variable-refresh display, 1 is
    int refresh_mhz = 0;         // the display's current refresh in mHz, 0 unknown
    bool env_forced = false;     // RX_VK_FIFO or RX_VK_IMMEDIATE set
};
struct VrrState {
    bool active = false;         // FIFO and the cap are used
    bool popup = false;          // the fullscreen window is a flip-eligible WS_POPUP (display.c)
    int cap_below = 0;           // frames a second under the refresh the rate is held at; 0: no cap (refresh unknown)
    VrrWhy why = kWhyOff;
};

/// What the frame rate limit is held to under VRR: NVIDIA's advice is a few fps under the refresh so the game never hits the
/// refresh and falls into V-Sync's wait
constexpr int kVrrCapBelow = 3;

inline VrrState vrr_resolve(const VrrInput& in) {
    VrrState s;
    if (in.row == kVrrOff) return s;
    if (in.env_forced) return s.why = kWhyForcedByEnv, s;
    if (in.row == kVrrAuto) {
        if (!in.fullscreen) return s.why = kWhyAutoWindowed, s;
        if (in.detected != 1) return s.why = kWhyAutoNotFound, s;
        s.why = kWhyAutoDetected;
    } else {
        s.why = kWhyOn;
    }
    s.active = true;
    s.popup = true;   // (only worn by the window while it is fullscreen)
    s.cap_below = in.refresh_mhz > 0 ? kVrrCapBelow : 0;
    return s;
}

/// The frame rate limit under VRR: the player's own limit when it is lower than the refresh - 3, else that. 0 for no cap
/// (inactive, or the refresh unknown). Only a limit above 60 is touched: the console's 60 is left as it is
inline int vrr_limit(const VrrState& s, int refresh_mhz, int limit) {
    if (!s.active || s.cap_below <= 0 || refresh_mhz <= 0 || limit <= 60) return limit;
    const int cap = (refresh_mhz + 500) / 1000 - s.cap_below;
    return cap < 60 ? 60 : (cap < limit ? cap : limit);
}

/// A monitor's refresh range from its EDID (the 0xFD display range limits descriptor): wide enough to be variable? 1 yes,
/// 0 no, -1 no range descriptor or no EDID. A fixed-rate monitor lists a narrow range (56-61 Hz, 50-75 Hz); FreeSync and G-Sync
/// Compatible ones list 48-144 and the like. A hint only: the display's own menu and the driver have the last word
inline int edid_variable(const unsigned char* edid, int size, int* min_hz = nullptr, int* max_hz = nullptr) {
    if (!edid || size < 128 || edid[0] != 0 || edid[1] != 0xFF || edid[7] != 0) return -1;
    for (int off = 54; off <= 108; off += 18) {
        const unsigned char* d = edid + off;
        if (d[0] || d[1] || d[2] || d[3] != 0xFD) continue;
        int lo = d[5], hi = d[6];
        if (d[4] & 0x02) hi += 255;   // (offsets: the vertical range limits over 255 Hz)
        if (d[4] & 0x08) lo += 255;
        if (min_hz) *min_hz = lo;
        if (max_hz) *max_hz = hi;
        return hi - lo >= 30 && hi >= 75 ? 1 : 0;
    }
    return -1;
}

} // namespace mkwvk::present_mode
