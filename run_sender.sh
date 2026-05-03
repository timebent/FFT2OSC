#!/usr/bin/env bash
set -e

# Usage: ./run_sender.sh [HOST] [PORT]
# Defaults: HOST=127.0.0.1 PORT=57120

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_PATH_CAND1="$ROOT_DIR/build/juce_fft_osc_artefacts/juce_fft_osc"
BIN_PATH_CAND2="$ROOT_DIR/build/juce_fft_osc_artefacts/Release/juce_fft_osc"

if [ -x "$BIN_PATH_CAND1" ]; then
  BIN_PATH="$BIN_PATH_CAND1"
elif [ -x "$BIN_PATH_CAND2" ]; then
  BIN_PATH="$BIN_PATH_CAND2"
else
  echo "Error: binary not found or not executable. Expected one of:" >&2
  echo "  $BIN_PATH_CAND1" >&2
  echo "  $BIN_PATH_CAND2" >&2
  echo "Build the project first (e.g., from the repo root: mkdir -p build && cd build && cmake .. && make)" >&2
  exit 1
fi

HOST=${1:-127.0.0.1}
PORT=${2:-57120}

echo "Starting FFT OSC sender -> $HOST:$PORT (running in foreground to allow microphone permissions)"
# forward any additional args (e.g. --play-files, --diag-sender)
"$BIN_PATH" --host "$HOST" --port "$PORT" "${@:3}"
