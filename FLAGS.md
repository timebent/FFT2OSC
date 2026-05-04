# FFT2OSC CLI Flags

All flags use `--flag=value` style. Boolean flags have no value.

## Network

| Flag | Default | Description |
|------|---------|-------------|
| `--host=HOST` | `127.0.0.1` | OSC destination host |
| `--port=PORT` | `57120` | OSC destination port |

## File Playback

| Flag | Default | Description |
|------|---------|-------------|
| `--play-dir=PATH` | — | Play all audio files in a directory |
| `--play-files=FILE,FILE,...` | — | Play specific comma-separated files |
| `--shuffle-play` | off | Shuffle playback order |

## Mic-Ducking and Auto-Playback

These two features are independent and can be used together. A typical setup uses both: auto-playback starts a file when the mic is quiet, and mic-ducking fades it out when the mic gets loud again.

### Mic-Ducking — fade out when mic is loud

| Flag | Default | Description |
|------|---------|-------------|
| `--suspend-threshold-db=X` | — | Enable mic-ducking; duck when mic exceeds X dBFS for 500 ms |

State machine: mic above threshold for 500 ms → fade out (2 s) → hold silent (10 s) → fade back in (30 s). A re-trigger during hold or fade-up restarts from the beginning.

### Auto-Playback — start playback when mic is quiet

| Flag | Default | Description |
|------|---------|-------------|
| `--auto-playback` | off | Start file playback when mic falls below threshold |
| `--auto-play-threshold-db=X` | `-40` | Mic must be below this level (dBFS) to be considered quiet |
| `--auto-play-hold-ms=MS` | — | Duration mic must stay quiet before playback starts |

## FFT / Frequency

| Flag | Default | Description |
|------|---------|-------------|
| `--map-min=HZ` | `80` | Lowest frequency to include in FFT output |
| `--map-max=HZ` | `24000` | Highest frequency to include in FFT output |
| `--hp-cutoff=HZ` | — | High-pass filter cutoff frequency |
| `--rms-agg` | off | Use RMS aggregation when downsampling bins |
| `--no-rms` | off | Disable RMS aggregation |
| `--display-noise-floor-db=X` | `-60` | Noise floor for display normalisation |

## Voice Filtering

| Flag | Default | Description |
|------|---------|-------------|
| `--voice-only` | off | Zero non-voice FFT bins before sending |
| `--voice-min=HZ` | `80` | Voice band lower bound |
| `--voice-max=HZ` | `3000` | Voice band upper bound |

## Test / Diagnostics

| Flag | Default | Description |
|------|---------|-------------|
| `--test-tone=HZ` | — | Enable built-in sine test tone at given frequency |
| `--sender-interval=MS` | `33` | OSC send interval in milliseconds |
| `--diag-sender` | off | Print per-send diagnostics |
| `--autoplay-toggle=MS:CYCLES` | — | Toggle auto-playback every MS ms for CYCLES cycles (testing) |
| `-h`, `--help` | — | Show help and exit |
