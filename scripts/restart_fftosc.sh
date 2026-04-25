#!/usr/bin/env bash
# Restart juce_fft_osc with configurable flags. Logs go to /tmp/fftosc.log
set -euo pipefail
BIN=./build/juce_fft_osc_artefacts/Release/juce_fft_osc
LOG=/tmp/fftosc.log
# Default flags (you can override by passing args)
DEFAULT_FLAGS=(--interp --sender-interval 33 --diag-sender --no-noise-floor --rms-agg --host 127.0.0.1 --port 57120)
# If first arg is "all", use a wide set of flags; otherwise pass through args
if [ "$#" -eq 0 ]; then
  ARGS=("${DEFAULT_FLAGS[@]}")
else
  if [ "$1" = "all" ]; then
    ARGS=( --interp --sender-interval 33 --diag-sender --no-noise-floor --rms-agg --voice-only --map-min 80 --map-max 8000 --host 127.0.0.1 --port 57120 )
    shift
    # append any extra args
    ARGS+=("$@")
  else
    ARGS=("$@")
  fi
fi
# Stop existing process (if any)
echo "Stopping existing juce_fft_osc instances..."
pkill -f "juce_fft_osc_artefacts/Release/juce_fft_osc" || true
sleep 0.1
# Start new instance with stdin from /dev/null to avoid TTY suspension
CMD=("$BIN" "${ARGS[@]}")
echo "Starting: ${CMD[*]}"
"${CMD[@]}" < /dev/null > "$LOG" 2>&1 &
PID=$!
echo "Started pid=$PID; logging to $LOG"
# show a few initial log lines
sleep 0.8
if [ -f "$LOG" ]; then
  echo "--- log head ---"
  head -n 80 "$LOG"
fi

echo "Use 'tail -f /tmp/fftosc.log' to watch runtime output or 'pkill -f juce_fft_osc' to stop."
