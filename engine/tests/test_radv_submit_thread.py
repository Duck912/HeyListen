"""Mesa's submit thread on by default with RADV: create_device sets MESA_VK_ENABLE_SUBMIT_THREAD=1 for its vkCreateDevice
(where Mesa reads it, vk_device_init) unless the user set it or RX_VK_RADV_SUBMIT_THREAD=0, and takes it out again after.
Compiles gpu/vk/radv_submit_thread.h's test and reads the source; no game or GPU.

    python -I -B tests/test_radv_submit_thread.py [--cxx g++]
"""
import argparse
import subprocess
import sys
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
    """The function starting at `head`, to its closing brace at the start of a line"""
    i = src.find(head)
    if i < 0:
        fails.append("no " + head)
        return ""
    return src[i:src.find("\n}\n", i) + 3]


def check_source():
    main = read("gpu/vk/vk_main.cpp")
    need('#include "radv_submit_thread.h"' in main, "vk_main.cpp includes radv_submit_thread.h")
    dev = body(main, "bool create_device() {")
    decide = dev.find("radv_submit_thread::decide(")
    setenv = dev.find('setenv("MESA_VK_ENABLE_SUBMIT_THREAD", "1", 0)')
    create = dev.find("vkCreateDevice(dev.physical, &ci, nullptr, &dev.device)")
    unset = dev.find('unsetenv("MESA_VK_ENABLE_SUBMIT_THREAD")')
    need(decide >= 0 and setenv >= 0 and create >= 0 and unset >= 0, "create_device: decide, setenv, vkCreateDevice, unsetenv")
    # (Mesa reads the variable in vk_device_init, inside vkCreateDevice: set before, taken out after)
    need(0 <= decide < setenv < create < unset, "create_device: the variable set before vkCreateDevice and taken out after")
    need("VK_DRIVER_ID_MESA_RADV" in dev[decide:decide + 200], "create_device: RADV from the picked device's driverID")
    need('getenv("MESA_VK_ENABLE_SUBMIT_THREAD")' in dev[decide:decide + 300]
         and 'getenv("RX_VK_RADV_SUBMIT_THREAD")' in dev[decide:decide + 300], "create_device: the user's value and the switch read")
    need(dev.rfind("#ifndef _WIN32", 0, decide) > dev.rfind("#endif", 0, decide), "create_device: not on Windows")
    for line in ("Mesa's submit thread on (MESA_VK_ENABLE_SUBMIT_THREAD unset",
                 "Mesa's submit thread left as the user set it",
                 "Mesa's submit thread off by the switch"):
        need(line in dev, "create_device logs: " + line)
    need(dev.count("vkCreateDevice(") == 1, "create_device: one vkCreateDevice")
    need("vkCreateDevice(" not in main.replace(dev, ""), "vk_main.cpp: no other vkCreateDevice")

    docs = read("docs/development.md")
    need("`RX_VK_RADV_SUBMIT_THREAD=0`" in docs, "docs/development.md: RX_VK_RADV_SUBMIT_THREAD in the table")
    need("HOS_INPUT_SCRIPT=8:A,10:A,12:A,14:A" in docs[docs.find("RX_VK_RADV_SUBMIT_THREAD"):],
         "docs/development.md: the benchmark recipe beside the switch")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    check_source()
    with tempfile.TemporaryDirectory(prefix="radv-submit-thread-") as tmp:
        exe = Path(tmp) / "radv-submit-thread"
        subprocess.run([args.cxx, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/test_radv_submit_thread.cpp"), "-o", str(exe)], check=True)
        run = subprocess.run([str(exe)], text=True, capture_output=True)
        need(run.returncode == 0, "radv_submit_thread.h: " + (run.stderr.strip() or f"exit {run.returncode}"))
    if fails:
        for f in fails:
            print("FAIL:", f)
        return 1
    print("test_radv_submit_thread: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
