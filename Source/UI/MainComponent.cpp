#include "MainComponent.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
void LevelMeter::pushLevels (float left, float right)
{
    const float incoming[2] { left, right };

    for (int ch = 0; ch < 2; ++ch)
        display[ch] = juce::jmax (incoming[ch], display[ch] * 0.82f);

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (gridText);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (label, area.removeFromLeft (28), juce::Justification::centredLeft);

    auto barArea = area.reduced (0, 4);
    const int barHeight = (barArea.getHeight() - 3) / 2;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = ch == 0 ? barArea.removeFromTop (barHeight)
                           : barArea.removeFromBottom (barHeight);

        g.setColour (sliderTrack);
        g.fillRoundedRectangle (bar.toFloat(), 2.0f);

        const auto dB = juce::Decibels::gainToDecibels (display[ch], -60.0f);
        const auto proportion = juce::jmap (dB, -60.0f, 0.0f, 0.0f, 1.0f);

        if (proportion > 0.001f)
        {
            auto fill = bar.toFloat().withWidth ((float) bar.getWidth() * juce::jlimit (0.0f, 1.0f, proportion));
            g.setColour (dB > -3.0f ? metricBad : (dB > -12.0f ? metricWarn : metricGood));
            g.fillRoundedRectangle (fill, 2.0f);
        }
    }
}

//==============================================================================
MainComponent::MainComponent (AudioEngine& engineToUse, PluginScanner& scannerToUse)
    : engine (engineToUse), scanner (scannerToUse)
{
    settingsButton.onClick = [this] { showAudioSettings(); };
    scanButton.onClick     = [this] { scanner.startScan(); updateScanButton(); };

    addAndMakeVisible (settingsButton);
    addAndMakeVisible (scanButton);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    chainView.onAddClicked = [this] (juce::Point<int> pos) { showAddPluginMenu (pos); };
    chainView.onOpenEditor = [this] (int index) { openPluginEditor (index); };

    viewport.setViewedComponent (&chainView, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, gridText);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (statusLabel);

    engine.addChangeListener (this);
    scanner.addChangeListener (this);

    engine.onPluginAboutToBeRemoved = [this] (juce::AudioProcessorGraph::NodeID nodeID)
    {
        closePluginWindow (nodeID);
    };

    startTimerHz (30);
    setSize (560, 700);
    updateStatusText();
}

MainComponent::~MainComponent()
{
    engine.onPluginAboutToBeRemoved = nullptr;
    engine.removeChangeListener (this);
    scanner.removeChangeListener (this);
    pluginWindows.clear();
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto header = getLocalBounds().removeFromTop (56);

    g.setColour (panelBackground);
    g.fillRect (header);
    g.setColour (gridLine);
    g.fillRect (header.removeFromBottom (1));

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("PLUGIN", 16, 0, 90, 56, juce::Justification::centredLeft);
    g.setColour (textBright);
    g.drawText ("PLAY", 103, 0, 70, 56, juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (56).reduced (12, 13);
    scanButton.setBounds (header.removeFromRight (120));
    header.removeFromRight (8);
    settingsButton.setBounds (header.removeFromRight (140));

    auto meterRow = area.removeFromTop (40).reduced (16, 6);
    auto half = meterRow.getWidth() / 2;
    inputMeter.setBounds (meterRow.removeFromLeft (half - 8));
    meterRow.removeFromLeft (16);
    outputMeter.setBounds (meterRow);

    auto footer = area.removeFromBottom (26);
    statusLabel.setBounds (footer.reduced (16, 0));

    viewport.setBounds (area.reduced (12, 4));
    chainView.setSize (viewport.getMaximumVisibleWidth(), chainView.getIdealHeight());
}

//==============================================================================
void MainComponent::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &engine)
    {
        chainView.refresh();
        chainView.setSize (viewport.getMaximumVisibleWidth(), chainView.getIdealHeight());
    }

    updateScanButton();
    updateStatusText();
}

void MainComponent::timerCallback()
{
    inputMeter.pushLevels (engine.readInputPeak (0), engine.readInputPeak (1));
    outputMeter.pushLevels (engine.readOutputPeak (0), engine.readOutputPeak (1));

    if (++timerTicks % 30 == 0)
        updateStatusText();
}

//==============================================================================
void MainComponent::showAddPluginMenu (juce::Point<int>)
{
    juce::PopupMenu menu;

    const auto types = scanner.knownPlugins.getTypes();

    if (types.isEmpty())
    {
        menu.addItem (999901, scanner.isScanning() ? "Scanning for plugins..."
                                                   : "No VST3 plugins found - scan now",
                      ! scanner.isScanning());
    }
    else
    {
        juce::KnownPluginList::addToMenu (menu, types,
                                          juce::KnownPluginList::sortByManufacturer);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (nullptr)
                            .withMousePosition(),
        [this, types] (int result)
        {
            if (result == 999901)
            {
                scanner.startScan();
                updateScanButton();
                return;
            }

            const auto index = juce::KnownPluginList::getIndexChosenByMenu (types, result);

            if (juce::isPositiveAndBelow (index, types.size()))
            {
                engine.addPlugin (types.getReference (index),
                    [] (const juce::String& error)
                    {
                        if (error.isNotEmpty())
                            juce::AlertWindow::showMessageBoxAsync (
                                juce::MessageBoxIconType::WarningIcon,
                                "Couldn't load plugin", error);
                    });
            }
        });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (
        engine.deviceManager, 0, 2, 2, 2, false, false, true, false);
    selector->setSize (500, 460);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio Settings";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

//==============================================================================
void MainComponent::openPluginEditor (int slotIndex)
{
    if (! juce::isPositiveAndBelow (slotIndex, engine.getNumPlugins()))
        return;

    const auto& slot = engine.getSlot (slotIndex);
    const auto key = slot.nodeID.uid;

    if (auto existing = pluginWindows.find (key); existing != pluginWindows.end())
    {
        existing->second->toFront (true);
        return;
    }

    if (auto* processor = engine.getProcessor (slotIndex))
    {
        auto nodeID = slot.nodeID;
        pluginWindows[key] = std::make_unique<PluginWindow> (
            *processor, slot.description.name,
            [this, nodeID] { closePluginWindow (nodeID); });
    }
}

void MainComponent::closePluginWindow (juce::AudioProcessorGraph::NodeID nodeID)
{
    pluginWindows.erase (nodeID.uid);
}

//==============================================================================
void MainComponent::updateScanButton()
{
    const auto scanning = scanner.isScanning();
    scanButton.setEnabled (! scanning);
    scanButton.setButtonText (scanning ? "SCANNING..." : "SCAN PLUGINS");
}

void MainComponent::updateStatusText()
{
    juce::String text;

    if (scanner.isScanning())
    {
        auto current = scanner.getProgressText();
        text = "Scanning: " + (current.isEmpty() ? "..." : current);
    }
    else if (auto* device = engine.deviceManager.getCurrentAudioDevice())
    {
        const auto latencyMs = 1000.0 * device->getOutputLatencyInSamples()
                                      / device->getCurrentSampleRate();

        text << engine.deviceManager.getCurrentAudioDeviceType()
             << "  |  " << device->getName()
             << "  |  " << juce::String (device->getCurrentSampleRate() / 1000.0, 1) << " kHz"
             << "  |  " << device->getCurrentBufferSizeSamples() << " smp"
             << "  |  out " << juce::String (latencyMs, 1) << " ms"
             << "  |  CPU " << juce::String (engine.deviceManager.getCpuUsage() * 100.0, 0) << "%";
    }
    else
    {
        text = "No audio device - open Audio Settings";
    }

    statusLabel.setText (text, juce::dontSendNotification);
}

} // namespace play
