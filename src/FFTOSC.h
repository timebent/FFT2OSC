#pragma once

#include <JuceHeader.h>
#include <netinet/in.h>
#include <mutex>

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
    // (bin-range feature removed)

    // File playback (urn) support: play files one-at-a-time into the FFT input
    void setPlaybackFiles(const std::vector<juce::String>& paths);
    void setFilePlaybackEnabled(bool on);

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

    juce::CriticalSection amplitudesLock;

    int sendCounter = 0;
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
    float noiseFloorAttack = 0.1f;   // how quickly floor drops when quieter (0..1)
    float noiseFloorRelease = 0.999f; // how slowly floor rises when louder (closer to 1 = slower)
    float noiseFloorInit = 1e6f;     // initial large floor value
    bool useNoiseFloor = true;
    // when true, the configured `noiseFloorInit` is used as a fixed floor
    // and the adaptive attack/release updates are disabled
    bool noiseFloorFixed = false;

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
    // interpolation mode (off by default). When enabled, map visual indices
    // to log-frequency targets and sample FFT magnitudes via fractional-bin interpolation.
    bool interpMode = false;
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

    // diagnostics: how many full OSC payloads to dump when senderDiag is enabled
    int payloadDumpRemaining = 5;

public:
    void setShufflePlayback(bool on) { shufflePlayback = on; juce::Logger::writeToLog(juce::String("Shuffle playback ") + (on ? "enabled" : "disabled")); }

    // definitions are in the .cpp file
    

    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTOSC)
};
