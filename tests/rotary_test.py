#!/usr/bin/env python3
"""Minimal end-to-end test for the rotary encoder driver via gpio-sim.

Usage (as root): rotary_test.py <gpio-sim chip sysfs dir>
"""

import glob
import os
import select
import struct
import sys
import time

CHIP = sys.argv[1]
DEVICE_NAME = "rotary-encoder-gpio"
EVENT_FMT = "llHHi"
EVENT_SIZE = 24
EV_REL, REL_X = 2, 0
EDGE_DELAY = 0.04
SETTLE = 0.2

fd = None
for name_path in glob.glob("/sys/class/input/event*/device/name"):
    if open(name_path).read().strip() == DEVICE_NAME:
        fd = os.open("/dev/input/" + name_path.split("/")[4],
                     os.O_RDONLY | os.O_NONBLOCK)
        break
if fd is None:
    sys.exit("FAIL: input device '%s' not found" % DEVICE_NAME)

level = [1, 1]


def set_lines(a, b):
    for i, value in enumerate((a, b)):
        if level[i] != value:
            with open("%s/sim_gpio%d/pull" % (CHIP, i), "w") as f:
                f.write("pull-up\n" if value else "pull-down\n")
            level[i] = value
            time.sleep(EDGE_DELAY)


def rel_x():
    time.sleep(SETTLE)
    values = []
    while select.select([fd], [], [], 0.1)[0]:
        data = os.read(fd, EVENT_SIZE * 64)
        for off in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
            _, _, etype, code, value = struct.unpack_from(
                EVENT_FMT, data, off)
            if etype == EV_REL and code == REL_X:
                values.append(value)
    return values


def run(name, sequence, expected):
    for a, b in sequence:
        set_lines(a, b)
    got = rel_x()
    print("%s: expected %s, got %s" % (name, expected, got))
    assert got == expected


CW = [(0, 1), (0, 0), (1, 0), (1, 1)]
CCW = [(1, 0), (0, 0), (0, 1), (1, 1)]

run("clockwise one detent", CW, [1])
run("counter-clockwise one detent", CCW, [-1])
run("partial detent then back", CW[:3] + [(0, 0), (0, 1), (1, 1)], [])
run("two detents clockwise", CW + CW, [1, 1])

os.close(fd)
