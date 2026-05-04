#include <JuceHeader.h>
#include "FFTOSC.h"
#include <thread>
#include <chrono>
#include <unistd.h>

static void printUsage(const char* argv0)
{
    std::cout << "Usage: " << argv0 << " [options]\n\n"
        << "Network:\n"
        << "  --host=HOST                  OSC destination host (default: 127.0.0.1)\n"
        << "  --port=PORT                  OSC destination port (default: 57120)\n\n"
        << "File playback:\n"
        << "  --play-dir=PATH              Play all audio files in directory\n"
        << "  --play-files=FILE,FILE,...   Play specific comma-separated files\n"
        << "  --shuffle-play               Shuffle playback order\n\n"
        << "Mic-ducking:\n"
        << "  --suspend-threshold-db=X     Duck when mic exceeds X dBFS for 500ms\n\n"
        << "Auto-playback:\n"
        << "  --auto-playback              Start playback when mic is silent\n"
        << "  --auto-play-threshold-db=X   Silence threshold in dBFS (default: -40)\n"
        << "  --auto-play-hold-ms=MS       Duration mic must be silent before playback starts\n\n"
        << "FFT / frequency:\n"
        << "  --map-min=HZ                 Lowest frequency band (default: 80)\n"
        << "  --map-max=HZ                 Highest frequency band (default: 24000)\n"
        << "  --hp-cutoff=HZ               High-pass filter cutoff\n"
        << "  --rms-agg                    Use RMS aggregation for bin downsampling\n"
        << "  --no-rms                     Disable RMS aggregation\n"
        << "  --display-noise-floor-db=X   Noise floor for display normalisation (default: -60)\n\n"
        << "Voice filtering:\n"
        << "  --voice-only                 Zero non-voice FFT bins before sending\n"
        << "  --voice-min=HZ               Voice lower bound (default: 80)\n"
        << "  --voice-max=HZ               Voice upper bound (default: 3000)\n\n"
        << "Test / diagnostics:\n"
        << "  --test-tone=HZ               Enable built-in sine test tone\n"
        << "  --sender-interval=MS         OSC send interval in ms (default: 33)\n"
        << "  --diag-sender                Enable sender diagnostics\n"
        << "  --autoplay-toggle=MS:CYCLES  Toggle auto-playback for testing\n"
        << "  -h, --help                   Show this help\n";
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    juce::String host = "127.0.0.1";
    int port = 57120;

    // First pass: host/port only (needed at FFTOSC construction)
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);
        if (arg.startsWith("--host="))
            host = arg.fromFirstOccurrenceOf("=", false, false);
        else if (arg.startsWith("--port="))
            port = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
        else if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
    }

    FFTOSC app(host, port);

    // State mirrors for flag log
    bool testToneEnabled  = false;
    double testToneFreq   = 0.0;
    bool voiceOnlyEnabled = false;
    bool rmsEnabled       = app.getUseRMSAggregation();
    bool senderDiag       = false;
    bool shufflePlay      = false;
    bool autoPlay         = false;
    int  senderInterval   = app.getSenderIntervalMs();
    double voiceMin       = app.getVoiceMinFreq();
    double voiceMax       = app.getVoiceMaxFreq();
    bool voiceMinSet = false, voiceMaxSet = false;
    double mapMin = 0.0, mapMax = 0.0;
    bool mapMinSet = false, mapMaxSet = false;
    double autoPlayThresholdDb = std::numeric_limits<double>::quiet_NaN();
    double suspendThresholdDb  = std::numeric_limits<double>::quiet_NaN();
    double displayNoiseFloorDb = std::numeric_limits<double>::quiet_NaN();
    double hpCutoff            = std::numeric_limits<double>::quiet_NaN();
    bool hpCutoffSet = false;
    int autoPlayHoldMs       = -1;
    int autoplayTogglePlayMs = 0;
    int autoplayToggleCycles = -1;
    bool playFiles = false;
    juce::String playFilesList;
    bool playDir = false;
    juce::String playDirPath;

    // Second pass: all other flags
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg(argv[i]);

        if (arg.startsWith("--host=") || arg.startsWith("--port=") ||
            arg == "-h" || arg == "--help")
            continue;

        // File playback
        else if (arg.startsWith("--play-dir="))
        {
            playDir = true;
            playDirPath = arg.fromFirstOccurrenceOf("=", false, false);
        }
        else if (arg.startsWith("--play-files="))
        {
            playFiles = true;
            playFilesList = arg.fromFirstOccurrenceOf("=", false, false);
        }
        else if (arg == "--shuffle-play")
        {
            shufflePlay = true;
            app.setShufflePlayback(true);
        }

        // Mic-ducking
        else if (arg.startsWith("--suspend-threshold-db="))
        {
            double v = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            suspendThresholdDb = v;
            app.setMicDuckThresholdDb(v);
            app.setMicDuckEnabled(true);
        }

        // Auto-playback
        else if (arg == "--auto-playback")
        {
            autoPlay = true;
            app.setAutoPlayback(true);
        }
        else if (arg.startsWith("--auto-play-threshold-db="))
        {
            double v = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            autoPlayThresholdDb = v;
            app.setAutoPlayThresholdDb(v);
        }
        else if (arg.startsWith("--auto-play-hold-ms="))
        {
            int v = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
            autoPlayHoldMs = v;
            app.setAutoPlayHoldMs(v);
        }

        // FFT / frequency
        else if (arg.startsWith("--map-min="))
        {
            mapMin = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMinSet = true;
        }
        else if (arg.startsWith("--map-max="))
        {
            mapMax = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMaxSet = true;
        }
        else if (arg.startsWith("--hp-cutoff="))
        {
            hpCutoff = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            hpCutoffSet = true;
        }
        else if (arg == "--rms-agg")
        {
            rmsEnabled = true;
            app.setUseRMSAggregation(true);
        }
        else if (arg == "--no-rms")
        {
            rmsEnabled = false;
            app.setUseRMSAggregation(false);
        }
        else if (arg.startsWith("--display-noise-floor-db="))
        {
            double v = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            displayNoiseFloorDb = v;
            app.setDisplayNoiseFloorDb(v);
        }

        // Voice filtering
        else if (arg == "--voice-only")
        {
            voiceOnlyEnabled = true;
            app.setSendVoiceOnly(true);
        }
        else if (arg.startsWith("--voice-min="))
        {
            voiceMin = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            voiceMinSet = true;
        }
        else if (arg.startsWith("--voice-max="))
        {
            voiceMax = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            voiceMaxSet = true;
        }

        // Test / diagnostics
        else if (arg.startsWith("--test-tone="))
        {
            double f = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            if (f > 0.0)
            {
                testToneEnabled = true;
                testToneFreq = f;
                app.setTestTone(true, (float)f);
            }
        }
        else if (arg.startsWith("--sender-interval="))
        {
            int v = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
            if (v > 0)
            {
                senderInterval = v;
                app.setSenderIntervalMs(v);
            }
        }
        else if (arg == "--diag-sender")
        {
            senderDiag = true;
            app.setSenderDiagnostic(true);
        }
        else if (arg.startsWith("--autoplay-toggle="))
        {
            juce::String v = arg.fromFirstOccurrenceOf("=", false, false);
            int colon = v.indexOfChar(':');
            if (colon >= 0)
            {
                autoplayTogglePlayMs = v.substring(0, colon).getIntValue();
                autoplayToggleCycles = v.substring(colon + 1).getIntValue();
            }
            else
            {
                autoplayTogglePlayMs = v.getIntValue();
                autoplayToggleCycles = 1;
            }
        }
        else
        {
            juce::Logger::writeToLog("Unknown flag ignored: " + arg);
        }
    }

    // Apply map frequency range
    if (mapMinSet || mapMaxSet)
    {
        double useMin = mapMinSet ? mapMin : 80.0;
        double useMax = mapMaxSet ? mapMax : 24000.0;
        if (useMax >= useMin)
        {
            if (useMax == useMin)
            {
                double eps = std::max(1.0, useMin * 0.001);
                useMin = std::max(1.0, useMin - eps);
                useMax = useMin + eps * 2.0;
            }
            app.setMapFreqRange(useMin, useMax);
            if (mapMinSet && mapMaxSet)
            {
                voiceMin = useMin; voiceMax = useMax;
                voiceMinSet = voiceMaxSet = true;
                app.setVoiceRange(useMin, useMax);
                app.setSendVoiceOnly(true);
            }
        }
        else
        {
            juce::Logger::writeToLog("Ignored map range: max must be >= min");
        }
    }

    // Apply voice range
    if (voiceMinSet || voiceMaxSet)
    {
        double useMin = voiceMinSet ? voiceMin : app.getVoiceMinFreq();
        double useMax = voiceMaxSet ? voiceMax : app.getVoiceMaxFreq();
        if (useMax > useMin)
            app.setVoiceRange(useMin, useMax);
        else
            juce::Logger::writeToLog("Ignored voice range: max must be > min");
    }

    // Load playback files
    if (playFiles)
    {
        std::vector<juce::String> parts;
        int start = 0;
        while (true)
        {
            int comma = playFilesList.indexOfChar(',', start);
            if (comma < 0) { parts.push_back(playFilesList.substring(start).trim()); break; }
            parts.push_back(playFilesList.substring(start, comma).trim());
            start = comma + 1;
        }
        app.setPlaybackFiles(parts);
        app.setFilePlaybackEnabled(true);
    }
    else if (playDir)
    {
        juce::File dir(playDirPath);
        if (!dir.exists() || !dir.isDirectory())
        {
            juce::Logger::writeToLog("Playback directory not found: " + playDirPath);
        }
        else
        {
            juce::Array<juce::File> files;
            dir.findChildFiles(files, juce::File::findFiles, false);
            std::vector<juce::String> parts;
            for (auto& f : files)
            {
                if (f.getFileName().startsWithChar('.')) continue;
                juce::String ext = f.getFileExtension().toLowerCase();
                if (ext == ".wav" || ext == ".mp3" || ext == ".aiff" || ext == ".aif" ||
                    ext == ".flac" || ext == ".ogg" || ext == ".m4a")
                    parts.push_back(f.getFullPathName());
            }
            if (!parts.empty())
            {
                app.setPlaybackFiles(parts);
                app.setFilePlaybackEnabled(true);
            }
            else
            {
                juce::Logger::writeToLog("No audio files found in: " + playDirPath);
            }
        }
    }

    // Apply HP cutoff
    if (hpCutoffSet)
        app.setHighPassCutoffHz(hpCutoff);

    // Log active flags
    juce::String flagLog = "Flags: host=" + host + " port=" + juce::String(port)
        + " testTone=" + (testToneEnabled ? juce::String(testToneFreq) : "off")
        + " voiceOnly=" + (voiceOnlyEnabled ? "on" : "off")
        + " rmsAgg=" + (rmsEnabled ? "on" : "off")
        + " senderDiag=" + (senderDiag ? "on" : "off")
        + " senderInterval=" + juce::String(senderInterval)
        + " voiceMin=" + juce::String(voiceMin) + " voiceMax=" + juce::String(voiceMax)
        + " mapMin=" + (mapMinSet ? juce::String(mapMin) : "default")
        + " mapMax=" + (mapMaxSet ? juce::String(mapMax) : "default")
        + " displayNoiseFloorDb=" + juce::String(app.getDisplayNoiseFloorDb());
    if (autoPlay)    flagLog += " autoPlay=on";
    if (shufflePlay) flagLog += " shufflePlay=on";
    if (hpCutoffSet) flagLog += " hpCutoff=" + juce::String(hpCutoff);
    if (suspendThresholdDb == suspendThresholdDb)
        flagLog += " suspendThresholdDb=" + juce::String(suspendThresholdDb);
    if (autoPlayThresholdDb == autoPlayThresholdDb)
        flagLog += " autoPlayThresholdDb=" + juce::String(autoPlayThresholdDb);
    if (autoPlayHoldMs >= 0)
        flagLog += " autoPlayHoldMs=" + juce::String(autoPlayHoldMs);
    juce::Logger::writeToLog(flagLog);

    app.start();

    if (autoplayTogglePlayMs > 0)
    {
        int cycles = autoplayToggleCycles == 0 ? -1 : autoplayToggleCycles;
        app.startAutoplayToggleTest(autoplayTogglePlayMs, cycles);
    }

    if (isatty(fileno(stdin)))
    {
        std::string s;
        std::getline(std::cin, s);
    }
    else
    {
        while (true)
            std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    app.stopAutoplayToggleTest();
    app.stop();
    juce::Logger::writeToLog("Shutting down");
    return 0;
}
