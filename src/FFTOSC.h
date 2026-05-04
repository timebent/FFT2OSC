#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <atomic>

class FFTOSC : public juce::AudioIODeviceCallback, private juce::Timer
{
public:
    FFTOSC();
    FFTOSC(const juce::String& host, int port);
    ~FFTOSC() override;

    void start();
    void stop();

    // Audio callback (new JUCE signature)
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    // test tone control (exposed publicly for CLI/main)
    void setTestTone(bool enabled, float freqHz);
    // sender interval control (ms) for background sender thread
    void setSenderIntervalMs(int ms);
    int getSenderIntervalMs() const { return senderIntervalMs; }
    // simple sender mode (bypass audio/FFT and send synthetic bands) - removed
    // fractional-bin interpolation mapping is always enabled
    // mapping and voice-range control (public)
    void setMapFreqRange(double minFreq, double maxFreq);
    void setSendVoiceOnly(bool enabled);
    void setVoiceRange(double minFreq, double maxFreq);
    double getVoiceMinFreq() const { return voiceMinFreq; }
    double getVoiceMaxFreq() const { return voiceMaxFreq; }
    void setSenderDiagnostic(bool on);
    // Control whether RMS aggregation is used when downsampling FFT bins to visual bands.
    void setUseRMSAggregation(bool on);
    bool getUseRMSAggregation() const;
    // (per-band noise-floor processing removed)
    // Mic-ducking: fade playback gain to zero when mic RMS/peak exceeds a dBFS threshold.
    // Fade speed is controlled by playbackFadeDurationMs (down) and playbackFadeUpDurationMs (up).
    void setMicDuckEnabled(bool on);
    void setMicDuckThresholdDb(double db);
    bool getMicDuckEnabled() const { return micDuckEnabled; }
    double getMicDuckThresholdDb() const { return micDuckThresholdDb; }
    // Auto-playback controls
    void setAutoPlayback(bool on);
    void setAutoPlayThresholdDb(double db);
    void setAutoPlayHoldMs(int ms);
    // (mic-fade and force-silence features removed)
    // (bin-range feature removed)

    // File playback (urn) support: play files one-at-a-time into the FFT input
    void setPlaybackFiles(const std::vector<juce::String>& paths);
    void setFilePlaybackEnabled(bool on);

    // Force the file feeder fallback even when OS outputs exist
    void setForceFileFeeder(bool on);

    // Autoplay toggle test: automatically enable file playback for `playMs` milliseconds
    // then disable to return to mic input, repeating for `cycles` times (0 = infinite).
    void startAutoplayToggleTest(int playMs, int cycles);
    void stopAutoplayToggleTest();

    // Adjust display noise-floor (minimum dB used when mapping magnitudes to 0..1)
    void setDisplayNoiseFloorDb(double db);
    double getDisplayNoiseFloorDb() const;
    // High-pass filter control
    void setHighPassCutoffHz(double hz);
    double getHighPassCutoffHz() const { return hpCutoffHz; }
    // control verbose logging (disable to suppress noisy diagnostics)
    void setVerboseLogging(bool on);

    // (mic-fade test helpers removed)

private:
    void timerCallback() override;
    // background sender loop moved out of start() for clarity
    void senderLoop();
    void logBandCenters();

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::dsp::FFT> fft;

    std::vector<float> fifo;
    int fifoIndex = 0;
    int fftOrder = 11; // 2048
    int fftSize = 0;

    juce::dsp::WindowingFunction<float> window{2048, juce::dsp::WindowingFunction<float>::hann};

    std::vector<float> fftData; // buffer for real samples and frequency/magnitude output
    std::vector<float> latestAmplitudes;

    juce::CriticalSection amplitudesLock; // ### What is this???

    int sendCounter = 0; // ###
    int sendLogLimit = 0; // controls verbose send logging (0 = disabled)
    int fftProducedCounter = 0;
    int timerCallCounter = 0;

    // (raw debug UDP socket removed; use juce::OSCSender)

    // sender thread (avoids relying on JUCE Timer / MessageThread)
    std::thread senderThread;
    std::atomic<bool> senderRunning{false};
    int senderIntervalMs = 33; // ~30 Hz
    int outBins = 64; // number of bins to send over OSC (downsampled)
    // diagnostic: when enabled, sender will log a timestamp for each OSC packet sent
    bool senderDiag = false;
    std::atomic<int> senderDiagCount{0};

    juce::OSCSender oscSender;
    juce::String oscHost = "127.0.0.1";
    int oscPort = 57120;
    std::mutex sendMutex;

    // test tone generator (useful for verifying FFT/bin mapping)
    std::atomic<bool> testToneEnabled{false};
    float testFreqHz = 440.0f;
    double phase = 0.0;
    double currentSampleRate = 44100.0;
    // High-pass filter state for microphone input (simple 1st-order HP)
    double hpCutoffHz = 300.0; // default HP cutoff
    double hpAlpha = 1.0; // computed from sample rate and cutoff
    float hpXPrev = 0.0f;
    float hpYPrev = 0.0f;
    bool hpEnabled = true;
    // periodic logging for test tone active state
    int testToneLogCounter = 0; // milliseconds accumulated in sender thread
    int testToneLogIntervalMs = 3000; // log once every ~3 seconds when enabled
    // fractional-bin interpolation is always used to sample magnitudes.
    // Use RMS (root-mean-square) aggregation when downsampling FFT bins into visual bands.
    // RMS normalizes for differing numbers of FFT bins per visual band and reflects energy.
    bool useRMSAggregation = true;
    double mapMinFreq = 80.0;
    double mapMaxFreq = 24000.0;
    // voice-range filtering: when true, non-voice bins will be zeroed before sending
    bool sendOnlyVoice = false;
    double voiceMinFreq = 80.0;   // default human voice lower bound (Hz)
    double voiceMaxFreq = 3000.0; // default upper bound for harmonics (Hz)

private:
    // internal for file playback
    juce::AudioFormatManager formatManager;
    std::vector<juce::File> playbackFiles;
    std::vector<int> urnIndices; // remaining indices in the current urn
    bool filePlaybackEnabled = false;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    juce::AudioTransportSource transportSource;
    double currentFileLengthSeconds = 0.0;
    int currentFileIndex = -1;
    std::atomic<bool> nextFileRequested{false};
    // playback thread to robustly advance files without relying on JUCE Timer
    std::thread playbackThread;
    std::atomic<bool> playbackThreadRunning{false};
    std::mutex playbackMutex;
    int sequentialIndex = 0; // next index for sequential playback
    bool shufflePlayback = false;

    // autoplay-toggle test helper
    std::thread autoplayTestThread;
    std::atomic<bool> autoplayTestRunning{false};

    // diagnostics: how many full OSC payloads to dump when senderDiag is enabled
    int payloadDumpRemaining = 5;

    // Fallback feeder thread when no audio outputs are available: reads files
    // directly and feeds samples into the FFT path for analysis.
    std::thread fileFeederThread;
    std::atomic<bool> fileFeederRunning{false};
    std::mutex feederMutex;

    // When true, force the fileFeeder path even if OS outputs are available
    bool forceFileFeeder = false;

    // Minimum dB used when converting magnitudes to normalized 0..1 for display
    // Default to -60 dB (display noise-floor enabled by default)
    float displayMinDb = -60.0f;

    // runtime verbosity: when false, suppress non-essential diagnostic logs
    bool verboseLogging = false;

    // push samples directly into FIFO and run FFT when ready (usable by feeder)
    void pushSamplesToFFT(const float* samples, int numSamples);

    // timestamp of last playback start (ms since steady_clock epoch). Used to
    // suppress immediate mic-detection of playback. Stored as atomic to allow
    // safe real-time thread reads without locking.
    std::atomic<long long> lastPlaybackStartMs{0};
    // last observed input absolute level (max per audio callback). Updated
    // from the audio thread and sampled by timerCallback for diagnostics.
    std::atomic<float> lastInputLevel{0.0f};
    // last observed input RMS level (per-audio-callback RMS of mic samples)
    std::atomic<float> lastInputRms{0.0f};
    // Playback gain state (0.0..1.0) and fade params
    std::atomic<float> playbackGain{1.0f};
    float playbackFadeDownGain = 0.0f; // target gain when mic active (fade to silence)
    int playbackFadeDurationMs = 2000; // duration of fade-down in ms (2s)
    int playbackFadeUpDurationMs = 30000; // duration of fade-up (resume) in ms (30s)
    // grace period in milliseconds after starting playback during which mic
    // activity will be ignored to avoid immediately stopping newly-started files.
    // Set to 0 to disable startup suppression.
    int playbackStartupGraceMs = 0;
    void setPlaybackStartupGraceMs(int ms) { if (ms >= 0) playbackStartupGraceMs = ms; }
    // Mic-ducking state machine
    // Idle → (500ms above threshold) → FadingDown (2s) → Holding (10s) → FadingUp (30s) → Idle
    enum class DuckState { Idle, FadingDown, Holding, FadingUp };
    bool micDuckEnabled = false;
    double micDuckThresholdDb = -40.0; // dBFS; overridden by setMicDuckThresholdDb
    int micDuckHoldMs = 500;           // ms above threshold before duck triggers
    int micAboveThresholdAccMs = 0;    // accumulated ms above threshold (sender-thread only)
    int duckHoldDurationMs = 10000;    // ms to hold silence before fading back up
    int duckHoldAccMs = 0;             // accumulated ms at silence
    DuckState duckState = DuckState::Idle;

    // Audio gate and mic-fade logic removed.

public:
    void setShufflePlayback(bool on) { shufflePlayback = on; juce::Logger::writeToLog(juce::String("Shuffle playback ") + (on ? "enabled" : "disabled")); }

    // definitions are in the .cpp file
    

    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTOSC)
};
