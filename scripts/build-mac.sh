#!/usr/bin/env bash
# One-shot macOS build: configures, builds VST3 + AU + Standalone + CLI + tests,
# runs the tests, and installs the plugins into your user plug-in folders:
#   ~/Library/Audio/Plug-Ins/VST3/Sample Chopper.vst3
#   ~/Library/Audio/Plug-Ins/Components/Sample Chopper.component
#
#   ./scripts/build-mac.sh            # release build + install
#   ./scripts/build-mac.sh --no-install
set -euo pipefail
cd "$(dirname "$0")/.."

INSTALL=ON
[[ "${1:-}" == "--no-install" ]] && INSTALL=OFF

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSC_COPY_PLUGIN_AFTER_BUILD=$INSTALL
cmake --build build --config Release --parallel "$(sysctl -n hw.ncpu)"
ctest --test-dir build --output-on-failure -C Release

echo
echo "Done."
echo "  Standalone app : build/plugin/SampleChopper_artefacts/Release/Standalone/Sample Chopper.app"
echo "  CLI            : build/cli/chopper"
if [[ "$INSTALL" == "ON" ]]; then
  echo "  VST3 installed : ~/Library/Audio/Plug-Ins/VST3/Sample Chopper.vst3"
  echo "  AU installed   : ~/Library/Audio/Plug-Ins/Components/Sample Chopper.component"
  echo
  echo "In Ableton: Settings > Plug-Ins > Rescan. The AU may need a Live restart the first time."
fi
