#!/usr/bin/env bash
# Build and run the Pentair protocol golden-vector tests on the host.
# No ESPHome / no toolchain download required; pure C++17 against protocol.h.
set -euo pipefail
cd "$(dirname "$0")"
OUT="$(mktemp -d)/ptest"
c++ -std=c++17 -Wall -Wextra -I ../components/pentair protocol_test.cpp -o "$OUT"
"$OUT"
