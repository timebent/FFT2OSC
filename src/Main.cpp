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
    // enable test tone if requested on CLI
    double mapMin = 0.0, mapMax = 0.0;
    bool mapMinSet = false, mapMaxSet = false;
    for (int i = 1; i < argc; ++i)
    {
        juce::String arg (argv[i]);
        if (arg == "--test-tone" && i + 1 < argc)
        {
            float f = std::atof(argv[++i]);
            if (f > 0.0f)
                app.setTestTone(true, f);
        }
        else if (arg == "--simple-send" && i + 1 < argc)
        {
            int b = std::atoi(argv[++i]);
            app.setSimpleSend(true, b);
        }
        else if (arg == "--interp")
        {
            app.setInterpMode(true);
        }
        else if (arg == "--voice-only")
        {
            app.setSendVoiceOnly(true);
        }
        else if (arg == "--voice-min" && i + 1 < argc)
        {
            double v = std::atof(argv[++i]);
            app.setVoiceRange(v, app.getVoiceMaxFreq());
        }
        else if (arg == "--voice-max" && i + 1 < argc)
        {
            double v = std::atof(argv[++i]);
            app.setVoiceRange(app.getVoiceMinFreq(), v);
        }
        else if (arg == "--map-min" && i + 1 < argc)
        {
            mapMin = std::atof(argv[++i]);
            mapMinSet = true;
        }
        else if (arg == "--map-max" && i + 1 < argc)
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

    app.start();

    // blocking wait so the app keeps running until Enter (simple and reliable)
    std::string s;
    std::getline(std::cin, s);

    app.stop();
    juce::Logger::writeToLog("Shutting down");
    return 0;
}
