#include <JuceHeader.h>
#include "Theme.h"
#include "Audio/AudioEngine.h"
#include "Plugins/PluginScanner.h"
#include "UI/MainComponent.h"

//==============================================================================
class PluginPlayApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Plugin Play"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "PluginPlay";
        options.filenameSuffix = "settings";
        options.folderName = "PluginPlay";
        options.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (options);

        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        engine = std::make_unique<play::AudioEngine>();
        scanner = std::make_unique<play::PluginScanner> (engine->formatManager,
                                                         *appProperties.getUserSettings());

        engine->loadSession();

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *scanner);

        // First run: populate the plugin list automatically.
        if (scanner->knownPlugins.getTypes().isEmpty())
            scanner->startScan();
    }

    void shutdown() override
    {
        mainWindow = nullptr;   // closes plugin windows via MainComponent
        scanner = nullptr;
        engine = nullptr;       // saves the session
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, play::AudioEngine& engine, play::PluginScanner& scanner)
            : juce::DocumentWindow (name, play::Colours::panelBackground,
                                    juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new play::MainComponent (engine, scanner), true);
            setResizable (true, false);
            setResizeLimits (560, 520, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
    };

    juce::ApplicationProperties appProperties;
    play::PlayLookAndFeel lookAndFeel;

    std::unique_ptr<play::AudioEngine> engine;
    std::unique_ptr<play::PluginScanner> scanner;
    std::unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION (PluginPlayApplication)
