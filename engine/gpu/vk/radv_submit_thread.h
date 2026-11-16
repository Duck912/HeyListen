#pragma once
// (Linux, RADV) Mesa's submit thread on by default: MESA_VK_ENABLE_SUBMIT_THREAD=1 set for vkCreateDevice when the user has not
// set it. Mesa's common Vulkan runtime reads the variable in vk_device_init (src/vulkan/runtime/vk_device.c), that is in each
// vkCreateDevice, with os_get_option / debug_get_bool_option (getenv each time, not cached), and only for a device in the
// "assisted" timeline mode (drm syncobj timelines: RADV, ANV and the other drivers on vk_drm_syncobj). Set: every queue of the
// device sends its submissions from a thread of its own (VK_QUEUE_SUBMIT_MODE_THREADED); unset: Mesa starts one only for a
// queue whose submission waits on work not yet sent (THREADED_ON_DEMAND); =0: never. So create_device sets it after the device
// is picked, for RADV alone, and puts the environment back once the device is made.
// Measured on an RX 9070 XT (Mesa 26.2.6, Ryzen 5 5500X3D, Kokiri Forest, frame limit off, 6 interleaved rounds): 90.2 ->
// 100.3 fps (+10.1, se 1.8). The submit ioctl leaves the replay's thread (its "queue submit" 1.16 -> 0.01 ms a frame, its replay
// span 9.3 -> 7.1 ms, its CPU 42% -> 21%); Mesa's thread takes 21-27% of a core.
// RX_VK_RADV_SUBMIT_THREAD=0 (or false, off, no, empty): Mesa's variable left alone.

namespace radv_submit_thread {

enum class Choice {
    set_on,     // RADV, the variable unset, the switch not off: MESA_VK_ENABLE_SUBMIT_THREAD=1 for vkCreateDevice
    user_set,   // the user set MESA_VK_ENABLE_SUBMIT_THREAD (any value, empty too): left as it is
    switch_off, // RX_VK_RADV_SUBMIT_THREAD=0: left alone
    not_radv,   // another driver: left alone
};

/// 0, false, off, no (any case) or empty
inline bool switch_is_off(const char* v) {
    if (!v) return false;
    static const char* const offs[] = {"", "0", "false", "off", "no"};
    for (const char* o : offs) {
        const char* a = v;
        const char* b = o;
        while (*a && *b && (*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) == *b) a++, b++;
        if (!*a && !*b) return true;
    }
    return false;
}

/// radv: the picked device's driverID is VK_DRIVER_ID_MESA_RADV. mesa_value: getenv("MESA_VK_ENABLE_SUBMIT_THREAD").
/// switch_value: getenv("RX_VK_RADV_SUBMIT_THREAD")
inline Choice decide(bool radv, const char* mesa_value, const char* switch_value) {
    if (!radv) return Choice::not_radv;
    if (mesa_value) return Choice::user_set;
    if (switch_is_off(switch_value)) return Choice::switch_off;
    return Choice::set_on;
}

} // namespace radv_submit_thread
