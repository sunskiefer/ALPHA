#!/usr/bin/env bash
# ALPHA's offline tests (x86): the framework's ASan/UBSan port test, then the DSP checks on a synthetic voice.
# None of this runs ARM code: the ARMv7 build is only compiled here, and must be tried on a Force (see TESTING in README).
set -euo pipefail
cd "$(dirname "$0")/.."
MV="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
python3 tools/gen_params.py
"$MV/tools/test_port.sh" vst.json 2>&1 | tee build/framework_test.txt | grep -E "^FAIL|PASSED|FAILED" || true
grep -q PASSED build/framework_test.txt
mkdir -p build/host
g++ -O1 -g -fsanitize=address,undefined -std=gnu++14 -w -Isrc -Ibuild -I"$MV/wrapper" test/dsp_test.cc src/engine.cc -lpthread -o build/host/dsp_test
mkdir -p /tmp/alpha_test_presets
./build/host/dsp_test | tee build/dsp_test.txt | grep -E "^FAIL|^info|PASSED|FAILED"
grep -q PASSED build/dsp_test.txt
