#include <JuceHeader.h>
#include "FFTOSC.h"

int main (int argc, char* argv[])
{
    juce::ignoreUnused (argc, argv);
    juce::ScopedJuceInitialiser_GUI libraryInitialiser;

    juce::Logger::writeToLog("Starting JUCE FFT OSC (press Enter to quit)");

    FFTOSC app;
    app.start();

    // Simple blocking wait so the app keeps running
    std::string s;
    std::getline(std::cin, s);

    app.stop();
    juce::Logger::writeToLog("Shutting down");
    return 0;
}
