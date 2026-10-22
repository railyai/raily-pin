#!/usr/bin/env bash
# Host-side unit tests for the pure firmware headers (counter restore,
# press and feedback gates, SHA-256/HMAC, server pass, secret record,
# bind window, device_info, BLE bond table and the bond files' CCCD field, the held press of bonding stage 2, screen_state payload, vibration rhythms,
# the DA7280 haptic driver on Wire and on bit-banged I2C and its D0 fallback, the OLED screen
# state (press, screen_state, fall, «found you», the agent's counts and notifications), the mascot record and composer,
# serial `i`'s screen_state bench object), the Keyring Air sleep and button (idle_sleep.h) and ship mode (ship_mode.h), battery telemetry (battery_curve.h) and the VDD discharge log (vdd_log.h), voice from the pin (adpcm.h + the golden vectors, voice_frame.h, voice_ring.h, voice_board.h, voice_gesture.h), the Air factory test's checks. No Arduino toolchain needed: g++, and python3 with
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
    test_bootloader_kind.cpp -o "$BUILD_DIR/test_bootloader_kind"
"$BUILD_DIR/test_bootloader_kind"
python3 -B check_dfu_bootloader_guard.py
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_bond_cccd_file.cpp -o "$BUILD_DIR/test_bond_cccd_file"
"$BUILD_DIR/test_bond_cccd_file"
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
# Keyring Air ship mode (docs/pins/air-ship-mode.md): the ship state, its
# radio rule and the 3 s wake / bind hold.
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_ship_mode.cpp -o "$BUILD_DIR/test_ship_mode"
"$BUILD_DIR/test_ship_mode"
# The Bluetooth name by model (device_name.h): «Raily P1» on the P1 and DIY
# images, «Raily Air» on the Air image (-DRAILY_SHIP_MODE=1), the 31-byte
# scan response, and the sketch still setting it from that header.
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_device_name.cpp -o "$BUILD_DIR/test_device_name_p1"
"$BUILD_DIR/test_device_name_p1"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -DRAILY_SHIP_MODE=1 \
    test_device_name.cpp -o "$BUILD_DIR/test_device_name_air"
"$BUILD_DIR/test_device_name_air"
python3 -B check_device_name.py
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_battery_curve.cpp -o "$BUILD_DIR/test_battery_curve"
"$BUILD_DIR/test_battery_curve"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_vdd_log.cpp -o "$BUILD_DIR/test_vdd_log"
"$BUILD_DIR/test_vdd_log"
# Voice from the pin, V1 (docs/pins/voice-from-pin.md §10): IMA-ADPCM and
# the golden vectors in vectors/, the voice_stream frames, the frame ring,
# the per-board mic target, the high-pass filter, the bench serial
# commands, and the push-to-talk gesture and the frame ring at the default
# 2 s talk threshold (owner, 2026-10-09) and at the doc's original 0.4 s.
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_adpcm.cpp -o "$BUILD_DIR/test_adpcm"
"$BUILD_DIR/test_adpcm" vectors
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_voice_frame.cpp -o "$BUILD_DIR/test_voice_frame"
"$BUILD_DIR/test_voice_frame" vectors
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_voice_ring.cpp -o "$BUILD_DIR/test_voice_ring"
"$BUILD_DIR/test_voice_ring" vectors
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_voice_board.cpp -o "$BUILD_DIR/test_voice_board"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -DRAILY_BOARD_XIAO147=1 \
    test_voice_board.cpp -o "$BUILD_DIR/test_voice_board_xiao147"
"$BUILD_DIR/test_voice_board_xiao147"
"$BUILD_DIR/test_voice_board"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_voice_gesture.cpp -o "$BUILD_DIR/test_voice_gesture"
"$BUILD_DIR/test_voice_gesture"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -DRAILY_VOICE_TALK_HOLD_MS=400 \
    test_voice_gesture.cpp -o "$BUILD_DIR/test_voice_gesture_400ms"
"$BUILD_DIR/test_voice_gesture_400ms"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -DRAILY_VOICE_TALK_HOLD_MS=400 \
    test_voice_ring.cpp -o "$BUILD_DIR/test_voice_ring_400ms"
"$BUILD_DIR/test_voice_ring_400ms" vectors
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_voice_capture.cpp -o "$BUILD_DIR/test_voice_capture"
"$BUILD_DIR/test_voice_capture"
g++ -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    test_voice_bench.cpp -o "$BUILD_DIR/test_voice_bench"
"$BUILD_DIR/test_voice_bench"
# The Mac capture tool's own decoder must read the same vectors. It lives in
# hardware/tools/, which the public mirror does not carry.
if [ -f ../../tools/tests/test_voice_capture.py ]; then
    python3 -B ../../tools/tests/test_voice_capture.py
else
    echo "run_tests.sh: voice_capture.py checks skipped (hardware/tools is not in this checkout)"
fi
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
python3 -B check_service_changed_range.py
python3 -B check_oled_boot_order.py
python3 -B check_fall_path.py
python3 -B check_idle_wake.py
