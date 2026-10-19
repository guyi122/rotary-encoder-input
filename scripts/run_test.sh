#!/usr/bin/env bash
# Minimal end-to-end test: build, sign, create a gpio-sim chip, load the
# driver and run the assertions.
#
# Run as root from the repository root:
#     sudo ./scripts/run_test.sh
#
# This version does no cleanup.  After a run (successful or not) the module
# stays loaded and the fixture stays in configfs.  Before running again:
#     sudo rmmod rotary_encoder_gpio
#     sudo sh -c 'echo 0 > /sys/kernel/config/gpio-sim/rotary-test/live'
#     sudo rmdir /sys/kernel/config/gpio-sim/rotary-test/bank0/line0 \
#                /sys/kernel/config/gpio-sim/rotary-test/bank0/line1 \
#                /sys/kernel/config/gpio-sim/rotary-test/bank0 \
#                /sys/kernel/config/gpio-sim/rotary-test

make
/usr/src/linux-headers-$(uname -r)/scripts/sign-file sha512 \
    /var/lib/shim-signed/mok/MOK.priv \
    /var/lib/shim-signed/mok/MOK.der \
    rotary_encoder_gpio.ko

CFG=/sys/kernel/config/gpio-sim
modprobe gpio-sim

mkdir "$CFG/rotary-test"
mkdir "$CFG/rotary-test/bank0"
echo 2 > "$CFG/rotary-test/bank0/num_lines"
echo rotary-sim > "$CFG/rotary-test/bank0/label"
mkdir "$CFG/rotary-test/bank0/line0"
echo a > "$CFG/rotary-test/bank0/line0/name"
mkdir "$CFG/rotary-test/bank0/line1"
echo b > "$CFG/rotary-test/bank0/line1/name"
echo 1 > "$CFG/rotary-test/live"

CHIP=$(ls -d /sys/devices/platform/"$(cat "$CFG/rotary-test/dev_name")"/gpiochip*)
echo pull-up > "$CHIP/sim_gpio0/pull"
echo pull-up > "$CHIP/sim_gpio1/pull"

insmod rotary_encoder_gpio.ko
python3 tests/rotary_test.py "$CHIP"
