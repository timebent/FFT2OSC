#include "FFTOSC.h"
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

    // raw debug UDP socket removed; rely on juce::OSCSender for emission

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
        // copy latest amplitudes
        std::vector<float> sendBins;
        sendBins.reserve(outBins);
        float globalMax = 0.0f;
        int globalMaxIndex = 0;
        int nonZeroCount = 0;
        int expectedBin = -1;

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
                // Use fractional-bin interpolation to sample amplitudes at
                // log-spaced target frequencies (always enabled).

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

                // Always use interpolation: sample fractional FFT bins at
                // log-spaced target frequencies and push interpolated values.
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
        const float minDb = displayMinDb;
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

        // (noise-floor subtraction removed)

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

    // raw debug UDP socket removed; rely on juce::OSCSender for emission
    formatManager.registerBasicFormats();
}

void FFTOSC::setTestTone(bool enabled, float freqHz)
{
    testToneEnabled.store(enabled);
    if (freqHz > 0.0f)
        testFreqHz = freqHz;
    if (verboseLogging)
        juce::Logger::writeToLog("Test tone " + juce::String(enabled ? "enabled" : "disabled") + 
                                 " freq=" + juce::String(testFreqHz));
}

void FFTOSC::setVerboseLogging(bool on)
{
    verboseLogging = on;
    // Log the state change (useful even when enabling only once)
    if (verboseLogging)
        juce::Logger::writeToLog("Verbose logging enabled");
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
    // reset urn; do not auto-request playback on file list changes
    urnIndices.clear();
    sequentialIndex = 0;
    nextFileRequested.store(false);
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
        // If we enabled playback at runtime but the audio device was
        // previously opened without output channels, make sure outputs
        // are initialized so the transport actually produces sound.
        auto* dev = deviceManager.getCurrentAudioDevice();
        bool needReinit = false;
        if (dev == nullptr)
            needReinit = true;
        else if (dev->getActiveOutputChannels().countNumberOfSetBits() == 0)
            needReinit = true;
        if (needReinit)
        {
            int numInputs = 1;
            int numOutputs = 2; // stereo output for file playback
            deviceManager.initialiseWithDefaultDevices(numInputs, numOutputs);
            deviceManager.addAudioCallback(this);
            juce::Logger::writeToLog("File playback: initialized audio outputs (stereo)");
        }
        // Do not auto-request playback when enabling file playback; require explicit start
    }
    juce::Logger::writeToLog(juce::String("File playback ") + (on ? "enabled" : "disabled"));

    // If there are no OS outputs available (e.g. inputs-only device), start
    // a feeder thread that reads file samples directly and feeds them into
    // the FFT path for analysis. This allows testing without an active
    // audio output device.
    auto* dev = deviceManager.getCurrentAudioDevice();
    bool outputsAvailable = (dev != nullptr) && (dev->getActiveOutputChannels().countNumberOfSetBits() > 0);
    if (forceFileFeeder)
    {
        outputsAvailable = false;
        juce::Logger::writeToLog("Force file feeder requested: treating outputs as unavailable");
    }

    if (on && !outputsAvailable)
    {
        // start feeder
        if (!fileFeederRunning.load())
        {
            fileFeederRunning.store(true);
            fileFeederThread = std::thread([this]() {
                // simple sequential feeder: loop through playbackFiles and read samples
                while (fileFeederRunning.load())
                {
                    if (playbackFiles.empty())
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    for (size_t fi = 0; fi < playbackFiles.size() && fileFeederRunning.load(); ++fi)
                    {
                        juce::File f = playbackFiles[fi];
                        std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(f));
                        if (!reader) { juce::Logger::writeToLog("fileFeeder: failed to open " + f.getFullPathName()); continue; }
                        const int block = 512;
                        juce::AudioBuffer<float> buf((int)reader->numChannels, block);
                        juce::int64 pos = 0;
                        juce::int64 total = reader->lengthInSamples;
                        double sr = reader->sampleRate;
                        while (pos < total && fileFeederRunning.load())
                        {
                            int toRead = (int)std::min<int64_t>(block, total - pos);
                            buf.clear();
                            reader->read(&buf, 0, toRead, pos, true, true);
                            pos += toRead;
                            // mix channels to mono and push to FFT
                            std::vector<float> mono((size_t)toRead);
                            for (int i = 0; i < toRead; ++i)
                            {
                                float s = 0.0f;
                                for (int ch = 0; ch < buf.getNumChannels(); ++ch)
                                    s += buf.getSample(ch, i);
                                mono[i] = s / (float)buf.getNumChannels();
                            }
                            pushSamplesToFFT(mono.data(), toRead);
                            // pace reads roughly according to sample rate
                            int ms = (int)std::round((double)toRead / sr * 1000.0);
                            if (ms > 0)
                                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
                        }
                        juce::Logger::writeToLog("fileFeeder: finished " + f.getFileName());
                    }
                }
                juce::Logger::writeToLog("fileFeeder: stopped");
            });
            juce::Logger::writeToLog("fileFeeder: started (no OS outputs available)");
        }
    }
    else
    {
        // stop feeder if running
        if (fileFeederRunning.load())
        {
            fileFeederRunning.store(false);
            if (fileFeederThread.joinable()) fileFeederThread.join();
            juce::Logger::writeToLog("fileFeeder: stopped by setFilePlaybackEnabled");
        }
    }

    // If we've just enabled file playback and we have files available but
    // no reader is currently set, request the playback thread to start the
    // next file. This does not re-introduce automatic resume logic on
    // mic inactivity — it simply starts the initial file when the user
    // explicitly enables playback (e.g. via --play-dir or --play-files).
    if (on && !playbackFiles.empty() && readerSource == nullptr)
    {
        nextFileRequested.store(true);
        juce::Logger::writeToLog("File playback enabled: requesting start of first file");
    }
}

// fractional-bin interpolation is always enabled; setInterpMode removed

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
// noise-floor API removed

// setForceSilence removed

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
}

void FFTOSC::start()
{
    // Initialize audio device
    // If a test tone is enabled, request both input and outputs so mic+tone work together
    int numInputs = 1;
    // Request stereo outputs by default so microphone passthrough is audible
    int numOutputs = 2;
    if (testToneEnabled.load())
        numOutputs = 2;
    // If file playback is enabled we need output channels so the audio callback
    // runs and `transportSource.getNextAudioBlock()` is called to advance files.
    if (filePlaybackEnabled)
        numOutputs = std::max(numOutputs, 2);
    deviceManager.initialiseWithDefaultDevices(numInputs, numOutputs);
    deviceManager.addAudioCallback(this);
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (dev != nullptr)
        {
            juce::Logger::writeToLog("Audio device init: " + dev->getName()
                                     + " rate=" + juce::String(dev->getCurrentSampleRate())
                                     + " inputs=" + juce::String(dev->getActiveInputChannels().countNumberOfSetBits())
                                     + " outputs=" + juce::String(dev->getActiveOutputChannels().countNumberOfSetBits()));
        }
        else
        {
            juce::Logger::writeToLog("Audio device init: no device returned by deviceManager");
        }
    }

    // start background sender thread
    senderRunning.store(true);
    senderThread = std::thread(&FFTOSC::senderLoop, this);
    // start the message-thread timer for periodic tasks (urn/file playback)
    startTimer(50); // 50 ms interval
    // print band centers so user can see which visual bins map to which frequencies
    logBandCenters();
    // Automatic start/resume at startup removed: playback will not be started
    // implicitly. Use explicit control to start playback when desired.
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
                                // 32‑bit Mersenne Twister pseudo‑random engine (a PRNG implementation).
                                std::random_device rd;
                                std::mt19937 g(rd());
                                
                                std::shuffle(urnIndices.begin(), urnIndices.end(), g);
                            }
                            if (currentFileIndex >= 0 && urnIndices.size() > 1)
                                urnIndices.erase(
                                    // move the item item at currentFileIndex to the end 
                                    // return an iterator that points to the new end of the range after removing currentFileIndex
                                    std::remove(urnIndices.begin(), urnIndices.end(), currentFileIndex), 
                                   // get the end of the range. This should always be 1 since we are eliminating single items.
                                    urnIndices.end());
                            // draw an item from the urn and remove it.
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
                            if (readerSource) readerSource->setNextReadPosition(0);
                            currentFileLengthSeconds = (double)readerSource->getTotalLength() / readerSource->getAudioFormatReader()->sampleRate;
                            transportSource.setSource(readerSource.get(), 0, nullptr, readerSource->getAudioFormatReader()->sampleRate);
                            transportSource.setPosition(0.0);
                            transportSource.start();
                            // keep transportSource internal gain at unity; audio callback
                            // applies `playbackGain` so we don't multiply gain twice.
                            transportSource.setGain(1.0f);
                            {
                                auto now = std::chrono::steady_clock::now();
                                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                                lastPlaybackStartMs.store((long long)ms);
                            }
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

            // Observe any external requests to start the next file (e.g. from audio thread)
            if (nextFileRequested.exchange(false))
            {
                juce::Logger::writeToLog("playbackThread: nextFileRequested observed");
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
                        transportSource.stop();
                        transportSource.setSource(nullptr);
                        readerSource.reset(new juce::AudioFormatReaderSource(reader.release(), true));
                        if (readerSource) readerSource->setNextReadPosition(0);
                        currentFileLengthSeconds = (double)readerSource->getTotalLength() / readerSource->getAudioFormatReader()->sampleRate;
                        transportSource.setSource(readerSource.get(), 0, nullptr, readerSource->getAudioFormatReader()->sampleRate);
                        transportSource.setPosition(0.0);
                        transportSource.start();
                        // keep transportSource internal gain at unity; audio callback
                        // applies `playbackGain` so we don't multiply gain twice.
                        transportSource.setGain(1.0f);
                        {
                            auto now = std::chrono::steady_clock::now();
                            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                            lastPlaybackStartMs.store((long long)ms);
                        }
                        juce::Logger::writeToLog("playbackThread: Started playback (requested): " + f.getFullPathName());
                    }
                    else
                    {
                        juce::Logger::writeToLog("playbackThread: Failed to open playback file: " + f.getFullPathName());
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void FFTOSC::setAutoPlayback(bool on)
{
    juce::Logger::writeToLog("Auto-playback request ignored: automatic resume/start removed");
}

void FFTOSC::setAutoPlayThresholdDb(double db)
{
    juce::Logger::writeToLog("Ignored setAutoPlayThresholdDb(" + juce::String(db) + "): auto-playback removed");
}

// mic-fade setters removed

void FFTOSC::setAutoPlayHoldMs(int ms)
{
    juce::Logger::writeToLog("Ignored setAutoPlayHoldMs(" + juce::String(ms) + "): auto-playback removed");
}

void FFTOSC::setForceFileFeeder(bool on)
{
    forceFileFeeder = on;
    juce::Logger::writeToLog(juce::String("Force file feeder ") + (on ? "enabled" : "disabled"));
    // re-evaluate playback enabling to start/stop feeder if files are enabled
    if (filePlaybackEnabled)
        setFilePlaybackEnabled(true);
}

void FFTOSC::startAutoplayToggleTest(int playMs, int cycles)
{
    // stop existing test if running
    stopAutoplayToggleTest();
    if (playMs <= 0) playMs = 1000;
    autoplayTestRunning.store(true);
    autoplayTestThread = std::thread([this, playMs, cycles]() {
        int iter = 0;
        while (autoplayTestRunning.load())
        {
            // enable playback
            setFilePlaybackEnabled(true);
            int slept = 0;
            while (autoplayTestRunning.load() && slept < playMs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                slept += 50;
            }
            if (!autoplayTestRunning.load()) break;
            // disable playback => return to mic input
            setFilePlaybackEnabled(false);
            // re-enable mic-driven fade if it was desired by user (leave disabled here)
            int slept2 = 0;
            while (autoplayTestRunning.load() && slept2 < playMs)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                slept2 += 50;
            }
            if (cycles > 0)
            {
                ++iter;
                if (iter >= cycles) break;
            }
        }
        // ensure playback is left disabled at end of test
        setFilePlaybackEnabled(false);
        autoplayTestRunning.store(false);
    });
    juce::Logger::writeToLog("Autoplay-toggle test started: " + juce::String(playMs) + "ms per state, cycles=" + juce::String(cycles));
}

void FFTOSC::stopAutoplayToggleTest()
{
    if (autoplayTestRunning.load())
    {
        autoplayTestRunning.store(false);
        if (autoplayTestThread.joinable())
            autoplayTestThread.join();
        juce::Logger::writeToLog("Autoplay-toggle test stopped");
    }
}

void FFTOSC::setDisplayNoiseFloorDb(double db)
{
    displayMinDb = (float)db;
    juce::Logger::writeToLog("Display noise-floor (min dB) set to " + juce::String(displayMinDb));
}

// runForcedFadeTest removed

// runForcedFadeResumeTest removed

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

    // stop feeder thread if running
    fileFeederRunning.store(false);
    if (fileFeederThread.joinable())
        fileFeederThread.join();

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
        s += " outputs=" + juce::String(device->getActiveOutputChannels().countNumberOfSetBits());
        juce::Logger::writeToLog(s);
        if (device->getActiveOutputChannels().countNumberOfSetBits() == 0 && filePlaybackEnabled)
            juce::Logger::writeToLog("Warning: no active output channels — playback will be silent");
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
    bool transportPlaying = transportSource.isPlaying();
    float maxAbsMic = 0.0f;
    float sumSquares = 0.0f;
    // diagnostics: accumulate output energy so we can verify audible output
    static double outSumSquares = 0.0;
    static int outSamplesCount = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        float micSample = 0.0f;
        // mix input channels to mono (if present)
        for (int ch = 0; ch < numInputChannels; ++ch)
        {
            const float* in = inputChannelData[ch];
            if (in != nullptr)
                micSample += in[i];
        }
        if (numInputChannels > 0)
            micSample /= (float) numInputChannels;

        // DEBUG: force direct microphone passthrough to outputs for testing
        // This writes the mixed mic sample immediately to all output channels
        // and sets a flag so the normal output-writing path is skipped.
        bool debugForcePassthrough = true;
        bool debugPassthroughDone = false;
        if (debugForcePassthrough && outputChannelData != nullptr)
        {
            float dbgOut = micSample;
            for (int outCh = 0; outCh < numOutputChannels; ++outCh)
            {
                float* out = outputChannelData[outCh];
                if (out != nullptr)
                    out[i] = dbgOut;
            }
            outSumSquares += (double)dbgOut * (double)dbgOut;
            ++outSamplesCount;
            debugPassthroughDone = true;
        }

        // accumulate for RMS computation (mic only)
        if (numInputChannels > 0)
            sumSquares += micSample * micSample;

        float playbackSample = 0.0f;
        if (filePlaybackEnabled && havePlaybackBuf)
        {
            // mix channels from playback buffer
            float s = 0.0f;
            int chans = tmpBuf.getNumChannels();
            for (int ch = 0; ch < chans; ++ch)
                s += tmpBuf.getSample(ch, i);
            if (chans > 0) s /= (float)chans;
            playbackSample = s;
        }

        float sample = 0.0f;

        // Select sample source. Mic-driven fade logic removed; prefer playback
        // when transport and playback buffer are available, otherwise use
        // test tone if enabled, else fall back to mic input.
        bool sampleFromPlaybackFlag = false;
        if (transportPlaying && filePlaybackEnabled && havePlaybackBuf)
        {
            sample = playbackSample;
            sampleFromPlaybackFlag = true;
        }
        else if (testToneEnabled.load())
        {
            double inc = 2.0 * M_PI * (double)testFreqHz / currentSampleRate;
            sample = (float)std::sin(phase);
            phase += inc;
            if (phase > (2.0 * M_PI)) phase -= (2.0 * M_PI);
        }
        else
        {
            sample = micSample;
        }

        // write computed sample to outputs if present (skip if debug passthrough already wrote)
        if (outputChannelData != nullptr && !debugPassthroughDone)
        {
            // apply playback gain to playback-derived samples so fades are
            // audible even if transportSource gain isn't taking effect.
            float outSample = sample;
            if (sampleFromPlaybackFlag)
                outSample *= playbackGain.load();

            for (int outCh = 0; outCh < numOutputChannels; ++outCh)
            {
                float* out = outputChannelData[outCh];
                if (out != nullptr)
                {
                    out[i] = outSample;
                }
            }
            // accumulate output energy for diagnostics
            outSumSquares += (double)outSample * (double)outSample;
            ++outSamplesCount;
        }

        // Feed all available sources into the FFT for analysis. Mic and
        // playback samples are both pushed when present (mic first).
        // Sometimes playback may be audibly turned down, but we still want
        // its spectral content to contribute to the FFT.
        if (numInputChannels > 0)
        {
            float s = micSample;
            pushSamplesToFFT(&s, 1);
        }
        if (filePlaybackEnabled && havePlaybackBuf)
        {
            // Feed the playback sample into the FFT after applying the
            // current playback gain so the visualizer matches audible output.
            float scaledPlayback = playbackSample * playbackGain.load();
            // Skip pushing silent playback to FFT to avoid showing inaudible content
            if (std::abs(scaledPlayback) > 1e-12f)
            {
                float s = scaledPlayback;
                pushSamplesToFFT(&s, 1);
            }
        }
        // If neither mic nor playback are available, push the mixed/selected
        // `sample` (test tone or silence) so FFT still runs.
        if (numInputChannels == 0 && !(filePlaybackEnabled && havePlaybackBuf))
        {
            float s = sample;
            pushSamplesToFFT(&s, 1);
        }
    }

    // publish the maximum input level observed during this callback for
    // the timer thread to consult when making mic-fade decisions.
    lastInputLevel.store(maxAbsMic);

    // publish RMS of mic samples for use by the timer-driven fade state machine
    if (numInputChannels > 0 && numSamples > 0)
    {
        float rms = std::sqrt(sumSquares / (float)numSamples);
        lastInputRms.store(rms);
    }

    // occasional diagnostic for playback state (not every callback)
    static int playbackDiagCounter = 0;
    if ((++playbackDiagCounter % 500) == 0 && filePlaybackEnabled)
    {
        double outRms = 0.0;
        if (outSamplesCount > 0)
            outRms = std::sqrt(outSumSquares / (double)outSamplesCount);
        juce::Logger::writeToLog("audio callback diag: numOutputChannels=" + juce::String(numOutputChannels)
                                  + " transportPlaying=" + juce::String(transportSource.isPlaying() ? "yes" : "no")
                                  + " pos=" + juce::String(transportSource.getCurrentPosition())
                                  + " len=" + juce::String(transportSource.getLengthInSeconds())
                                  + " outRms=" + juce::String(outRms)
                                  + " playbackGain=" + juce::String(playbackGain.load()));
        outSumSquares = 0.0;
        outSamplesCount = 0;
    }
    // periodic input diagnostics: every ~1s (timer runs at 50ms)
    static int inputDiagCounter = 0;
    if ((++inputDiagCounter % 20) == 0)
    {
        float micRmsVal = lastInputRms.load();
        float micPeakVal = lastInputLevel.load();
        juce::Logger::writeToLog("DIAG_INPUT: rms=" + juce::String(micRmsVal)
                                 + " peak=" + juce::String(micPeakVal)
                                 + " playbackGain=" + juce::String(playbackGain.load()));
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
        if (++sendCounter % 30 == 0)
            juce::Logger::writeToLog("No FFT amplitudes available yet");
        // continue — allow timer logic to run based on time-domain RMS
    }
    // Use time-domain RMS input level for fade decisions; auto-playback and
    // automatic resume/start logic have been removed. This block only
    // implements mic-driven fade-down of playback gain when sustained
    // input energy is present.
    {
        // Consider both RMS and peak levels for robust detection across
        // different mic setups. Use the larger of RMS and peak so short
        // vocal bursts and sustained speech both trigger the fade reliably.
        float micRms = lastInputRms.load();
        float micPeak = lastInputLevel.load();
        float micLevel = std::max(micRms, micPeak);

        if (verboseLogging)
        {
            juce::Logger::writeToLog("DEBUG_TIMER: micLevel=" + juce::String(micLevel)
                                     + " rms=" + juce::String(micRms)
                                     + " peak=" + juce::String(micPeak)
                                     + " playbackGain=" + juce::String(playbackGain.load()));
        }

        // compute peak of latest amplitudes (kept for diagnostics only)
        float peak = 0.0f;
        float scale = (fftSize > 0) ? (1.0f / (float) fftSize) : 1.0f;
        for (size_t i = 0; i < ampsCopy.size(); ++i)
        {
            float v = ampsCopy[i];
            if (!std::isfinite(v)) v = 0.0f;
            v *= scale;
            if (v > peak) peak = v;
        }

        // Mic-driven fade logic removed: playbackGain is unchanged here.
        (void) micLevel; // keep variable used
    }
    // (manual resume paths removed - no automatic/manual resume on mic inactivity)
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
                    // reserve spaces in the vector to avoid reallocations
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
                transportSource.setPosition(0.0);
                transportSource.start();
                {
                    auto now = std::chrono::steady_clock::now();
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                    lastPlaybackStartMs.store((long long)ms);
                }
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

void FFTOSC::pushSamplesToFFT(const float* samples, int numSamples)
{
    juce::ScopedLock gl(amplitudesLock); // protect fifoIndex and fftData operations
    for (int si = 0; si < numSamples; ++si)
    {
        fifo[fifoIndex++] = samples[si];
        if (fifoIndex >= fftSize)
        {
            // copy fifo into fftData (first half)
            for (int j = 0; j < fftSize; ++j)
                fftData[j] = fifo[j];

            // remove DC offset
            float mean = 0.0f;
            for (int j = 0; j < fftSize; ++j)
                mean += fftData[j];
            mean /= (float) fftSize;
            if (std::abs(mean) > 1e-12f)
            {
                for (int j = 0; j < fftSize; ++j)
                    fftData[j] -= mean;
            }

            window.multiplyWithWindowingTable(fftData.data(), (size_t) fftSize);
            fft->performFrequencyOnlyForwardTransform(fftData.data(), true);

            std::vector<float> amps(fftSize / 2);
            for (int b = 0; b < fftSize / 2; ++b)
                amps[b] = fftData[b];

            latestAmplitudes = std::move(amps);
            if (++fftProducedCounter % 10 == 0)
            {
                juce::String s = "FFT produced count=" + juce::String(latestAmplitudes.size()) + " first=" + juce::String(latestAmplitudes[0]);
                if (verboseLogging)
                    juce::Logger::writeToLog(s);
            }

            fifoIndex = 0;
        }
    }
}
