#include "FFTOSC.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <cmath>
#include <random>

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

    formatManager.registerBasicFormats();
}

void FFTOSC::logBandCenters()
{
    if (outBins <= 0) return;
    double logRange = (mapMaxFreq > mapMinFreq) ? std::log10(mapMaxFreq / mapMinFreq) : 0.0;
    juce::String s = "Band centers (Hz):";
    for (int b = 0; b < outBins; ++b)
    {
        double t = (outBins > 1) ? (double)b / (double)(outBins - 1) : 0.0;
        double targetFreq = mapMinFreq * std::pow(10.0, t * logRange);
        s += " " + juce::String((double)std::round(targetFreq * 100.0) / 100.0);
    }
    juce::Logger::writeToLog(s);
}

void FFTOSC::senderLoop()
{
    while (senderRunning.load())
    {
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

            // If send-only-voice mode is enabled, zero raw FFT bins whose
            // center frequency lies outside the requested voice/map range.
            // This ensures downstream aggregation cannot pick up energy from
            // outside the requested interval.
            if (sendOnlyVoice && !ampsCopy.empty() && currentSampleRate > 0.0)
            {
                for (size_t i = 0; i < ampsCopy.size(); ++i)
                {
                    double freq = (double)i * currentSampleRate / (double)fftSize;
                    if (freq < voiceMinFreq || freq > voiceMaxFreq)
                        ampsCopy[i] = 0.0f;
                }
            }

            if (ampsCopy.empty())
            {
                // no FFT yet; wait a short time and retry so we can send immediately once available
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }

            // downsample to outBins using logarithmic (geometric) banding
            size_t total = ampsCopy.size();
            if (total == 0)
                continue;

            {
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
                    // Aggregate FFT bins into output visual bands.
                    for (int b = 0; b < outBins; ++b)
                    {
                        int start = edges[b];
                        int end = edges[b+1];
                        if (start >= (int)total) { sendBins.push_back(0.0f); continue; }
                        end = std::min(end, (int)total);
                        int count = end - start;
                        if (count <= 0)
                        {
                            sendBins.push_back(0.0f);
                            continue;
                        }
                        double sumSq = 0.0;
                        for (int i = start; i < end; ++i)
                        {
                            float v = ampsCopy[(size_t)i];
                            if (!std::isfinite(v) || v <= 0.0f)
                                v = 0.0f;
                            sumSq += (double)v * (double)v;
                        }
                        double meanSq = sumSq / (double)count;
                        float rms = (float)std::sqrt(meanSq);
                        sendBins.push_back(rms);
                    }
                }
                else
                {
                    double logRange = std::log10(mapMaxFreq / mapMinFreq);
                    for (int b = 0; b < outBins; ++b)
                    {
                        double t = (outBins > 1) ? (double)b / (double)(outBins - 1) : 0.0;
                        double targetFreq = mapMinFreq * std::pow(10.0, t * logRange);
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

                if (testToneEnabled.load() && currentSampleRate > 0.0)
                {
                    expectedBin = (int)std::lround((double)testFreqHz * (double)fftSize / currentSampleRate);
                }
            }

        }

        // sanitize and scale magnitudes, then convert to dB and normalize to 0..1
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

        // (Voice-only masking will be applied after normalization so that
        // noise-floor subtraction and dB normalization cannot re-introduce
        // energy into bands outside the requested voice range.)

        // initialize noise floor vector to match sendBins size; allow fixed-or-adaptive modes
        if (useNoiseFloor)
        {
            if (noiseFloorFixed)
            {
                // Fixed floor: subtract the configured init value from every band
                for (size_t i = 0; i < sendBins.size(); ++i)
                {
                    float mag = sendBins[i];
                    if (!std::isfinite(mag)) mag = 0.0f;
                    float sub = mag - noiseFloorInit;
                    if (!std::isfinite(sub) || sub <= 0.0f)
                        sendBins[i] = 0.0f;
                    else
                        sendBins[i] = sub;
                }
            }
            else
            {
                if (noiseFloor.size() != sendBins.size())
                    noiseFloor.assign(sendBins.size(), noiseFloorInit);

                for (size_t i = 0; i < sendBins.size(); ++i)
                {
                    float mag = sendBins[i];
                    if (!std::isfinite(mag)) mag = 0.0f;
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
        }

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
        // Enforce voice-only masking after normalization so that no subsequent
        // processing can re-introduce energy into bins outside the voice range.
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
        if (sendLogLimit > 0)
        {
            juce::String normS = "norm[0..7]: ";
            for (int i = 0; i < 8 && i < (int)sendBins.size(); ++i)
                normS += juce::String(sendBins[(size_t)i]) + " ";
            juce::Logger::writeToLog(normS);
            --sendLogLimit;
        }

        // Brief amplitude logging when diagnostics are enabled
        if (senderDiag)
        {
            juce::String ampS = "AMP[0..7]: ";
            for (int i = 0; i < 8 && i < (int)sendBins.size(); ++i)
                ampS += juce::String(sendBins[(size_t)i]) + " ";
            juce::Logger::writeToLog(ampS);
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
        for (size_t i = 0; i < sendBins.size(); ++i)
            msg.addFloat32(sendBins[i]);
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(sendMutex);
            ok = oscSender.send(msg);
        }
        // when diagnostics enabled, optionally dump the full float payload for a few sends
        if (senderDiag && payloadDumpRemaining > 0)
        {
            juce::String payload = "FULL_PAYLOAD: ";
            for (size_t i = 0; i < sendBins.size(); ++i)
            {
                payload += juce::String(sendBins[i]);
                if (i + 1 < sendBins.size()) payload += ", ";
            }
            juce::Logger::writeToLog(payload);
            --payloadDumpRemaining;
        }
        // diagnostic logging: timestamp each send when enabled
        if (senderDiag)
        {
            auto now = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            int cnt = ++senderDiagCount;
            juce::Logger::writeToLog("SEND ts_ms=" + juce::String((long long)ms) + " cnt=" + juce::String(cnt));
        }
        if (!ok)
            juce::Logger::writeToLog("OSC send failed (oscSender)");
        else if (sendLogLimit > 0)
        {
            juce::String s = "Sent /fft/amplitudes count=" + juce::String(outBins) + " first=" + juce::String(sendBins[0]) + " (verbose)";
            juce::Logger::writeToLog(s);
            --sendLogLimit;
        }
        // Periodic runtime confirmation for test tone
        if (testToneEnabled.load())
        {
            testToneLogCounter += senderIntervalMs;
            if (testToneLogCounter >= testToneLogIntervalMs)
            {
                juce::Logger::writeToLog("Test tone active freq=" + juce::String(testFreqHz));
                testToneLogCounter = 0;
            }
        }
        // sleep after sending so the first valid FFT is sent immediately
        std::this_thread::sleep_for(std::chrono::milliseconds(senderIntervalMs));
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
    formatManager.registerBasicFormats();
}

void FFTOSC::setTestTone(bool enabled, float freqHz)
{
    testToneEnabled.store(enabled);
    if (freqHz > 0.0f)
        testFreqHz = freqHz;
    juce::Logger::writeToLog("Test tone " + juce::String(enabled ? "enabled" : "disabled") + 
                             " freq=" + juce::String(testFreqHz));
}

void FFTOSC::setPlaybackFiles(const std::vector<juce::String>& paths)
{
    playbackFiles.clear();
    for (auto& p : paths)
    {
        juce::File f(p);
        if (f.existsAsFile())
            playbackFiles.push_back(f);
        else
            juce::Logger::writeToLog("Playback file not found: " + p);
    }
    // reset urn so nextFileRequested will trigger fresh selection
    urnIndices.clear();
    sequentialIndex = 0;
    nextFileRequested.store(!playbackFiles.empty());
}

void FFTOSC::setFilePlaybackEnabled(bool on)
{
    filePlaybackEnabled = on;
    if (!on)
    {
        transportSource.stop();
        readerSource.reset();
    }
    else
    {
        nextFileRequested.store(!playbackFiles.empty());
    }
    juce::Logger::writeToLog(juce::String("File playback ") + (on ? "enabled" : "disabled"));
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

void FFTOSC::setSenderIntervalMs(int ms)
{
    if (ms > 0)
    {
        senderIntervalMs = ms;
        juce::Logger::writeToLog("Sender interval set to " + juce::String(senderIntervalMs) + " ms");
    }
}

void FFTOSC::setSenderDiagnostic(bool on)
{
    senderDiag = on;
    senderDiagCount.store(0);
    juce::Logger::writeToLog(juce::String("Sender diagnostic ") + (on ? "enabled" : "disabled"));
}

void FFTOSC::setUseRMSAggregation(bool on)
{
    useRMSAggregation = on;
    juce::Logger::writeToLog(juce::String("RMS aggregation ") + (on ? "enabled" : "disabled"));
}

void FFTOSC::setUseNoiseFloor(bool on)
{
    useNoiseFloor = on;
    juce::Logger::writeToLog(juce::String("Noise floor ") + (on ? "enabled" : "disabled"));
    if (!useNoiseFloor)
        noiseFloor.clear();
}

void FFTOSC::setNoiseFloorInit(float initVal)
{
    noiseFloorInit = initVal;
    juce::Logger::writeToLog("Noise floor init set to " + juce::String(noiseFloorInit));
    // if floor vector exists, reinit
    if (!noiseFloor.empty())
        std::fill(noiseFloor.begin(), noiseFloor.end(), noiseFloorInit);
}

void FFTOSC::setNoiseFloorFixed(bool on)
{
    noiseFloorFixed = on;
    juce::Logger::writeToLog(juce::String("Noise floor fixed mode ") + (on ? "enabled" : "disabled"));
    if (on)
        noiseFloor.clear();
}

// bin-range feature removed

bool FFTOSC::getUseRMSAggregation() const
{
    return useRMSAggregation;
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
        // If a test tone is enabled, request both input and outputs so mic+tone work together
        int numInputs = 1;
        int numOutputs = 0;
        if (testToneEnabled.load())
            numOutputs = 2;
        // If file playback is enabled we need output channels so the audio callback
        // runs and `transportSource.getNextAudioBlock()` is called to advance files.
        if (filePlaybackEnabled)
            numOutputs = std::max(numOutputs, 2);
        deviceManager.initialiseWithDefaultDevices(numInputs, numOutputs);
        deviceManager.addAudioCallback(this);
    }
        else
        {
            juce::Logger::writeToLog("Simple sender enabled: skipping audio device init");
        }

    // start background sender thread
    senderRunning.store(true);
    senderThread = std::thread(&FFTOSC::senderLoop, this);
    // start the message-thread timer for periodic tasks (urn/file playback)
    startTimer(50); // 50 ms interval
    // print band centers so user can see which visual bins map to which frequencies
    logBandCenters();
    // If file playback was enabled before start, try to kick off the first file immediately
    if (filePlaybackEnabled && !playbackFiles.empty())
    {
        // try each file until one successfully opens
        bool openedAny = false;
        for (int idx = 0; idx < (int)playbackFiles.size(); ++idx)
        {
            juce::File f = playbackFiles[(size_t)idx];
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
            if (reader)
            {
                currentFileIndex = idx;
                readerSource.reset(new juce::AudioFormatReaderSource(reader.release(), true));
                transportSource.setSource(readerSource.get(), 0, nullptr, readerSource->getAudioFormatReader()->sampleRate);
                transportSource.start();
                juce::Logger::writeToLog("Started playback: " + f.getFullPathName());
                openedAny = true;
                break;
            }
            else
            {
                juce::Logger::writeToLog("Failed to open playback file: " + f.getFullPathName());
            }
        }
        if (openedAny)
        {
            // clear any pending automatic request so timerCallback doesn't immediately open
            nextFileRequested.store(false);
        }
    }
    // start playback thread to monitor transport and advance files
    playbackThreadRunning.store(true);
    playbackThread = std::thread([this]() {
        while (playbackThreadRunning.load())
        {
            if (filePlaybackEnabled && readerSource != nullptr)
            {
                double pos = transportSource.getCurrentPosition();
                double len = transportSource.getLengthInSeconds();
                bool playing = transportSource.isPlaying();
                if (len > 0.0 && !playing && pos >= (len - 0.05) && pos > 0.01)
                {
                    juce::Logger::writeToLog("playbackThread: detected EOF pos=" + juce::String(pos) + " len=" + juce::String(len));
                    // start next file under playbackMutex to avoid races
                    std::lock_guard<std::mutex> lg(playbackMutex);
                    if (!playbackFiles.empty())
                    {
                        int idx = 0;
                        if (shufflePlayback)
                        {
                            if (urnIndices.empty())
                            {
                                urnIndices.reserve((int)playbackFiles.size());
                                for (int i = 0; i < (int)playbackFiles.size(); ++i)
                                    urnIndices.push_back(i);
                                std::random_device rd;
                                std::mt19937 g(rd());
                                std::shuffle(urnIndices.begin(), urnIndices.end(), g);
                            }
                            if (currentFileIndex >= 0 && urnIndices.size() > 1)
                                urnIndices.erase(std::remove(urnIndices.begin(), urnIndices.end(), currentFileIndex), urnIndices.end());
                            idx = urnIndices.back();
                            urnIndices.pop_back();
                        }
                        else
                        {
                            if (sequentialIndex < 0) sequentialIndex = 0;
                            if (sequentialIndex >= (int)playbackFiles.size()) sequentialIndex = 0;
                            idx = sequentialIndex;
                            sequentialIndex = (sequentialIndex + 1) % (int)playbackFiles.size();
                        }
                        juce::File f = playbackFiles[(size_t)idx];
                        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
                        if (reader)
                        {
                            transportSource.stop();
                            transportSource.setSource(nullptr);
                            readerSource.reset(new juce::AudioFormatReaderSource(reader.release(), true));
                            currentFileLengthSeconds = (double)readerSource->getTotalLength() / readerSource->getAudioFormatReader()->sampleRate;
                            transportSource.setSource(readerSource.get(), 0, nullptr, readerSource->getAudioFormatReader()->sampleRate);
                            transportSource.start();
                            currentFileIndex = idx;
                            juce::Logger::writeToLog("playbackThread: Started playback: " + f.getFullPathName());
                        }
                        else
                        {
                            juce::Logger::writeToLog("playbackThread: Failed to open playback file: " + f.getFullPathName());
                        }
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void FFTOSC::stop()
{
    // stop sender thread
    senderRunning.store(false);
    if (senderThread.joinable())
        senderThread.join();

    // stop the timer
    stopTimer();

    // stop playback thread
    playbackThreadRunning.store(false);
    if (playbackThread.joinable())
        playbackThread.join();

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

    // prepare transportSource if file playback is enabled
    transportSource.prepareToPlay(512, currentSampleRate);
}

void FFTOSC::audioDeviceStopped()
{
    juce::Logger::writeToLog("Audio device stopped");
    transportSource.stop();
    transportSource.releaseResources();
}

void FFTOSC::audioDeviceIOCallbackWithContext (const float* const* inputChannelData, int numInputChannels, float* const* outputChannelData, int numOutputChannels, int numSamples, const juce::AudioIODeviceCallbackContext& context)
{
    // Mix input channels to mono and push into FIFO
    juce::AudioBuffer<float> tmpBuf;
    juce::AudioSourceChannelInfo tmpInfo;
    bool havePlaybackBuf = false;
    if (filePlaybackEnabled)
    {
        tmpBuf.setSize(2, numSamples);
        tmpInfo = juce::AudioSourceChannelInfo(&tmpBuf, 0, numSamples);
        transportSource.getNextAudioBlock(tmpInfo);
        havePlaybackBuf = true;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = 0.0f;

        if (filePlaybackEnabled && havePlaybackBuf)
        {
            // mix channels for this sample index
            float s = 0.0f;
            int chans = tmpBuf.getNumChannels();
            for (int ch = 0; ch < chans; ++ch)
                s += tmpBuf.getSample(ch, i);
            if (chans > 0) s /= (float)chans;
            sample = s;
        }
        else if (testToneEnabled.load())
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

        // write computed sample to outputs if present (test tone or input-derived)
        if (outputChannelData != nullptr)
        {
            for (int outCh = 0; outCh < numOutputChannels; ++outCh)
            {
                float* out = outputChannelData[outCh];
                if (out != nullptr)
                {
                    out[i] = sample;
                }
            }
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

    // occasional diagnostic for playback state (not every callback)
    static int playbackDiagCounter = 0;
    if ((++playbackDiagCounter % 500) == 0 && filePlaybackEnabled)
    {
        juce::Logger::writeToLog("audio callback diag: numOutputChannels=" + juce::String(numOutputChannels)
                                  + " transportPlaying=" + juce::String(transportSource.isPlaying() ? "yes" : "no")
                                  + " pos=" + juce::String(transportSource.getCurrentPosition())
                                  + " len=" + juce::String(transportSource.getLengthInSeconds()) );
    }
    // If the transport has stopped but position indicates end-of-file, request next file.
    if (filePlaybackEnabled)
    {
        double pos = transportSource.getCurrentPosition();
        double len = transportSource.getLengthInSeconds();
        if (len > 0.0 && !transportSource.isPlaying() && pos >= (len - 0.05) && pos > 0.01)
        {
            juce::Logger::writeToLog("audio callback: detected EOF pos=" + juce::String(pos) + " len=" + juce::String(len) + ", requesting next file");
            nextFileRequested.store(true);
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
    // handle any requested next-file events from audio thread
    if (nextFileRequested.exchange(false))
    {
        juce::Logger::writeToLog("timerCallback: nextFileRequested observed");
        if (filePlaybackEnabled && !playbackFiles.empty())
        {
            // choose next file from urn and start it
            int idx = 0;
            if (shufflePlayback)
            {
                if (urnIndices.empty())
                {
                    urnIndices.reserve((int)playbackFiles.size());
                    for (int i = 0; i < (int)playbackFiles.size(); ++i)
                        urnIndices.push_back(i);
                    std::random_device rd;
                    std::mt19937 g(rd());
                    std::shuffle(urnIndices.begin(), urnIndices.end(), g);
                }
                // avoid reselecting currently playing file if possible
                if (currentFileIndex >= 0 && urnIndices.size() > 1)
                    urnIndices.erase(std::remove(urnIndices.begin(), urnIndices.end(), currentFileIndex), urnIndices.end());
                idx = urnIndices.back();
                urnIndices.pop_back();
            }
            else
            {
                if (sequentialIndex < 0) sequentialIndex = 0;
                if (sequentialIndex >= (int)playbackFiles.size()) sequentialIndex = 0;
                idx = sequentialIndex;
                sequentialIndex = (sequentialIndex + 1) % (int)playbackFiles.size();
            }
            currentFileIndex = idx;
            juce::File f = playbackFiles[(size_t)idx];
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
            if (reader)
            {
                // stop and clear previous source before swapping
                transportSource.stop();
                transportSource.setSource(nullptr);
                readerSource.reset(new juce::AudioFormatReaderSource(reader.release(), true));
                currentFileLengthSeconds = (double)readerSource->getTotalLength() / readerSource->getAudioFormatReader()->sampleRate;
                transportSource.setSource(readerSource.get(), 0, nullptr, readerSource->getAudioFormatReader()->sampleRate);
                transportSource.start();
                juce::Logger::writeToLog("Started playback: " + f.getFullPathName() + " seqIndex=" + juce::String(sequentialIndex));
            }
            else
            {
                juce::Logger::writeToLog("Failed to open playback file: " + f.getFullPathName());
            }
        }
    }
    // If file playback is enabled and the transport stopped (file ended), schedule next
    if (filePlaybackEnabled && readerSource != nullptr)
    {
        // consider playback finished either when transport reports stopped or we've reached the end
        bool finished = !transportSource.isPlaying();
        double pos = transportSource.getCurrentPosition();
        double len = transportSource.getLengthInSeconds();
        if (len > 0.0 && pos >= (len - 0.05))
            finished = true;
        if (finished)
        {
            juce::Logger::writeToLog("Playback finished for index=" + juce::String(currentFileIndex) + ", scheduling next file");
            nextFileRequested.store(true);
        }
    }
    // Sending is now handled by the sender thread; timerCallback will not transmit OSC.
}
