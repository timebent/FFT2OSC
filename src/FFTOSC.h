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

private:
    void timerCallback() override;

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
    int sendLogLimit = 20; // log the first N sends verbosely for debugging
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

    // per-band noise floor (to subtract background energy)
    std::vector<float> noiseFloor;
    float noiseFloorAttack = 0.1f;   // how quickly floor drops when quieter (0..1)
    float noiseFloorRelease = 0.999f; // how slowly floor rises when louder (closer to 1 = slower)
    float noiseFloorInit = 1e6f;     // initial large floor value
    bool useNoiseFloor = true;

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
    // interpolation mode (off by default). When enabled, map visual indices
    // to log-frequency targets and sample FFT magnitudes via fractional-bin interpolation.
    bool interpMode = false;
    double mapMinFreq = 80.0;
    double mapMaxFreq = 24000.0;
    // voice-range filtering: when true, non-voice bins will be zeroed before sending
    bool sendOnlyVoice = false;
    double voiceMinFreq = 80.0;   // default human voice lower bound (Hz)
    double voiceMaxFreq = 3000.0; // default upper bound for harmonics (Hz)

    

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTOSC)
};
