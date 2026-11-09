#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p /tmp/pantalla-emt-tests
for source in tests/test_*.cpp; do
  name="$(basename "$source" .cpp)"
  g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Iinclude -Itests/fakes -Ilib/stationData -I.pio/libdeps/esp32dev/ArduinoJson/src "$source" -o "/tmp/pantalla-emt-tests/$name"
  "/tmp/pantalla-emt-tests/$name"
done
