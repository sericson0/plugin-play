#include "MainComponent.h"
#include "PluginPicker.h"
#include "../Audio/WasapiEndpoints.h"
#include "BinaryData.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
void LevelMeter::pushLevels (float left, float right)
{
    const float incoming[2] { left, right };

    for (int ch = 0; ch < 2; ++ch)
    {
        display[ch] = juce::jmax (incoming[ch], display[ch] * 0.82f);

        if (incoming[ch] >= peakHold[ch])
        {
            peakHold[ch] = incoming[ch];
            holdFrames[ch] = 45;                 // ~1.5 s at the 30 Hz UI timer
        }
        else if (holdFrames[ch] > 0)
        {
            --holdFrames[ch];
        }
        else
        {
            peakHold[ch] *= 0.85f;
        }

        if (incoming[ch] >= 1.0f)
            clipped = true;
    }

    repaint();
}

void LevelMeter::mouseDown (const juce::MouseEvent&)
{
    clipped = false;
    peakHold[0] = peakHold[1] = 0.0f;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds();

    g.setColour (gridText);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText (label, area.removeFromLeft (28), juce::Justification::centredLeft);

    // Latching clip light at the right-hand end; click the meter to reset.
    auto lamp = area.removeFromRight (10).reduced (0, 6).toFloat();
    g.setColour (clipped ? metricBad : sliderTrack);
    g.fillRoundedRectangle (lamp, 2.0f);
    area.removeFromRight (4);

    auto proportionFor = [] (float level)
    {
        const auto dB = juce::Decibels::gainToDecibels (level, -60.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (dB, -60.0f, 0.0f, 0.0f, 1.0f));
    };

    auto barArea = area.reduced (0, 4);
    const int barHeight = (barArea.getHeight() - 3) / 2;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = ch == 0 ? barArea.removeFromTop (barHeight)
                           : barArea.removeFromBottom (barHeight);

        g.setColour (sliderTrack);
        g.fillRoundedRectangle (bar.toFloat(), 2.0f);

        const auto dB = juce::Decibels::gainToDecibels (display[ch], -60.0f);
        const auto proportion = proportionFor (display[ch]);

        if (proportion > 0.001f)
        {
            auto fill = bar.toFloat().withWidth ((float) bar.getWidth() * proportion);
            g.setColour (dB > -3.0f ? metricBad : (dB > -12.0f ? metricWarn : metricGood));
            g.fillRoundedRectangle (fill, 2.0f);
        }

        const auto holdProportion = proportionFor (peakHold[ch]);

        if (holdProportion > 0.001f)
        {
            const auto x = (float) bar.getX() + (float) (bar.getWidth() - 2) * holdProportion;
            g.setColour (textBright.withAlpha (0.8f));
            g.fillRect (x, (float) bar.getY(), 2.0f, (float) bar.getHeight());
        }
    }
}

//==============================================================================
MainComponent::MainComponent (AudioEngine& engineToUse, PluginScanner& scannerToUse)
    : engine (engineToUse), scanner (scannerToUse)
{
    settingsButton.onClick = [this] { showAudioSettings(); };
    scanButton.onClick     = [this] { scanner.startScan(); updateScanButton(); };
    cableButton.onClick    = [this] { CableSetupComponent::launch (engine.deviceManager); };
    presetsButton.onClick  = [this] { showPresetsMenu(); };
    killButton.onClick     = [this] { engine.setMasterBypass (! engine.isMasterBypassed()); };
    killButton.setColour (juce::TextButton::textColourOffId, accentBright);
    killButton.setColour (juce::TextButton::buttonOnColourId, metricBad.darker (0.5f));

    addAndMakeVisible (settingsButton);
    addAndMakeVisible (scanButton);
    addAndMakeVisible (cableButton);
    addAndMakeVisible (presetsButton);
    addAndMakeVisible (killButton);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    updateKillButton();

    // ── Device bar (below the meters) ────────────────────────────────────────
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName  = "PluginPlayUI";
        opts.filenameSuffix   = "settings";
        opts.folderName       = "PluginPlay";
        opts.osxLibrarySubFolder = "Application Support";
        uiPrefs = std::make_unique<juce::PropertiesFile> (opts);
    }

    for (auto* l : { &inputLabel, &outputLabel, &rateLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, gridText);
        addAndMakeVisible (*l);
    }

    inputSelector .setTextWhenNoChoicesAvailable ("No inputs");
    outputSelector.setTextWhenNoChoicesAvailable ("No outputs");
    inputSelector .onChange = [this] { if (! updatingSelectors) applyDeviceSelection(); };
    outputSelector.onChange = [this] { if (! updatingSelectors) applyDeviceSelection(); };

    autoRate = uiPrefs->getBoolValue ("autoRate", true);

    sampleRateSelector.onChange = [this]
    {
        if (updatingSelectors)
            return;

        const auto id = sampleRateSelector.getSelectedId();

        // The first item ("Auto — match source") re-enables auto-match; any
        // specific rate is a manual override.
        autoRate = (id == autoRateItemId);
        uiPrefs->setValue ("autoRate", autoRate);
        lastAutoAppliedRate = 0.0;

        if (autoRate)
            checkSampleRate();
        else
            applySampleRate ((double) id);
    };

    rateHint.setFont (juce::Font (juce::FontOptions (11.0f)));
    rateHint.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (inputSelector);
    addAndMakeVisible (outputSelector);
    addAndMakeVisible (sampleRateSelector);
    addAndMakeVisible (rateHint);

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
    setSize (720, 700);
    buildDeviceSelectors();
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

    // Square app logo, then the wordmark beside it.
    int textX = 16;
    static const auto logo = juce::ImageCache::getFromMemory (BinaryData::exe_logo_png,
                                                              BinaryData::exe_logo_pngSize);
    if (logo.isValid())
    {
        const int logoSize = 40;
        g.drawImageWithin (logo, 12, (56 - logoSize) / 2, logoSize, logoSize,
                           juce::RectanglePlacement::centred, false);
        textX = 12 + logoSize + 10;
    }

    g.setColour (accent);
    g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
    g.drawText ("PLUGIN", textX, 0, 90, 56, juce::Justification::centredLeft);
    g.setColour (textBright);
    g.drawText ("PLAY", textX + 87, 0, 70, 56, juce::Justification::centredLeft);

    // Device bar panel behind the input/output/rate selectors.
    if (! deviceBarBounds.isEmpty())
    {
        g.setColour (panelBackground);
        g.fillRect (deviceBarBounds);
        g.setColour (gridLine);
        g.fillRect (deviceBarBounds.getX(), deviceBarBounds.getY(), deviceBarBounds.getWidth(), 1);
        g.fillRect (deviceBarBounds.getX(), deviceBarBounds.getBottom() - 1, deviceBarBounds.getWidth(), 1);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (56).reduced (12, 13);
    scanButton.setBounds (header.removeFromRight (116));
    header.removeFromRight (8);
    settingsButton.setBounds (header.removeFromRight (130));
    header.removeFromRight (8);
    cableButton.setBounds (header.removeFromRight (124));
    header.removeFromRight (8);
    presetsButton.setBounds (header.removeFromRight (92));

    // Meter row: IN meter | FX kill switch | OUT meter.
    auto meterRow = area.removeFromTop (40).reduced (16, 6);
    const int killWidth = 84;
    inputMeter.setBounds (meterRow.removeFromLeft ((meterRow.getWidth() - killWidth) / 2 - 8));
    meterRow.removeFromLeft (8);
    killButton.setBounds (meterRow.removeFromLeft (killWidth));
    meterRow.removeFromLeft (8);
    outputMeter.setBounds (meterRow);

    // ── Device bar: input / output on one row, rate + status on the next ──────
    deviceBarBounds = area.removeFromTop (96);
    auto bar = deviceBarBounds.reduced (16, 10);

    auto labelRow = bar.removeFromTop (14);
    auto comboRow = bar.removeFromTop (26);
    const int barHalf = bar.getWidth() / 2;

    inputLabel .setBounds (labelRow.removeFromLeft (barHalf).withTrimmedRight (8));
    outputLabel.setBounds (labelRow);
    inputSelector .setBounds (comboRow.removeFromLeft (barHalf).withTrimmedRight (8));
    outputSelector.setBounds (comboRow);

    bar.removeFromTop (10);
    auto rateRow = bar.removeFromTop (26);
    rateLabel.setBounds (rateRow.removeFromLeft (42));
    sampleRateSelector.setBounds (rateRow.removeFromLeft (120));
    rateRow.removeFromLeft (14);
    rateHint.setBounds (rateRow);   // status sits inline beside the rate dropdown

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
        buildDeviceSelectors();   // device may have changed (Audio Settings, hot-plug)
    }

    updateScanButton();
    updateKillButton();
    updateStatusText();
}

void MainComponent::timerCallback()
{
    inputMeter.pushLevels (engine.readInputPeak (0), engine.readInputPeak (1));
    outputMeter.pushLevels (engine.readOutputPeak (0), engine.readOutputPeak (1));

    if (++timerTicks % 30 == 0)
    {
        updateStatusText();
        checkSampleRate();   // the source's rate can change while we're running
    }
}

//==============================================================================
void MainComponent::showAddPluginMenu (juce::Point<int> screenPosition)
{
    const auto types = scanner.knownPlugins.getTypes();

    if (types.isEmpty())
    {
        juce::PopupMenu menu;
        menu.addItem (1, scanner.isScanning() ? "Scanning for plugins..."
                                              : "No VST3 plugins found - scan now",
                      ! scanner.isScanning());

        menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
            [this] (int result)
            {
                if (result == 1)
                {
                    scanner.startScan();
                    updateScanButton();
                }
            });
        return;
    }

    auto picker = std::make_unique<PluginPicker> (types,
        [this] (const juce::PluginDescription& description)
        {
            engine.addPlugin (description,
                [] (const juce::String& error)
                {
                    if (error.isNotEmpty())
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon,
                            "Couldn't load plugin", error);
                });
        });

    juce::CallOutBox::launchAsynchronously (std::move (picker),
        { screenPosition.x, screenPosition.y, 1, 1 }, nullptr);
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

    if (auto* window = options.launchAsync())
        applyDarkTitleBar (*window);
}

//==============================================================================
void MainComponent::showPresetsMenu()
{
    auto presetFiles = engine.getPresetsDirectory()
                           .findChildFiles (juce::File::findFiles, false, "*.xml");
    presetFiles.sort();

    juce::PopupMenu menu;
    menu.addItem (1, "Save chain as preset...", engine.getNumPlugins() > 0);

    if (! presetFiles.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Load");

        for (int i = 0; i < presetFiles.size(); ++i)
            menu.addItem (100 + i, presetFiles[i].getFileNameWithoutExtension());

        juce::PopupMenu deleteMenu;
        for (int i = 0; i < presetFiles.size(); ++i)
            deleteMenu.addItem (1000 + i, presetFiles[i].getFileNameWithoutExtension());

        menu.addSeparator();
        menu.addSubMenu ("Delete preset", deleteMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetsButton),
        [this, presetFiles] (int result)
        {
            if (result == 1)
            {
                promptSavePreset();
            }
            else if (result >= 1000)
            {
                presetFiles[result - 1000].deleteFile();
            }
            else if (result >= 100)
            {
                if (! engine.loadPreset (presetFiles[result - 100]))
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon, "Couldn't load preset",
                        "The preset file couldn't be read:\n"
                            + presetFiles[result - 100].getFullPathName());
            }
        });
}

void MainComponent::promptSavePreset()
{
    auto* window = new juce::AlertWindow ("Save preset", "Name for this chain:",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("name", {});
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true,
        juce::ModalCallbackFunction::create ([this, window] (int result)
        {
            const auto name = window->getTextEditorContents ("name").trim();

            if (result != 1 || name.isEmpty())
                return;

            const auto file = engine.getPresetsDirectory()
                                  .getChildFile (juce::File::createLegalFileName (name) + ".xml");

            if (! engine.savePreset (file))
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Couldn't save preset",
                    "The preset file couldn't be written:\n" + file.getFullPathName());
        }),
        true);   // delete the window when dismissed
}

//==============================================================================
void MainComponent::buildDeviceSelectors()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    auto* type = engine.deviceManager.getCurrentDeviceTypeObject();
    auto setup = engine.deviceManager.getAudioDeviceSetup();

    auto fill = [type] (juce::ComboBox& box, bool wantInputs, const juce::String& selected)
    {
        box.clear (juce::dontSendNotification);

        if (type == nullptr)
            return;

        int id = 1;
        for (const auto& name : type->getDeviceNames (wantInputs))
        {
            box.addItem (name, id);
            if (name == selected)
                box.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
    };

    fill (inputSelector,  true,  setup.inputDeviceName);
    fill (outputSelector, false, setup.outputDeviceName);

    refreshSampleRates();
    checkSampleRate();
}

void MainComponent::refreshSampleRates()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    sampleRateSelector.clear (juce::dontSendNotification);

    auto* device = engine.deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto current = device->getCurrentSampleRate();

    sampleRateSelector.addItem ("Auto - match source", autoRateItemId);
    sampleRateSelector.addSeparator();

    int currentId = 0;
    for (auto rate : device->getAvailableSampleRates())
    {
        const auto id = (int) juce::roundToInt (rate);
        sampleRateSelector.addItem (juce::String (rate / 1000.0, 1) + " kHz", id);

        if (std::abs (rate - current) < 1.0)
            currentId = id;
    }

    // When auto-matching, keep "Auto" selected; otherwise show the actual rate.
    sampleRateSelector.setSelectedId (autoRate ? autoRateItemId : currentId,
                                      juce::dontSendNotification);
}

void MainComponent::applyDeviceSelection()
{
    auto setup = engine.deviceManager.getAudioDeviceSetup();

    setup.inputDeviceName  = inputSelector .getText();
    setup.outputDeviceName = outputSelector.getText();
    setup.useDefaultInputChannels  = true;
    setup.useDefaultOutputChannels = true;

    lastAutoAppliedRate = 0.0;

    auto error = engine.deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Couldn't change device", error);

    // setAudioDeviceSetup broadcasts a change; buildDeviceSelectors() then refreshes.
}

void MainComponent::applySampleRate (double rate)
{
    if (rate <= 0.0)
        return;

    auto* device = engine.deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    // Only request rates the device can actually run, or the call just fails.
    bool supported = false;
    for (auto available : device->getAvailableSampleRates())
        if (std::abs (available - rate) < 1.0)
            supported = true;

    if (! supported)
        return;

    if (std::abs (device->getCurrentSampleRate() - rate) < 1.0)
        return;

    auto setup = engine.deviceManager.getAudioDeviceSetup();
    setup.sampleRate = rate;
    engine.deviceManager.setAudioDeviceSetup (setup, true);
}

double MainComponent::recommendedSampleRate() const
{
    // The rate the source is really running at in Windows — matching it avoids an
    // extra resample of the signal we care about (the captured DJ audio).
    const auto inputName = engine.deviceManager.getAudioDeviceSetup().inputDeviceName;
    return WasapiEndpoints::mixRateForDevice (inputName, true);
}

void MainComponent::checkSampleRate()
{
    auto* device = engine.deviceManager.getCurrentAudioDevice();

    if (device == nullptr)
    {
        rateHint.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto current = device->getCurrentSampleRate();
    const auto source  = recommendedSampleRate();

    auto asKHz = [] (double r) { return juce::String (r / 1000.0, 1) + " kHz"; };

    if (source <= 0.0)
    {
        // Couldn't read the source's native rate (e.g. non-WASAPI input).
        rateHint.setColour (juce::Label::textColourId, gridText);
        rateHint.setText ("Running at " + asKHz (current), juce::dontSendNotification);
        return;
    }

    if (std::abs (current - source) < 1.0)
    {
        rateHint.setColour (juce::Label::textColourId, metricGood);
        rateHint.setText ("Matched to source (" + asKHz (source) + ")", juce::dontSendNotification);
        return;
    }

    // Mismatch: source and Plugin Play disagree, so audio is being resampled.
    if (autoRate && std::abs (source - lastAutoAppliedRate) > 1.0)
    {
        lastAutoAppliedRate = source;   // guard against a re-apply loop if it can't switch
        applySampleRate (source);
        return;
    }

    rateHint.setColour (juce::Label::textColourId, metricWarn);
    rateHint.setText ("Source is " + asKHz (source) + " but you're at " + asKHz (current)
                          + " — audio is being resampled.",
                      juce::dontSendNotification);
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

void MainComponent::updateKillButton()
{
    const auto killed = engine.isMasterBypassed();

    killButton.setButtonText (killed ? "FX OFF" : "FX ON");
    killButton.setToggleState (killed, juce::dontSendNotification);
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
