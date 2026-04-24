#!/usr/bin/env bash
set -e

# Usage: ./run_sender.sh [HOST] [PORT]
# Defaults: HOST=127.0.0.1 PORT=57120

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_PATH="$ROOT_DIR/build/juce_fft_osc_artefacts/Release/juce_fft_osc"

if [ ! -x "$BIN_PATH" ]; then
  echo "Error: binary not found or not executable: $BIN_PATH" >&2
  echo "Build the project first (e.g., from the repo root: mkdir -p build && cd build && cmake .. && make)" >&2
  exit 1
fi

HOST=${1:-127.0.0.1}
PORT=${2:-57120}

echo "Starting FFT OSC sender -> $HOST:$PORT (press Enter to quit)"
"$BIN_PATH" --host "$HOST" --port "$PORT"
