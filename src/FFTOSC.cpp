#include "FFTOSC.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cmath>

FFTOSC::FFTOSC()
{
    fftSize = 1 << fftOrder;
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    fifo.assign(fftSize, 0.0f);
    fftData.assign(2 * fftSize, 0.0f);
    latestAmplitudes.assign(fftSize / 2, 0.0f);

    // try connecting OSC sender (non-fatal) and log result for debugging
    if (oscSender.connect(oscHost, oscPort))
        juce::Logger::writeToLog("OSC connected to " + oscHost + ":" + juce::String(oscPort));
    else
        juce::Logger::writeToLog("OSC connect failed to " + oscHost + ":" + juce::String(oscPort));

    // setup raw UDP debug socket
    debugSock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (debugSock >= 0)
    {
        std::memset(&debugAddr, 0, sizeof(debugAddr));
        debugAddr.sin_family = AF_INET;
        debugAddr.sin_port = htons((uint16_t) oscPort);
        if (inet_pton(AF_INET, oscHost.toRawUTF8(), &debugAddr.sin_addr) == 1)
        {
            debugSockValid = true;
            juce::Logger::writeToLog("Debug UDP socket opened to " + oscHost + ":" + juce::String(oscPort));
        }
        else
        {
            ::close(debugSock);
            debugSock = -1;
            juce::Logger::writeToLog("Debug UDP socket: invalid host");
        }
    }
    else
    {
        juce::Logger::writeToLog("Debug UDP socket creation failed: " + juce::String(std::strerror(errno)));
    }
}

FFTOSC::FFTOSC(const juce::String& host, int port)
    : oscHost(host), oscPort(port)
{
    fftSize = 1 << fftOrder;
    fft = std::make_unique<juce::dsp::FFT>(fftOrder);
    fifo.assign(fftSize, 0.0f);
    fftData.assign(2 * fftSize, 0.0f);
    latestAmplitudes.assign(fftSize / 2, 0.0f);

    if (oscSender.connect(oscHost, oscPort))
        juce::Logger::writeToLog("OSC connected to " + oscHost + ":" + juce::String(oscPort));
    else
        juce::Logger::writeToLog("OSC connect failed to " + oscHost + ":" + juce::String(oscPort));

    // setup raw UDP debug socket
    debugSock = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (debugSock >= 0)
    {
        std::memset(&debugAddr, 0, sizeof(debugAddr));
        debugAddr.sin_family = AF_INET;
        debugAddr.sin_port = htons((uint16_t) oscPort);
        if (inet_pton(AF_INET, oscHost.toRawUTF8(), &debugAddr.sin_addr) == 1)
        {
            debugSockValid = true;
            juce::Logger::writeToLog("Debug UDP socket opened to " + oscHost + ":" + juce::String(oscPort));
        }
        else
        {
            ::close(debugSock);
            debugSock = -1;
            juce::Logger::writeToLog("Debug UDP socket: invalid host");
        }
    }
    else
    {
        juce::Logger::writeToLog("Debug UDP socket creation failed: " + juce::String(std::strerror(errno)));
    }
}

void FFTOSC::setTestTone(bool enabled, float freqHz)
{
    testToneEnabled.store(enabled);
    if (freqHz > 0.0f)
        testFreqHz = freqHz;
    juce::Logger::writeToLog("Test tone " + juce::String(enabled ? "enabled" : "disabled") + 
                             " freq=" + juce::String(testFreqHz));
}

void FFTOSC::setSimpleSend(bool enabled, int bandIndex)
{
    simpleSendEnabled.store(enabled);
    simpleSendBand.store(bandIndex);
    juce::Logger::writeToLog("Simple send " + juce::String(enabled ? "enabled" : "disabled") + " band=" + juce::String(bandIndex));
}

void FFTOSC::setInterpMode(bool enabled)
{
    interpMode = enabled;
    juce::Logger::writeToLog("Interpolation mode " + juce::String(enabled ? "enabled" : "disabled"));
}

void FFTOSC::setMapFreqRange(double minFreq, double maxFreq)
{
    if (minFreq > 0 && maxFreq > minFreq)
    {
        mapMinFreq = minFreq;
        mapMaxFreq = maxFreq;
        juce::Logger::writeToLog("Map freq range set: " + juce::String(mapMinFreq) + " - " + juce::String(mapMaxFreq));
    }
}

void FFTOSC::setSendVoiceOnly(bool enabled)
{
    sendOnlyVoice = enabled;
    juce::Logger::writeToLog("Send-only-voice " + juce::String(enabled ? "enabled" : "disabled"));
}

void FFTOSC::setVoiceRange(double minFreq, double maxFreq)
{
    if (minFreq > 0 && maxFreq > minFreq)
    {
        voiceMinFreq = minFreq;
        voiceMaxFreq = maxFreq;
        juce::Logger::writeToLog("Voice range set: " + juce::String(voiceMinFreq) + " - " + juce::String(voiceMaxFreq));
    }
}

FFTOSC::~FFTOSC()
{
    stop();
    if (debugSockValid && debugSock >= 0)
        ::close(debugSock);
}

void FFTOSC::start()
{
    if (!simpleSendEnabled.load())
    {
        deviceManager.initialiseWithDefaultDevices(1, 0);
        deviceManager.addAudioCallback(this);
    }
    else
    {
        juce::Logger::writeToLog("Simple sender enabled: skipping audio device init");
    }

    // start background sender thread
    senderRunning.store(true);
    senderThread = std::thread([this]() {
        while (senderRunning.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(senderIntervalMs));

            // copy latest amplitudes or generate synthetic bins when in simple-send mode
            std::vector<float> sendBins;
            sendBins.reserve(outBins);
            float globalMax = 0.0f;
            int globalMaxIndex = 0;
            int nonZeroCount = 0;
            int expectedBin = -1;

            if (simpleSendEnabled.load())
            {
                // generate synthetic band array: single active band (or sweep if band out of range)
                sendBins.assign(outBins, 0.0f);
                int band = simpleSendBand.load();
                if (band < 0 || band >= outBins)
                {
                    int b = (int)std::floor(simpleSweepPos) % outBins;
                    sendBins[b] = 1.0f;
                    simpleSweepPos += 1.0;
                    if (simpleSweepPos >= outBins) simpleSweepPos -= outBins;
                    globalMax = 1.0f;
                    globalMaxIndex = b;
                    nonZeroCount = 1;
                }
                else
                {
                    sendBins[band] = 1.0f;
                    globalMax = 1.0f;
                    globalMaxIndex = band;
                    nonZeroCount = 1;
                }
                // no FFT inputs, expectedBin is N/A here (we're sending bands directly)
            }
            else
            {
                // copy latest amplitudes
                std::vector<float> ampsCopy;
                {
                    const juce::ScopedLock sl(amplitudesLock);
                    ampsCopy = latestAmplitudes;
                }

                if (ampsCopy.empty())
                    continue;

                // downsample to outBins using logarithmic (geometric) banding
                size_t total = ampsCopy.size();
                if (total == 0)
                    continue;

            // build log-spaced edges from 0..total OR perform fractional-bin interpolation
            std::vector<int> edges;
            edges.reserve(outBins + 1);
            double logMin = std::log(1.0);
            double logMax = std::log((double)total);
            if (!interpMode)
            {
                edges.push_back(0);
                for (int k = 1; k < outBins; ++k)
                {
                    double t = (double)k / (double)outBins;
                    int idx = (int)std::floor(std::exp(logMin + (logMax - logMin) * t));
                    if (idx < 1) idx = 1;
                    if (idx > (int)total - 1) idx = (int)total - 1;
                    edges.push_back(idx);
                }
                edges.push_back((int)total);
            }

                // compute global peak and count of non-zero bins (before downsampling)
                const float zeroEps = 1e-9f;
                for (size_t i = 0; i < total; ++i)
                {
                    float v = ampsCopy[i];
                    if (std::isfinite(v) && v > globalMax)
                    {
                        globalMax = v;
                        globalMaxIndex = (int)i;
                    }
                    if (std::isfinite(v) && v > zeroEps)
                        ++nonZeroCount;
                }

                if (!interpMode)
                {
                    for (int b = 0; b < outBins; ++b)
                    {
                        int start = edges[b];
                        int end = edges[b+1];
                        if (start >= (int)total) { sendBins.push_back(0.0f); continue; }
                        end = std::min(end, (int)total);
                        float m = 0.0f;
                        for (int i = start; i < end; ++i)
                        {
                            float v = ampsCopy[(size_t)i];
                            if (std::isfinite(v) && v > m) m = v;
                        }
                        sendBins.push_back(m);
                    }
                }
                else
                {
                    // fractional-bin interpolation per visual output index (log-frequency mapping)
                    double logRange = std::log10(mapMaxFreq / mapMinFreq);
                    for (int b = 0; b < outBins; ++b)
                    {
                        double t = (outBins > 1) ? (double)b / (double)(outBins - 1) : 0.0;
                        double targetFreq = mapMinFreq * std::pow(10.0, t * logRange);
                        // map frequency to FFT bin index: use full fftSize (not half-length 'total')
                        double binFloat = targetFreq * (double)fftSize / currentSampleRate;
                        int i0 = (int)std::floor(binFloat);
                        if (i0 < 0) i0 = 0;
                        if (i0 > (int)total - 1) i0 = (int)total - 1;
                        int i1 = std::min(i0 + 1, (int)total - 1);
                        float v0 = ampsCopy[(size_t)i0];
                        float v1 = ampsCopy[(size_t)i1];
                        double frac = binFloat - std::floor(binFloat);
                        if (!std::isfinite(v0)) v0 = 0.0f;
                        if (!std::isfinite(v1)) v1 = 0.0f;
                        float interp = (float)(v0 + frac * (v1 - v0));
                        sendBins.push_back(interp);
                    }
                }

                // If test tone is enabled, compute expected bin for the tone
                if (testToneEnabled.load() && currentSampleRate > 0.0)
                {
                    expectedBin = (int)std::lround((double)testFreqHz * (double)fftSize / currentSampleRate);
                }
            }

            // sanitize and scale magnitudes, then convert to dB and normalize to 0..1
            // scale by fftSize to bring FFT output into a sane amplitude range
            const float eps = 1e-12f;
            const float minDb = -80.0f;
            const float maxDb = 0.0f;
            const float maxReasonable = 1e12f; // clamp extreme values
            const float scale = (fftSize > 0) ? (1.0f / (float) fftSize) : 1.0f;

            for (size_t i = 0; i < sendBins.size(); ++i)
            {
                float v = sendBins[i];
                if (!std::isfinite(v))
                    v = 0.0f;
                if (v < 0.0f)
                    v = 0.0f;
                if (v > maxReasonable)
                    v = maxReasonable;
                v *= scale;
                sendBins[i] = v;
            }

            // If requested, zero out bins outside the voice frequency range
            if (sendOnlyVoice)
            {
                double logRange = (mapMaxFreq > mapMinFreq) ? std::log10(mapMaxFreq / mapMinFreq) : 0.0;
                for (int b = 0; b < (int)sendBins.size(); ++b)
                {
                    double t = (outBins > 1) ? (double)b / (double)(outBins - 1) : 0.0;
                    double targetFreq = mapMinFreq * std::pow(10.0, t * logRange);
                    if (targetFreq < voiceMinFreq || targetFreq > voiceMaxFreq)
                        sendBins[(size_t)b] = 0.0f;
                }
            }

            // initialize noise floor vector to match sendBins size
            if (useNoiseFloor)
            {
                if (noiseFloor.size() != sendBins.size())
                    noiseFloor.assign(sendBins.size(), noiseFloorInit);

                // apply attack/release smoothing to noise floor and subtract
                for (size_t i = 0; i < sendBins.size(); ++i)
                {
                    float mag = sendBins[i];
                    if (!std::isfinite(mag)) mag = 0.0f;
                    // update floor: drop quickly when mag < floor (attack), rise slowly when mag > floor (release)
                    if (mag < noiseFloor[i])
                        noiseFloor[i] = noiseFloorAttack * mag + (1.0f - noiseFloorAttack) * noiseFloor[i];
                    else
                        noiseFloor[i] = noiseFloorRelease * noiseFloor[i] + (1.0f - noiseFloorRelease) * mag;

                    float sub = mag - noiseFloor[i];
                    if (!std::isfinite(sub) || sub <= 0.0f)
                        sendBins[i] = 0.0f;
                    else
                        sendBins[i] = sub;
                }
            }
            // Debug: log raw values and diagnostics before normalization for first few sends
            if (sendLogLimit > 0)
            {
                juce::String rawS = "raw[0..7]: ";
                for (int i = 0; i < 8 && i < (int)sendBins.size(); ++i)
                    rawS += juce::String(sendBins[(size_t)i]) + " ";
                rawS += " | globalMax=" + juce::String(globalMax) + " idx=" + juce::String(globalMaxIndex) + " nonZero=" + juce::String(nonZeroCount);
                if (expectedBin >= 0)
                    rawS += " expectedBin=" + juce::String(expectedBin);
                if (testToneEnabled.load())
                {
                    double logRange10 = std::log10(mapMaxFreq / mapMinFreq);
                    double tvis = 0.0;
                    if (testFreqHz > 0.0 && logRange10 > 0.0)
                        tvis = std::log10((double)testFreqHz / mapMinFreq) / logRange10;
                    int expectedVis = (int)std::lround(tvis * (double)(outBins - 1));
                    if (expectedVis < 0) expectedVis = 0;
                    if (expectedVis > outBins - 1) expectedVis = outBins - 1;
                    rawS += " expectedVis=" + juce::String(expectedVis);
                }
                juce::Logger::writeToLog(rawS);
            }
            for (size_t i = 0; i < sendBins.size(); ++i)
            {
                float mag = sendBins[i];
                float db = 20.0f * std::log10f(mag + eps);
                float norm = (db - minDb) / (maxDb - minDb);
                if (!std::isfinite(norm)) norm = 0.0f;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                sendBins[i] = norm;
            }
            if (sendLogLimit > 0)
            {
                juce::String normS = "norm[0..7]: ";
                for (int i = 0; i < 8 && i < (int)sendBins.size(); ++i)
                    normS += juce::String(sendBins[(size_t)i]) + " ";
                juce::Logger::writeToLog(normS);
                --sendLogLimit;
            }

            // Ensure we always send exactly outBins floats (pad with 0.0 or truncate)
            if ((int)sendBins.size() < outBins)
            {
                sendBins.resize(outBins, 0.0f);
            }
            else if ((int)sendBins.size() > outBins)
            {
                sendBins.resize(outBins);
            }

            // send using JUCE OSCMessage (ensures proper OSC formatting)
            juce::OSCMessage msg("/fft/amplitudes");
            for (int i = 0; i < outBins; ++i)
                msg.addFloat32(sendBins[(size_t)i]);
            bool ok = false;
            {
                std::lock_guard<std::mutex> lock(sendMutex);
                ok = oscSender.send(msg);
            }
            if (!ok)
                juce::Logger::writeToLog("OSC send failed (oscSender)");
            else if (sendLogLimit > 0)
            {
                juce::String s = "Sent /fft/amplitudes count=" + juce::String(outBins) + " first=" + juce::String(sendBins[0]) + " (verbose)";
                juce::Logger::writeToLog(s);
                --sendLogLimit;
            }
        }
    });
}

void FFTOSC::stop()
{
    // stop sender thread
    senderRunning.store(false);
    if (senderThread.joinable())
        senderThread.join();

    deviceManager.removeAudioCallback(this);
    deviceManager.closeAudioDevice();
    oscSender.disconnect();
}

void FFTOSC::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    if (device != nullptr)
    {
        currentSampleRate = device->getCurrentSampleRate();
        juce::String s = "Audio device started: " + device->getName() + " rate=" + juce::String(currentSampleRate);
        s += " inputs=" + juce::String(device->getActiveInputChannels().countNumberOfSetBits());
        juce::Logger::writeToLog(s);
    }
}

void FFTOSC::audioDeviceStopped()
{
    juce::Logger::writeToLog("Audio device stopped");
}

void FFTOSC::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels, float* const* /*outputChannelData*/, int /*numOutputChannels*/, int numSamples, const juce::AudioIODeviceCallbackContext& /*context*/)
{
    // Mix input channels to mono and push into FIFO
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = 0.0f;

        if (testToneEnabled.load())
        {
            // generate sine test tone
            double inc = 2.0 * M_PI * (double)testFreqHz / currentSampleRate;
            sample = (float)std::sin(phase);
            phase += inc;
            if (phase > (2.0 * M_PI)) phase -= (2.0 * M_PI);
        }
        else
        {
            for (int ch = 0; ch < numInputChannels; ++ch)
            {
                const float* in = inputChannelData[ch];
                if (in != nullptr)
                    sample += in[i];
            }
            if (numInputChannels > 0)
                sample /= (float) numInputChannels;
        }

        fifo[fifoIndex++] = sample;

        if (fifoIndex >= fftSize)
        {
            // copy fifo into fftData (first half)
            for (int j = 0; j < fftSize; ++j)
                fftData[j] = fifo[j];

            // remove DC offset (mean) before windowing to avoid DC dominating low bins
            {
                float mean = 0.0f;
                for (int j = 0; j < fftSize; ++j)
                    mean += fftData[j];
                mean /= (float) fftSize;
                if (std::abs(mean) > 1e-12f)
                {
                    for (int j = 0; j < fftSize; ++j)
                        fftData[j] -= mean;
                }
            }

            // apply window in-place
            window.multiplyWithWindowingTable(fftData.data(), (size_t) fftSize);

            // perform frequency-only forward transform (outputs magnitudes into fftData)
            fft->performFrequencyOnlyForwardTransform(fftData.data(), true);

            // copy first half magnitudes
            std::vector<float> amps(fftSize / 2);
            for (int b = 0; b < fftSize / 2; ++b)
                amps[b] = fftData[b];

            // store latest amplitudes under lock
            const juce::ScopedLock sl(amplitudesLock);
            latestAmplitudes = std::move(amps);

            // occasional log to confirm FFT was produced
            if (++fftProducedCounter % 10 == 0)
            {
                juce::String s = "FFT produced count=" + juce::String(latestAmplitudes.size()) + " first=" + juce::String(latestAmplitudes[0]);
                juce::Logger::writeToLog(s);
            }

            // reset fifo index
            fifoIndex = 0;
        }
    }
}

void FFTOSC::timerCallback()
{
    if (++timerCallCounter <= 10)
        juce::Logger::writeToLog("timerCallback invoked");

    std::vector<float> ampsCopy;
    {
        const juce::ScopedLock sl(amplitudesLock);
        ampsCopy = latestAmplitudes;
    }

    if (ampsCopy.empty())
    {
        // occasionally report that we have no data yet
        if (++sendCounter % 30 == 0)
            juce::Logger::writeToLog("No FFT amplitudes available yet");
        return;
    }
    // Sending is now handled by the sender thread; timerCallback will not transmit OSC.
}
