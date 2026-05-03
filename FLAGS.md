Command-line flags — quick reference

This file is a compact summary of runtime flags and a few common invocation examples.

Flags (short):
- --host HOST           — OSC destination host (default 127.0.0.1)
- --port PORT           — OSC destination port (default 57120)
- -h, --help            — show usage
- --test-tone <Hz>      — enable built-in test tone (1.0 → 440 Hz)
- --sender-interval N   — send interval in ms (many aliases supported)
- --diag-sender         — enable sender diagnostics and optional payload dumps
- --shuffle-play        — shuffle playback
- --play-files=<list>   — comma-separated playback files
- --play-dir <path>     — play all supported files in directory
- --rms-agg             — use RMS aggregation when not using interpolation
- --no-rms              — disable RMS aggregation
 --auto-playback       — enable auto file playback on silence
 --auto-play-threshold-db=X
 --mic-fade-threshold-db=X (alias: --suspend-threshold-db=X)
 --auto-play-hold-ms=X
 (fractional-bin interpolation is always enabled)
 --display-noise-floor-db=X
- --voice-only[=0|1]    — enable/disable voice-only masking
- --voice-min <Hz>
- --voice-max <Hz>
- --map-min <Hz> / --mapMaxFreq=<Hz>
- --map-max <Hz> / --mapMinFreq=<Hz>

Notes
- See src/Main.cpp for authoritative parsing and exact alias forms.
- base-noise-floor-db is converted with lin = pow(10, db/20). For fftSize=2048 the code scales magnitudes by 1/fftSize (~-66.2 dB) before subtraction; choose dB values accordingly.

Common examples

Start sending to localhost:57120 at default settings:

```bash
./build/juce_fft_osc_artefacts/Release/juce_fft_osc --diag-sender --host 127.0.0.1 --port 57120
```

(Noise-floor processing has been removed from this build.)

Auto-playback when input is silent:

```bash
./build/juce_fft_osc_artefacts/Release/juce_fft_osc --auto-playback --auto-play-threshold-db=-30 --auto-play-hold-ms=1500 --diag-sender
```

Debugging note: enable `--diag-sender` and inspect `/tmp/fftosc.log` for `AMP[...]`, `SEND`, and `FULL_PAYLOAD` entries.