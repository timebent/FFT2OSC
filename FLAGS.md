Command-line flags — quick reference

This file is a compact summary of runtime flags and a few common invocation examples.

## Network / OSC

- `--host HOST`                  OSC destination host (default: 127.0.0.1)
- `--port PORT`                  OSC destination port (default: 57120)

## General

- `-h`, `--help`                 Show usage
- `--quiet`                      Suppress non-essential log output
- `--diag-sender`                Enable sender diagnostics and payload dumps
- `--diag-sender=N`              Enable diagnostics; dump N full OSC payloads
- `--sender-interval N`          Send interval in ms (aliases: `--sender-interval=N`, `--senderInterval N`, `--senderInterval=N`, `--senderIntervalMs=N`)

## FFT / frequency mapping

- `--map-min <Hz>`               Lowest frequency mapped to band 0 (aliases: `--map-min=`, `--mapMinFreq=`, `--mapMinFreq`)
- `--map-max <Hz>`               Highest frequency mapped to last band (aliases: `--map-max=`, `--mapMaxFreq=`, `--mapMaxFreq`)
- `--rms-agg`                    Use RMS aggregation when downsampling FFT bins
- `--rms-agg=N`                  Enable (N≠0) or disable (N=0) RMS aggregation
- `--no-rms`                     Disable RMS aggregation
- `--display-noise-floor-db=X`   Noise floor in dBFS for display normalisation (default: -60)
- `--hp-cutoff=X`                High-pass filter cutoff in Hz (aliases: `--hpCutoff=`, `--hp-cutoff X`)

## Voice filtering

- `--voice-only`                 Zero non-voice FFT bins before sending
- `--voice-only=0|1`             Enable/disable voice-only masking (aliases: `--voiceOnly=`, `--voiceOnly`)
- `--voice-min <Hz>`             Voice lower bound in Hz (alias: `--voice-min=`)
- `--voice-max <Hz>`             Voice upper bound in Hz (alias: `--voice-max=`)

## File playback

- `--play-dir <path>`            Play all supported audio files in directory (alias: `--play-dir=`)
- `--play-files=<list>`          Comma-separated list of audio files to play
- `--shuffle-play`               Shuffle file playback order

## Auto-playback (start playback when mic is quiet)

- `--auto-playback`              Enable auto file playback when mic is silent
- `--auto-play-threshold-db=X`  Silence threshold in dBFS to trigger playback (default: -40)
- `--auto-play-hold-ms=X`       How long mic must be silent before playback starts (ms)

## Mic-ducking (duck playback when mic is loud)

State machine: Idle → FadingDown (2 s) → Holding (10 s) → FadingUp (30 s) → Idle.
Re-triggering during Holding or FadingUp restarts the cycle from FadingDown.

- `--suspend-threshold-db=X`    Enable mic-ducking; duck when mic exceeds X dBFS for 500 ms (e.g. -40)

## Test tone

- `--test-tone <Hz>`            Enable built-in sine test tone at given frequency (aliases: `--testTone=`, `--testTone`)

## Diagnostics / development

- `--autoplay-toggle=playMs:cycles`  Toggle auto-playback on/off for testing (cycles=0 = continuous)

---

## Common examples

Send to localhost:57122, shuffle-play files from `numbers/`, duck on voice:

```bash
./build/juce_fft_osc_artefacts/juce_fft_osc --port 57122 --play-dir=./numbers --shuffle-play --suspend-threshold-db=-40
```

Auto-playback when mic is silent:

```bash
./build/juce_fft_osc_artefacts/juce_fft_osc --port 57122 --play-dir=./numbers --auto-playback --auto-play-threshold-db=-40 --auto-play-hold-ms=1500
```

Basic send with diagnostics:

```bash
./build/juce_fft_osc_artefacts/juce_fft_osc --port 57120 --diag-sender
```