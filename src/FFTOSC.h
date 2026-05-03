#pragma once

#include <JuceHeader.h>
#include <netinet/in.h>
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
    // simple sender mode (bypass audio/FFT and send synthetic bands)
    void setSimpleSend(bool enabled, int bandIndex);
    // enable fractional-bin interpolation mapping (off by default)
    void setInterpMode(bool enabled);
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
    // enable/disable per-band noise-floor subtraction
    void setUseNoiseFloor(bool on);
    void setNoiseFloorInit(float initVal);
    void setNoiseFloorFixed(bool on);
    // Force sender to emit all-zero payloads (testing/debug)
    void setForceSilence(bool on);
    // Auto-playback controls
    void setAutoPlayback(bool on);
    void setAutoPlayThresholdDb(double db);
    void setAutoPlayHoldMs(int ms);
    // Mic-driven fade controls: when enabled, playback gain will be reduced
    // if the input peak exceeds the configured threshold (linear dB scale).
    void setMicFadeOnInput(bool on);
    void setMicFadeThresholdDb(double db);
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
    // control verbose logging (disable to suppress noisy diagnostics)
    void setVerboseLogging(bool on);

    // Test helper: simulate sustained mic input for `durationMs` milliseconds
    // at `micLevel` (linear 0..1). This will repeatedly invoke the timer
    // logic so fades are exercised without real microphone input.
    void runForcedFadeTest(int durationMs, float micLevel);
    // Run a two-phase forced fade test: sustained high mic for `highMs`,
    // then sustained low mic at `lowLevel` for `lowMs` to exercise resume.
    void runForcedFadeResumeTest(int highMs, int lowMs, float lowLevel);

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

    // debug raw UDP socket to verify OS-level emission
    int debugSock = -1;
    bool debugSockValid = false;
    struct sockaddr_in debugAddr;

    // sender thread (avoids relying on JUCE Timer / MessageThread)
    std::thread senderThread;
    std::atomic<bool> senderRunning{false};
    int senderIntervalMs = 33; // ~30 Hz
    int outBins = 64; // number of bins to send over OSC (downsampled)
    // diagnostic: when enabled, sender will log a timestamp for each OSC packet sent
    bool senderDiag = false;
    std::atomic<int> senderDiagCount{0};

    // per-band noise floor (to subtract background energy)
    std::vector<float> noiseFloor;
    float noiseFloorAttack = 0.5f;   // how quickly floor drops when quieter (0..1). Larger -> faster
    float noiseFloorRelease = 0.9f; // how quickly floor rises when louder (smaller -> faster)
    float noiseFloorInit = 1e6f;     // initial large floor value
    bool useNoiseFloor = true;
    // when true, the configured `noiseFloorInit` is used as a fixed floor
    // and the adaptive attack/release updates are disabled
    bool noiseFloorFixed = false;

    // Mic-driven fade: when enabled, playback gain is reduced
    // if the input peak exceeds the configured threshold (linear dB scale).
    bool micFadeOnInput = false;
    double micFadeThresholdDb = std::numeric_limits<double>::quiet_NaN();
    double micFadeThresholdLinear = 0.0;

    // simple synthetic sender (bypass audio/FFT)
    std::atomic<bool> simpleSendEnabled{false};
    std::atomic<int> simpleSendBand{0};
    double simpleSweepPos = 0.0;

    juce::OSCSender oscSender;
    juce::String oscHost = "127.0.0.1";
    int oscPort = 57120;
    std::mutex sendMutex;

    // test tone generator (useful for verifying FFT/bin mapping)
    std::atomic<bool> testToneEnabled{false};
    float testFreqHz = 440.0f;
    double phase = 0.0;
    double currentSampleRate = 44100.0;
    // periodic logging for test tone active state
    int testToneLogCounter = 0; // milliseconds accumulated in sender thread
    int testToneLogIntervalMs = 3000; // log once every ~3 seconds when enabled
    // interpolation mode (enabled by default). When enabled, map visual indices
    // to log-frequency targets and sample FFT magnitudes via fractional-bin interpolation.
    bool interpMode = true;
    // Use RMS (root-mean-square) aggregation when downsampling FFT bins into visual bands.
    // RMS normalizes for differing numbers of FFT bins per visual band and reflects energy.
    bool useRMSAggregation = true;
    // (bin-range feature removed)
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

    // When true, the sender will send all-zero payloads regardless of FFT
    bool forceSilence = false;

    // Minimum dB used when converting magnitudes to normalized 0..1 for display
    float displayMinDb = -80.0f;

    // runtime verbosity: when false, suppress non-essential diagnostic logs
    bool verboseLogging = true;

    // push samples directly into FIFO and run FFT when ready (usable by feeder)
    void pushSamplesToFFT(const float* samples, int numSamples);

    // timestamp of last playback start (ms since steady_clock epoch). Used to
    // suppress immediate mic-detection of playback. Stored as atomic to allow
    // safe real-time thread reads without locking.
    std::atomic<long long> lastPlaybackStartMs{0};
    // last observed input absolute level (max per audio callback). Updated
    // from the audio thread and sampled by timerCallback for consistent
    // mic-fade decisions.
    std::atomic<float> lastInputLevel{0.0f};
    // last observed input RMS level (per-audio-callback RMS of mic samples)
    std::atomic<float> lastInputRms{0.0f};
    // Counters used by the RMS-triggered fade state machine (ms)
    std::atomic<int> micAboveMs{0};
    // Counter for how long mic has been below threshold (ms)
    std::atomic<int> micBelowMs{0};
    // Whether the mic was previously above the detection threshold long enough
    // to trigger a fade-down. Used to detect the "cross from above to below"
    // transition so resume timing only begins after that event.
    std::atomic<bool> micWasAbove{false};
    // Fade configuration: ms above threshold to fade down (automatic fade-up removed)
    int micFadeThresholdMsHigh = 500;
    // ms below threshold to resume playback (auto-fade-up). Default 10s.
    int micFadeResumeMs = 10000;
    // Playback gain state (0.0..1.0) and fade params
    std::atomic<float> playbackGain{1.0f};
    float playbackFadeDownGain = 0.0f; // target gain when mic active (fade to silence)
    int playbackFadeDurationMs = 2000; // duration of fade-down in ms (2s)
    int playbackFadeUpDurationMs = 5000; // duration of fade-up (resume) in ms (5s)
    // grace period in milliseconds after starting playback during which mic
    // activity will be ignored to avoid immediately stopping newly-started files.
    // Set to 0 to disable startup suppression.
    int playbackStartupGraceMs = 0;
    void setPlaybackStartupGraceMs(int ms) { if (ms >= 0) playbackStartupGraceMs = ms; }

    // Audio gate: delegates mic-driven fade/resume logic
    // AudioGate removed: mic-driven fade logic simplified/disabled

public:
    void setShufflePlayback(bool on) { shufflePlayback = on; juce::Logger::writeToLog(juce::String("Shuffle playback ") + (on ? "enabled" : "disabled")); }

    // definitions are in the .cpp file
    

    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTOSC)
};
