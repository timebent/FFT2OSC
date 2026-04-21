#include "FFTOSC.h"

FFTOSC::FFTOSC()
    : window(fftOrder ? (1u << fftOrder) : 2048, juce::dsp::WindowingFunction<float>::hann)
{
    fftSize = 1 << fftOrder;
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    fifo.assign(fftSize, 0.0f);
    fftData.assign(2 * fftSize, 0.0f);
    latestAmplitudes.assign(fftSize / 2, 0.0f);

    // try connecting OSC sender (non-fatal if it fails — we'll log)
    if (! oscSender.connect(oscHost, oscPort))
        juce::Logger::writeToLog("OSC connect failed to " + oscHost + ":" + juce::String(oscPort));
}

FFTOSC::~FFTOSC()
{
    stop();
}

void FFTOSC::start()
{
    deviceManager.initialiseWithDefaultDevices(1, 0);
    deviceManager.addAudioCallback(this);
    startTimerHz(30); // send OSC ~30 times/sec
}

void FFTOSC::stop()
{
    stopTimer();
    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    oscSender.disconnect();
}

void FFTOSC::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    juce::ignoreUnused(device);
}

void FFTOSC::audioDeviceStopped()
{
}

void FFTOSC::audioDeviceIOCallback (const float** inputChannelData, int numInputChannels, float** /*outputChannelData*/, int /*numOutputChannels*/, int numSamples)
{
    // Mix input channels to mono and push into FIFO
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = 0.0f;
        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            const float* in = inputChannelData[ch];
            if (in != nullptr)
                sample += in[i];
        }
        if (numInputChannels > 0)
            sample /= (float) numInputChannels;

        fifo[fifoIndex++] = sample;

        if (fifoIndex >= fftSize)
        {
            // copy fifo into fftData (real parts) and zero imag parts
            for (int j = 0; j < fftSize; ++j)
            {
                fftData[2*j] = fifo[j] * window.getWindowingTable()[j];
                fftData[2*j + 1] = 0.0f;
            }

            // perform FFT in-place
            fft->perform (fftData.data(), fftData.data(), false);

            // compute magnitudes for first half
            std::vector<float> amps(fftSize / 2);
            for (int b = 0; b < fftSize / 2; ++b)
            {
                float re = fftData[2*b];
                float im = fftData[2*b + 1];
                float mag = std::sqrt(re*re + im*im);
                amps[b] = mag;
            }

            // store latest amplitudes under lock
            const juce::ScopedLock sl(amplitudesLock);
            latestAmplitudes = std::move(amps);

            // reset fifo index
            fifoIndex = 0;
        }
    }
}

void FFTOSC::timerCallback()
{
    std::vector<float> ampsCopy;
    {
        const juce::ScopedLock sl(amplitudesLock);
        ampsCopy = latestAmplitudes;
    }

    if (ampsCopy.empty())
        return;

    juce::OSCMessage msg("/fft/amplitudes");
    // add amplitudes as 32-bit floats
    for (auto a : ampsCopy)
        msg.addFloat32(a);

    if (! oscSender.send (msg))
        juce::Logger::writeToLog("OSC send failed");
}
