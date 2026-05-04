# JUCE FFT → OSC

Captures audio, performs an FFT, and sends per-band amplitudes as an OSC message (`/fft/amplitudes`, 64 floats) at a configurable rate. Plays audio files from a directory, with mic-ducking to automatically fade playback out when someone speaks.

## Requirements

- CMake 3.15+
- A C++17 toolchain (Clang/AppleClang on macOS, MSVC on Windows, GCC on Linux)
- JUCE is fetched automatically via CMake FetchContent — no manual installation needed

## Build

```bash
git clone https://github.com/timebent/FFT2OSC.git
cd FFT2OSC
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Binary location after build:
- **macOS/Linux**: `build/juce_fft_osc_artefacts/juce_fft_osc`
- **Windows**: `build\Release\juce_fft_osc.exe`

## Run

```bash
# Basic — send FFT to localhost:57122
./build/juce_fft_osc_artefacts/juce_fft_osc --port 57122

# Shuffle-play audio files and duck when mic exceeds -40 dBFS
./build/juce_fft_osc_artefacts/juce_fft_osc \
  --port 57122 \
  --play-dir=./numbers \
  --shuffle-play \
  --suspend-threshold-db=-40
```

## Mic-ducking

When `--suspend-threshold-db=X` is passed, a state machine monitors the microphone level:

1. **Idle** — mic below threshold; playback at full volume
2. **FadingDown** — mic exceeded threshold for 500 ms; playback fades to silence over 2 s
3. **Holding** — silence held for 10 s; mic activity during this state restarts the cycle
4. **FadingUp** — playback fades back to full volume over 30 s; mic activity restarts the cycle

## OSC output

- Address: `/fft/amplitudes`
- Payload: 64 floats, each in the range 0.0–1.0, representing frequency bands from `--map-min` (default 80 Hz) to `--map-max` (default 24 000 Hz)
- Default destination: `127.0.0.1:57120`

## Flags

See [FLAGS.md](FLAGS.md) for a full list of command-line options.
