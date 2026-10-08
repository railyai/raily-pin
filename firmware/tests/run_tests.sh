#!/usr/bin/env bash
# Host-side unit tests for the pure firmware headers (counter restore,
# press and feedback gates, SHA-256/HMAC, server pass, secret record,
# bind window, device_info, BLE bond table, the held press of bonding stage 2, screen_state payload, vibration rhythms,
# the DA7280 haptic driver on bit-banged I2C and its D0 fallback, the OLED screen
# state (press, screen_state, fall, «found you», the agent's counts and notifications), the mascot record and composer,
# serial `i`'s screen_state bench object), the Keyring Air sleep and button (idle_sleep.h), battery telemetry (battery_curve.h) and the VDD discharge log (vdd_log.h), the Air factory test's checks. No Arduino toolchain needed: g++, and python3 with
# Pillow for the OLED golden frames and asset checks.
set -euo pipefail
cd "$(dirname "$0")"
# The Air factory test is private (owner, 2026-10-08): the public mirror has
# no ../RailyAirFactoryTest and no test_factory_checks.cpp. Where the folder
# exists, its checks are required, not optional.
FACTORY_FLAGS=()
[ -d ../RailyAirFactoryTest ] && FACTORY_FLAGS=(-DRAILY_REQUIRE_FACTORY_CHECKS)
command -v g++ >/dev/null || { echo "run_tests.sh: g++ not found" >&2; exit 127; }
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/counter_select_test.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_counter_select.cpp -o "$BUILD_DIR/test"
"$BUILD_DIR/test"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_press_command.cpp -o "$BUILD_DIR/test_press_command"
"$BUILD_DIR/test_press_command"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_feedback_state.cpp -o "$BUILD_DIR/test_feedback_state"
"$BUILD_DIR/test_feedback_state"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_hmac_sha256.cpp -o "$BUILD_DIR/test_hmac_sha256"
"$BUILD_DIR/test_hmac_sha256"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_server_pass.cpp -o "$BUILD_DIR/test_server_pass"
"$BUILD_DIR/test_server_pass"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -DRAILY_SEAL_PROD \
    test_server_pass.cpp -o "$BUILD_DIR/test_server_pass_prod"
"$BUILD_DIR/test_server_pass_prod"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_bind_window.cpp -o "$BUILD_DIR/test_bind_window"
"$BUILD_DIR/test_bind_window"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_device_info.cpp -o "$BUILD_DIR/test_device_info"
"$BUILD_DIR/test_device_info"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_bond_table.cpp -o "$BUILD_DIR/test_bond_table"
"$BUILD_DIR/test_bond_table"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_press_hold.cpp -o "$BUILD_DIR/test_press_hold"
"$BUILD_DIR/test_press_hold"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_payload.cpp -o "$BUILD_DIR/test_screen_payload"
"$BUILD_DIR/test_screen_payload"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_rhythm.cpp -o "$BUILD_DIR/test_rhythm"
"$BUILD_DIR/test_rhythm"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    ${FACTORY_FLAGS[@]+"${FACTORY_FLAGS[@]}"} test_da7280.cpp -o "$BUILD_DIR/test_da7280"
"$BUILD_DIR/test_da7280"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_state.cpp -o "$BUILD_DIR/test_screen_state"
"$BUILD_DIR/test_screen_state"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_agent.cpp -o "$BUILD_DIR/test_screen_agent"
"$BUILD_DIR/test_screen_agent"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_fall.cpp -o "$BUILD_DIR/test_screen_fall"
"$BUILD_DIR/test_screen_fall"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_found_you.cpp -o "$BUILD_DIR/test_screen_found_you"
"$BUILD_DIR/test_screen_found_you"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_counts.cpp -o "$BUILD_DIR/test_screen_counts"
"$BUILD_DIR/test_screen_counts"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_notify.cpp -o "$BUILD_DIR/test_screen_notify"
"$BUILD_DIR/test_screen_notify"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_mascot_record.cpp -o "$BUILD_DIR/test_mascot_record"
"$BUILD_DIR/test_mascot_record"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_bench_serial.cpp -o "$BUILD_DIR/test_bench_serial"
"$BUILD_DIR/test_bench_serial"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_screen_bench.cpp -o "$BUILD_DIR/test_screen_bench"
"$BUILD_DIR/test_screen_bench"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_idle_sleep.cpp -o "$BUILD_DIR/test_idle_sleep"
"$BUILD_DIR/test_idle_sleep"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_battery_curve.cpp -o "$BUILD_DIR/test_battery_curve"
"$BUILD_DIR/test_battery_curve"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_vdd_log.cpp -o "$BUILD_DIR/test_vdd_log"
"$BUILD_DIR/test_vdd_log"
if [ -d ../RailyAirFactoryTest ]; then
    g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
        test_factory_checks.cpp -o "$BUILD_DIR/test_factory_checks"
    "$BUILD_DIR/test_factory_checks"
else
    echo "run_tests.sh: Air factory checks skipped (../RailyAirFactoryTest is private)"
fi
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_oled_compose.cpp -o "$BUILD_DIR/test_oled_compose"
mkdir -p "$BUILD_DIR/oled"
"$BUILD_DIR/test_oled_compose" oled_golden/manifest.txt "$BUILD_DIR/oled"
python3 -B check_oled_golden.py "$BUILD_DIR/oled"
python3 -B check_oled_assets.py

python3 check_ble_setup.py
python3 check_authorize_replies.py
python3 check_reset_reason.py
python3 -B check_oled_boot_order.py
python3 -B check_fall_path.py
python3 -B check_idle_wake.py
