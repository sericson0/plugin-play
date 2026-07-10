#include <JuceHeader.h>
#include "Theme.h"
#include "Audio/AppRouting.h"
#include "Audio/AudioEngine.h"
#include "Plugins/PluginScanner.h"
#include "UI/MainComponent.h"

//==============================================================================
class PluginPlayApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Plugin Play"; }
    // Single source of truth: the version set in CMakeLists (project VERSION),
    // which also stamps the exe's Windows version resource and the installer.
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        // Single-instance for normal launches. The uninstaller's headless
        // --cleanup-redirects run is exempt: it must do its cleanup and exit even if
        // (or especially if) the app is still open, not raise the existing window.
        return getCommandLineParameters().contains ("--cleanup-redirects");
    }

    void initialise (const juce::String& commandLine) override
    {
        // Headless mode used by the uninstaller: if a crashed run left an app's audio
        // routed into the virtual cable (redirect.marker still present), restore that
        // app's normal output before Plugin Play disappears for good — otherwise the
        // app would be left silently playing into a cable with nothing to undo it.
        // Normal launches do the same cleanup in AudioEngine::loadSession(); this path
        // exists so uninstalling still heals the routing without opening any UI.
        if (commandLine.contains ("--cleanup-redirects"))
        {
            const auto marker = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("PluginPlay")
                                    .getChildFile ("redirect.marker");

            if (marker.existsAsFile())
            {
                const auto exe = marker.loadFileAsString().trim();
                marker.deleteFile();

                if (exe.isNotEmpty() && ! play::AppRouting::clearAppOutputByName (exe))
                    play::AppRouting::clearAllOverrides();
            }

            quit();
            return;
        }

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

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), *engine, *scanner,
                                                   *appProperties.getUserSettings());

        // Defer the heavy startup work — scanning/opening the audio device and
        // re-creating the saved plugin chain (each VST3 loads its DLL on the message
        // thread) — until after the window's first paint, so the app appears
        // immediately instead of after seconds of silent loading. A plain posted
        // callback would still run before the first paint (Windows only delivers
        // WM_PAINT once the message queue is idle), hence the short timer.
        juce::Timer::callAfterDelay (100, [this]
        {
            if (engine == nullptr)
                return;

            engine->loadSession();

            // First run: populate the plugin list automatically.
            if (scanner->knownPlugins.getTypes().isEmpty())
                scanner->startScan();
        });
    }

    void shutdown() override
    {
        mainWindow = nullptr;   // closes plugin windows via MainComponent
        scanner = nullptr;
        engine = nullptr;       // saves the session
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

    // Single-instance app: when the user launches it again (e.g. double-clicks the
    // icon while it's already running), bring the existing window forward instead of
    // silently doing nothing — otherwise it reads as "the app won't open".
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised (false);
            mainWindow->setVisible (true);
            mainWindow->toFront (true);
        }
    }

private:
    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, play::AudioEngine& engine, play::PluginScanner& scanner,
                    juce::PropertiesFile& settingsToUse)
            : juce::DocumentWindow (name, play::Colours::panelBackground,
                                    juce::DocumentWindow::allButtons),
              settings (settingsToUse)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new play::MainComponent (engine, scanner), true);
            setResizable (true, false);
            setResizeLimits (700, 560, 10000, 10000);

            // Restore the last window position/size if we have one; otherwise centre
            // at the content's default size. Guard against a saved position on a
            // monitor that's no longer connected (a DJ undocking a laptop): if the
            // window wouldn't land somewhere its title bar can be grabbed, re-centre.
            const auto state = settings.getValue ("windowState");
            if (state.isNotEmpty())
            {
                restoreWindowStateFromString (state);

                if (! isTitleBarReachable())
                    centreWithSize (getWidth(), getHeight());
            }
            else
            {
                centreWithSize (getWidth(), getHeight());
            }

            setVisible (true);
            play::applyDarkTitleBar (*this);
        }

        /** True if a grabbable strip of the window's title bar lands on a connected
            display — i.e. the window isn't stranded off-screen (disconnected monitor,
            or dragged above the top of the screen). */
        bool isTitleBarReachable() const
        {
            const auto bounds = getBounds();
            const juce::Rectangle<int> titleStrip (bounds.getX(), bounds.getY(),
                                                   bounds.getWidth(),
                                                   juce::jmin (bounds.getHeight(), 30));

            for (auto& display : juce::Desktop::getInstance().getDisplays().displays)
                if (display.userArea.getIntersection (titleStrip).getWidth() >= 120)
                    return true;

            return false;
        }

        ~MainWindow() override
        {
            settings.setValue ("windowState", getWindowStateAsString());
            settings.saveIfNeeded();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        juce::PropertiesFile& settings;
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
