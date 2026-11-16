// gpu/vk/radv_submit_thread.h: whether create_device sets MESA_VK_ENABLE_SUBMIT_THREAD=1 for vkCreateDevice (RADV, the variable
// unset, RX_VK_RADV_SUBMIT_THREAD not off) or leaves Mesa's variable alone. No GPU: the driver is a bool, the environment strings.
#include "../gpu/vk/radv_submit_thread.h"

#include <cstdio>

#define CHECK(name, condition) do { if (!(condition)) { std::fprintf(stderr, "FAIL %s at line %d\n", name, __LINE__); return 1; } } while (0)

int main() {
    using radv_submit_thread::Choice;
    using radv_submit_thread::decide;
    using radv_submit_thread::switch_is_off;

    // RADV, nothing set: on
    CHECK("radv, unset: set", decide(true, nullptr, nullptr) == Choice::set_on);
    CHECK("radv, unset, switch 1: set", decide(true, nullptr, "1") == Choice::set_on);
    CHECK("radv, unset, switch junk: set", decide(true, nullptr, "yes") == Choice::set_on);

    // the user's own value, whatever it is, is kept (Mesa reads an empty or unknown value as off: still the user's)
    CHECK("user 1", decide(true, "1", nullptr) == Choice::user_set);
    CHECK("user 0", decide(true, "0", nullptr) == Choice::user_set);
    CHECK("user empty", decide(true, "", nullptr) == Choice::user_set);
    CHECK("user 0 and switch 0", decide(true, "0", "0") == Choice::user_set);

    // the switch off: left alone
    CHECK("switch 0", decide(true, nullptr, "0") == Choice::switch_off);
    CHECK("switch false", decide(true, nullptr, "False") == Choice::switch_off);
    CHECK("switch OFF", decide(true, nullptr, "OFF") == Choice::switch_off);
    CHECK("switch no", decide(true, nullptr, "no") == Choice::switch_off);
    CHECK("switch empty", decide(true, nullptr, "") == Choice::switch_off);

    // another driver: never touched
    CHECK("not radv", decide(false, nullptr, nullptr) == Choice::not_radv);
    CHECK("not radv, user set", decide(false, "1", nullptr) == Choice::not_radv);
    CHECK("not radv, switch 1", decide(false, nullptr, "1") == Choice::not_radv);

    CHECK("off words", switch_is_off("0") && switch_is_off("") && switch_is_off("No") && !switch_is_off(nullptr));
    CHECK("not off", !switch_is_off("1") && !switch_is_off("00") && !switch_is_off("offf") && !switch_is_off("o"));
    return 0;
}
