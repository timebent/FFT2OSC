JUCE FFT → OSC

This project is a minimal JUCE-based app that captures audio input, performs an FFT, computes per-bin amplitudes, and sends the amplitudes via Open Sound Control (OSC).

Requirements
- JUCE 6+ (CMake-aware JUCE recommended)
- CMake 3.15+
- A C++17 toolchain

Quick build (assuming JUCE is available via CMake):

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

Default OSC target: localhost:9000 — change in `src/FFTOSC.cpp` or pass flags after build (future improvement).

Notes
- To avoid audio-thread blocking, OSC sending happens on the message thread at a fixed rate.
- This is a scaffold. You may want to add CLI args for OSC host/port, FFT size, windowing, and smoothing.
