#include <JuceHeader.h>
#include "FFTOSC.h"
#include <thread>
#include <chrono>
#include <unistd.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    juce::String host = "127.0.0.1";
    int port = 57120;

    // Simple CLI parsing: only handle --host and --port here (other flags handled later)
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg (argv[i]);
        if (arg == "--host" && i + 1 < argc)
        {
            host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            port = std::atoi(argv[++i]);
        }
        else if (arg == "-h" || arg == "--help")
        {
            std::cout << "Usage: " << argv[0] << " [--host HOST] [--port PORT]\n";
            return 0;
        }
    }

    juce::Logger::writeToLog("Starting JUCE FFT OSC (press Enter to quit)");

    FFTOSC app (host, port);

    // local mirrors of flag state for logging
    bool testToneEnabled = false;
    double testToneFreq = 0.0;
    bool simpleSendEnabled = false;
    int simpleSendBins = 0;
    bool interpEnabled = false;
    bool voiceOnlyEnabled = false;
    double voiceMin = app.getVoiceMinFreq();
    double voiceMax = app.getVoiceMaxFreq();
    bool voiceMinSet = false, voiceMaxSet = false;
    int senderInterval = app.getSenderIntervalMs();
    bool rmsEnabled = app.getUseRMSAggregation();
    double mapMin = 0.0, mapMax = 0.0;
    bool mapMinSet = false, mapMaxSet = false;
    bool senderDiag = false;
    bool shufflePlay = false;
    bool noNoiseFloor = false;
    double noiseFloorInitVal = -1.0; // negative means keep default
    double baseNoiseFloorDb = std::numeric_limits<double>::quiet_NaN();
    bool fixedNoiseFloor = false;
    // bin-range flag removed
        bool playFiles = false;
        juce::String playFilesList;
        bool playDir = false;
        juce::String playDirPath;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg (argv[i]);
        if (arg == "--test-tone" && i + 1 < argc)
        {
            double f = std::atof(argv[++i]);
            if (f > 1.0)
            {
                testToneEnabled = true;
                testToneFreq = f;
                app.setTestTone(true, (float)f);
            }
            else if (f == 1.0)
            {
                testToneEnabled = true;
                testToneFreq = 440.0;
                app.setTestTone(true, (float)testToneFreq);
            }
        }
        else if (arg.startsWith("--testTone="))
        {
            double f = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            if (f <= 1.0 && f > 0.0)
                f = 440.0;
            if (f > 0.0)
            {
                testToneEnabled = true;
                testToneFreq = f;
                app.setTestTone(true, (float)f);
            }
        }
        else if (arg == "--testTone" && i + 1 < argc)
        {
            double f = std::atof(argv[++i]);
            if (f <= 1.0 && f > 0.0)
                f = 440.0;
            if (f > 0.0)
            {
                testToneEnabled = true;
                testToneFreq = f;
                app.setTestTone(true, (float)f);
            }
        }
        else if (arg == "--simple-send" && i + 1 < argc)
        {
            int b = std::atoi(argv[++i]);
            simpleSendEnabled = true;
            simpleSendBins = b;
            app.setSimpleSend(true, b);
        }
        else if (arg == "--sender-interval" && i + 1 < argc)
        {
            int v = std::atoi(argv[++i]);
            if (v > 0)
            {
                senderInterval = v;
                app.setSenderIntervalMs(v);
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
        else if (arg.startsWith("--senderIntervalMs="))
        {
            int v = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
            if (v > 0)
            {
                senderInterval = v;
                app.setSenderIntervalMs(v);
            }
        }
        else if (arg == "--senderInterval" && i + 1 < argc)
        {
            int v = std::atoi(argv[++i]);
            if (v > 0)
            {
                senderInterval = v;
                app.setSenderIntervalMs(v);
            }
        }
        else if (arg.startsWith("--senderInterval="))
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
        else if (arg == "--shuffle-play")
        {
            shufflePlay = true;
            app.setShufflePlayback(true);
        }
        else if (arg.startsWith("--diag-sender="))
        {
            int v = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
            bool on = (v != 0);
            senderDiag = on;
            app.setSenderDiagnostic(on);
        }
            else if (arg.startsWith("--play-files="))
            {
                playFiles = true;
                playFilesList = arg.fromFirstOccurrenceOf("=", false, false);
            }
            else if (arg == "--play-dir" && i + 1 < argc)
            {
                playDir = true;
                playDirPath = argv[++i];
            }
            else if (arg.startsWith("--play-dir="))
            {
                playDir = true;
                playDirPath = arg.fromFirstOccurrenceOf("=", false, false);
            }
        else if (arg == "--rms-agg")
        {
            rmsEnabled = true;
            app.setUseRMSAggregation(true);
        }
        else if (arg.startsWith("--rms-agg="))
        {
            int v = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
            bool on = (v != 0);
            rmsEnabled = on;
            app.setUseRMSAggregation(on);
        }
        else if (arg == "--no-rms")
        {
            rmsEnabled = false;
            app.setUseRMSAggregation(false);
        }
        else if (arg == "--interp")
        {
            interpEnabled = true;
            app.setInterpMode(true);
        }
        else if (arg == "--no-noise-floor")
        {
            noNoiseFloor = true;
            app.setUseNoiseFloor(false);
        }
        else if (arg.startsWith("--noise-floor-init="))
        {
            double v = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            noiseFloorInitVal = v;
            app.setNoiseFloorInit((float)v);
        }
        else if (arg.startsWith("--base-noise-floor="))
        {
            // linear amplitude value provided directly
            double v = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            noiseFloorInitVal = v;
            app.setNoiseFloorInit((float)v);
        }
        else if (arg.startsWith("--base-noise-floor-db="))
        {
            // dB value: convert to linear amplitude (20*log10 reference)
            double db = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            baseNoiseFloorDb = db;
            double lin = std::pow(10.0, db / 20.0);
            noiseFloorInitVal = lin;
            app.setNoiseFloorInit((float)lin);
        }
        else if (arg == "--fixed-noise-floor")
        {
            fixedNoiseFloor = true;
            app.setNoiseFloorFixed(true);
        }
        else if (arg.startsWith("--fixed-noise-floor="))
        {
            int v = arg.fromFirstOccurrenceOf("=", false, false).getIntValue();
            fixedNoiseFloor = (v != 0);
            app.setNoiseFloorFixed(fixedNoiseFloor);
        }
        else if (arg == "--voice-only")
        {
            voiceOnlyEnabled = true;
            app.setSendVoiceOnly(true);
        }
        else if (arg.startsWith("--voice-only="))
        {
            juce::String v = arg.fromFirstOccurrenceOf("=", false, false);
            bool on = (v != "0" && v != "false");
            voiceOnlyEnabled = on;
            app.setSendVoiceOnly(on);
        }
        else if (arg.startsWith("--voiceOnly="))
        {
            juce::String v = arg.fromFirstOccurrenceOf("=", false, false);
            bool on = (v != "0" && v != "false");
            voiceOnlyEnabled = on;
            app.setSendVoiceOnly(on);
        }
        else if (arg == "--voiceOnly" && i + 1 < argc)
        {
            juce::String v(argv[++i]);
            bool on = (v != "0" && v.toLowerCase() != "false");
            voiceOnlyEnabled = on;
            app.setSendVoiceOnly(on);
        }
        else if (arg == "--voice-min" && i + 1 < argc)
        {
            double v = std::atof(argv[++i]);
            voiceMin = v;
            voiceMinSet = true;
        }
        else if (arg == "--voice-max" && i + 1 < argc)
        {
            double v = std::atof(argv[++i]);
            voiceMax = v;
            voiceMaxSet = true;
        }
        else if (arg == "--map-min" && i + 1 < argc)
        {
            mapMin = std::atof(argv[++i]);
            mapMinSet = true;
        }
        else if (arg.startsWith("--map-min="))
        {
            mapMin = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMinSet = true;
        }
        else if (arg.startsWith("--mapMinFreq="))
        {
            mapMin = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMinSet = true;
        }
        else if (arg == "--mapMinFreq" && i + 1 < argc)
        {
            mapMin = std::atof(argv[++i]);
            mapMinSet = true;
        }
        else if (arg == "--map-max" && i + 1 < argc)
        {
            mapMax = std::atof(argv[++i]);
            mapMaxSet = true;
        }
        else if (arg.startsWith("--map-max="))
        {
            mapMax = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMaxSet = true;
        }
        else if (arg.startsWith("--mapMaxFreq="))
        {
            mapMax = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMaxSet = true;
        }
        // --bin-range removed
        else if (arg == "--mapMaxFreq" && i + 1 < argc)
        {
            mapMax = std::atof(argv[++i]);
            mapMaxSet = true;
        }
    }

    // apply mapping range if provided (use defaults for missing values)
    if (mapMinSet || mapMaxSet)
    {
        double useMin = mapMinSet ? mapMin : 80.0;
        double useMax = mapMaxSet ? mapMax : 24000.0;
        if (useMax >= useMin)
        {
            // If endpoints are equal, expand to a tiny window so downstream
            // voice-range logic (which requires max>min) can accept it and so
            // that a single-frequency selection still masks correctly.
            if (useMax == useMin)
            {
                double eps = std::max(1.0, useMin * 0.001); // at least 1 Hz or 0.1%
                double old = useMin;
                useMin = std::max(1.0, useMin - eps);
                useMax = old + eps;
                juce::Logger::writeToLog("Map range endpoints equal: expanded to " + juce::String(useMin) + " - " + juce::String(useMax));
            }
            app.setMapFreqRange(useMin, useMax);
            // If the user explicitly provided both map-min and map-max, treat
            // that as an implicit "voice-only" selection: apply the same
            // voice-range and enable send-only-voice so bins outside this
            // interval are zeroed.
            if (mapMinSet && mapMaxSet)
            {
                voiceMin = useMin;
                voiceMax = useMax;
                voiceMinSet = true;
                voiceMaxSet = true;
                app.setVoiceRange(useMin, useMax);
                app.setSendVoiceOnly(true);
                juce::Logger::writeToLog("Map range provided: enabling voice-only masking for " + juce::String(useMin) + "-" + juce::String(useMax));
            }
        }
        else
        {
            juce::Logger::writeToLog("Ignored map range: max must be >= min");
        }
    }

    // Apply voice range from CLI if either endpoint was provided; do this
    // after parsing to avoid order-dependence between --voice-min/--voice-max.
    if (voiceMinSet || voiceMaxSet)
    {
        double useMin = voiceMinSet ? voiceMin : app.getVoiceMinFreq();
        double useMax = voiceMaxSet ? voiceMax : app.getVoiceMaxFreq();
        if (useMax > useMin)
            app.setVoiceRange(useMin, useMax);
        else
            juce::Logger::writeToLog("Ignored voice range: max must be > min");
    }

        if (playFiles)
        {
            // split comma-separated list
            std::vector<juce::String> parts;
            int start = 0;
            while (true)
            {
                int comma = playFilesList.indexOfChar(',', start);
                if (comma < 0)
                {
                    parts.push_back(playFilesList.substring(start).trim());
                    break;
                }
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
                    auto name = f.getFileName();
                    if (name.startsWithChar('.'))
                        continue; // skip hidden files like .DS_Store
                    juce::String ext = f.getFileExtension().toLowerCase();
                    if (ext == ".wav" || ext == ".mp3" || ext == ".aiff" || ext == ".aif" || ext == ".flac" || ext == ".ogg" || ext == ".m4a")
                        parts.push_back(f.getFullPathName());
                    else
                        juce::Logger::writeToLog("Skipping non-audio file: " + f.getFullPathName());
                }
                if (!parts.empty())
                {
                    app.setPlaybackFiles(parts);
                    app.setFilePlaybackEnabled(true);
                }
                else
                {
                    juce::Logger::writeToLog("No files found in playback directory: " + playDirPath);
                }
            }
        }

    // log active CLI flags so it's obvious what options were used
    juce::String flagLog = "Flags: host=" + host + " port=" + juce::String(port)
        + " testTone=" + (testToneEnabled ? juce::String(testToneFreq) : "off")
        + " simpleSend=" + (simpleSendEnabled ? juce::String(simpleSendBins) : "off")
        + " interp=" + (interpEnabled ? "on" : "off")
        + " voiceOnly=" + (voiceOnlyEnabled ? "on" : "off")
        + " rmsAgg=" + (rmsEnabled ? "on" : "off")
        + " senderDiag=" + (senderDiag ? "on" : "off")
        + " senderInterval=" + juce::String(senderInterval)
        + " voiceMin=" + juce::String(voiceMin) + " voiceMax=" + juce::String(voiceMax)
        + " mapMinSet=" + (mapMinSet ? "yes" : "no") + " mapMin=" + juce::String(mapMin)
        + " mapMaxSet=" + (mapMaxSet ? "yes" : "no") + " mapMax=" + juce::String(mapMax);
    // binRange logging removed
    if (noNoiseFloor)
        flagLog += " noNoiseFloor=1";
    if (fixedNoiseFloor)
        flagLog += " fixedNoiseFloor=1";
    if (shufflePlay)
        flagLog += " shufflePlay=1";
    if (noiseFloorInitVal >= 0.0)
        flagLog += " noiseFloorInit=" + juce::String(noiseFloorInitVal);
    if (std::isfinite(baseNoiseFloorDb))
        flagLog += " baseNoiseFloorDb=" + juce::String(baseNoiseFloorDb);
    juce::Logger::writeToLog(flagLog);

    app.start();

    // If stdin is a TTY, block on Enter for interactive use; otherwise
    // keep the process alive until it's killed (suitable for nohup/daemon runs).
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

    app.stop();
    juce::Logger::writeToLog("Shutting down");
    return 0;
}
