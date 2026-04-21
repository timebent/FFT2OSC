#pragma once

#include <JuceHeader.h>

class FFTOSC : public juce::AudioIODeviceCallback, private juce::Timer
{
public:
    FFTOSC();
    ~FFTOSC() override;

    void start();
    void stop();

    // Audio callback
    void audioDeviceIOCallback (const float** inputChannelData, int numInputChannels, float** outputChannelData, int numOutputChannels, int numSamples) override;
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    void timerCallback() override;

    juce::AudioDeviceManager deviceManager;
    std::unique_ptr<juce::dsp::FFT> fft;
    juce::dsp::WindowingFunction<float> window;

    std::vector<float> fifo;
    int fifoIndex = 0;
    int fftOrder = 11; // 2048
    int fftSize = 0;

    std::vector<float> fftData; // real+imag interleaved for FFT
    std::vector<float> latestAmplitudes;

    juce::CriticalSection amplitudesLock;

    juce::OSCSender oscSender;
    juce::String oscHost = "127.0.0.1";
    int oscPort = 9000;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTOSC)
};
