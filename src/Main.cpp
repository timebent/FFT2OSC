#include <JuceHeader.h>
#include "FFTOSC.h"
#include <thread>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    juce::String host = "127.0.0.1";
    int port = 57120;

    // Simple CLI parsing: --host <host> and --port <port>
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
        else if (arg == "--test-tone" && i + 1 < argc)
        {
            float f = std::atof(argv[++i]);
            if (f > 0.0f)
                ; // handled below
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
    double mapMin = 0.0, mapMax = 0.0;
    bool mapMinSet = false, mapMaxSet = false;

    for (int i = 1; i < argc; ++i)
    {
        juce::String arg (argv[i]);
        if (arg == "--test-tone" && i + 1 < argc)
        {
            double f = std::atof(argv[++i]);
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
        else if (arg == "--interp")
        {
            interpEnabled = true;
            app.setInterpMode(true);
        }
        else if (arg == "--voice-only")
        {
            voiceOnlyEnabled = true;
            app.setSendVoiceOnly(true);
        }
        else if (arg == "--voice-min" && i + 1 < argc)
        {
            double v = std::atof(argv[++i]);
            voiceMin = v;
            app.setVoiceRange(v, app.getVoiceMaxFreq());
        }
        else if (arg == "--voice-max" && i + 1 < argc)
        {
            double v = std::atof(argv[++i]);
            voiceMax = v;
            app.setVoiceRange(app.getVoiceMinFreq(), v);
        }
        else if (arg == "--map-min" && i + 1 < argc)
        {
            mapMin = std::atof(argv[++i]);
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
        else if (arg.startsWith("--mapMaxFreq="))
        {
            mapMax = arg.fromFirstOccurrenceOf("=", false, false).getDoubleValue();
            mapMaxSet = true;
        }
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
        if (useMax > useMin)
            app.setMapFreqRange(useMin, useMax);
    }

    // log active CLI flags so it's obvious what options were used
    juce::String flagLog = "Flags: host=" + host + " port=" + juce::String(port)
        + " testTone=" + (testToneEnabled ? juce::String(testToneFreq) : "off")
        + " simpleSend=" + (simpleSendEnabled ? juce::String(simpleSendBins) : "off")
        + " interp=" + (interpEnabled ? "on" : "off")
        + " voiceOnly=" + (voiceOnlyEnabled ? "on" : "off")
        + " voiceMin=" + juce::String(voiceMin) + " voiceMax=" + juce::String(voiceMax)
        + " mapMinSet=" + (mapMinSet ? "yes" : "no") + " mapMin=" + juce::String(mapMin)
        + " mapMaxSet=" + (mapMaxSet ? "yes" : "no") + " mapMax=" + juce::String(mapMax);
    juce::Logger::writeToLog(flagLog);

    app.start();

    // blocking wait so the app keeps running until Enter (simple and reliable)
    std::string s;
    std::getline(std::cin, s);

    app.stop();
    juce::Logger::writeToLog("Shutting down");
    return 0;
}
