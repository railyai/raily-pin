#!/usr/bin/env bash
# Host-side unit tests for the firmware counter restore selection.
# Pure C++ — no Arduino toolchain needed.
set -euo pipefail
cd "$(dirname "$0")"
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
