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
    scanButton.onClick      = [this] { showScanMenu(); };
    cableButton.onClick     = [this] { CableSetupComponent::launch (engine.deviceManager); };
    helpButton.onClick      = [this] { showHelp(); };
    presetsButton.onClick   = [this] { showPresetsMenu(); };
    killButton.onClick      = [this] { engine.setMasterBypass (! engine.isMasterBypassed()); };
    killButton.setColour (juce::TextButton::textColourOffId, accentBright);
    killButton.setColour (juce::TextButton::buttonOnColourId, metricBad.darker (0.5f));

    limiterButton.onClick   = [this] { engine.setLimiterEnabled (! engine.isLimiterEnabled()); updateLimiterButton(); };
    limiterButton.setColour (juce::TextButton::textColourOffId, gridText);
    limiterButton.setColour (juce::TextButton::buttonOnColourId, metricGood.darker (0.4f));

    // "Add Plugin" is the primary action but should read like the other toolbar
    // buttons rather than a filled orange call-to-action.
    addPluginButton.onClick = [this] { showAddPluginMenu (addPluginButton.getScreenPosition()); };

    scanButton    .setTooltip ("Scan for installed VST3 plugins, or manage extra scan folders");
    cableButton   .setTooltip ("Set up the virtual audio cable that captures your DJ software");
    helpButton    .setTooltip ("Open the Plugin Play help & documentation");
    presetsButton .setTooltip ("Save the current chain, or load a saved one");
    killButton    .setTooltip ("Master bypass — turn every effect on or off at once");
    limiterButton .setTooltip ("Brickwall safety limiter on the output — protects against runaway plugin levels");
    addPluginButton.setTooltip ("Add a plugin to the end of the effect chain");

    addAndMakeVisible (scanButton);
    addAndMakeVisible (cableButton);
    addAndMakeVisible (helpButton);
    addAndMakeVisible (presetsButton);
    addAndMakeVisible (killButton);
    addAndMakeVisible (limiterButton);
    addAndMakeVisible (addPluginButton);
    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);
    updateKillButton();
    updateLimiterButton();

    // Catch Ctrl+Z (undo the last plugin removal) anywhere in the window.
    setWantsKeyboardFocus (true);

    // ── Device bar (below the meters) ────────────────────────────────────────
    {
        juce::PropertiesFile::Options opts;
        opts.applicationName  = "PluginPlayUI";
        opts.filenameSuffix   = "settings";
        opts.folderName       = "PluginPlay";
        opts.osxLibrarySubFolder = "Application Support";
        uiPrefs = std::make_unique<juce::PropertiesFile> (opts);
    }

    for (auto* l : { &inputLabel, &outputLabel, &driverLabel, &rateLabel, &bufferLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, gridText);
        addAndMakeVisible (*l);
    }

    inputSelector .setTextWhenNoChoicesAvailable ("No inputs");
    outputSelector.setTextWhenNoChoicesAvailable ("No outputs");
    inputSelector .onChange = [this] { if (! updatingSelectors) applyDeviceSelection(); };
    outputSelector.onChange = [this] { if (! updatingSelectors) applyDeviceSelection(); };

    inputChannelSelector .setTextWhenNoChoicesAvailable ("-");
    outputChannelSelector.setTextWhenNoChoicesAvailable ("-");
    inputChannelSelector .onChange = [this] { if (! updatingSelectors) applyChannelSelection (true); };
    outputChannelSelector.onChange = [this] { if (! updatingSelectors) applyChannelSelection (false); };

    deviceTypeSelector.onChange = [this]
    {
        if (! updatingSelectors)
            engine.deviceManager.setCurrentAudioDeviceType (deviceTypeSelector.getText(), true);
        // The device manager broadcasts the change; buildDeviceSelectors() then
        // repopulates every selector for the new driver's devices.
    };

    inputSelector       .setTooltip ("Audio input device — the capture source (usually the virtual cable)");
    outputSelector      .setTooltip ("Audio output device — where processed audio is sent");
    inputChannelSelector .setTooltip ("Which channel pair of the input device to capture");
    outputChannelSelector.setTooltip ("Which channel pair of the output device to play to");
    deviceTypeSelector  .setTooltip ("Audio driver type — ASIO gives the lowest latency if available");
    sampleRateSelector  .setTooltip ("Sample rate — 'Auto' follows your source to avoid an extra resample");
    bufferSizeSelector  .setTooltip ("Buffer size — smaller means lower latency, larger means more stable");
    testButton          .setTooltip ("Play a short test tone through the current output device");

    testButton.setColour (juce::TextButton::textColourOffId, accentBright);
    testButton.onClick = [this] { engine.deviceManager.playTestSound(); };

    autoRate      = uiPrefs->getBoolValue ("autoRate", true);
    audioExpanded = uiPrefs->getBoolValue ("audioExpanded", true);

    audioToggleButton.setColour (juce::TextButton::buttonColourId, panelBackground);
    audioToggleButton.setColour (juce::TextButton::textColourOffId, accentBright);
    audioToggleButton.setTooltip ("Show or hide the channel, driver, sample-rate and buffer settings");
    audioToggleButton.onClick = [this] { setAudioExpanded (! audioExpanded); };
    addAndMakeVisible (audioToggleButton);
    updateAudioToggle();

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

    bufferSizeSelector.onChange = [this]
    {
        if (! updatingSelectors)
            applyBufferSize (bufferSizeSelector.getSelectedId());
    };

    rateHint.setFont (juce::Font (juce::FontOptions (11.0f)));
    rateHint.setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (inputSelector);
    addAndMakeVisible (outputSelector);
    addAndMakeVisible (inputChannelSelector);
    addAndMakeVisible (outputChannelSelector);
    addAndMakeVisible (deviceTypeSelector);
    addAndMakeVisible (sampleRateSelector);
    addAndMakeVisible (bufferSizeSelector);
    addAndMakeVisible (testButton);
    addAndMakeVisible (rateHint);

    // Input/output device selection is always visible; the channel, driver,
    // rate and buffer controls only show while the audio area is expanded.
    juce::Component* expandedControls[] { &inputChannelSelector, &outputChannelSelector,
                                          &driverLabel, &deviceTypeSelector,
                                          &rateLabel, &bufferLabel,
                                          &sampleRateSelector, &bufferSizeSelector,
                                          &testButton, &rateHint };
    for (auto* c : expandedControls)
        c->setVisible (audioExpanded);

    chainView.onOpenEditor  = [this] (int index) { openPluginEditor (index); };
    chainView.onFloatEditor = [this] (int index, bool shouldFloat) { setPluginFloating (index, shouldFloat); };
    chainView.isFloating    = [this] (int index) { return isSlotFloating (index); };

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

    engine.onRestoreErrors = [] (const juce::StringArray& failed)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Some plugins couldn't be restored",
            "These plugins in your saved chain couldn't be loaded (missing or "
            "incompatible) and were skipped:\n\n  " + failed.joinIntoString ("\n  "));
    };

    startTimerHz (30);
    setSize (720, 700);
    buildDeviceSelectors();
    updateStatusText();
}

MainComponent::~MainComponent()
{
    engine.onPluginAboutToBeRemoved = nullptr;
    engine.onRestoreErrors = nullptr;
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
    static const auto logo = juce::ImageCache::getFromMemory (BinaryData::exe_logo_transparent_png,
                                                              BinaryData::exe_logo_transparent_pngSize);
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

    // Device area panel behind the input/output/rate selectors.
    if (! deviceBarBounds.isEmpty())
    {
        g.setColour (panelBackground);
        g.fillRect (deviceBarBounds);
        g.setColour (gridLine.brighter (0.2f));
        g.fillRect (deviceBarBounds.getX(), deviceBarBounds.getY(), deviceBarBounds.getWidth(), 1);
        g.fillRect (deviceBarBounds.getX(), deviceBarBounds.getBottom() - 1, deviceBarBounds.getWidth(), 1);
    }

    // Toolbar panel — a distinct band separating the audio area from the chain.
    if (! toolbarBounds.isEmpty())
    {
        g.setColour (background.brighter (0.035f));
        g.fillRect (toolbarBounds);
        g.setColour (gridLine.brighter (0.2f));
        g.fillRect (toolbarBounds.getX(), toolbarBounds.getY(), toolbarBounds.getWidth(), 1);
        g.fillRect (toolbarBounds.getX(), toolbarBounds.getBottom() - 1, toolbarBounds.getWidth(), 1);
    }
}

void MainComponent::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (56).reduced (12, 13);
    helpButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (8);
    cableButton.setBounds (header.removeFromRight (124));

    // Meter row: IN meter | FX kill switch | OUT meter.
    auto meterRow = area.removeFromTop (40).reduced (16, 6);
    const int killWidth = 84;
    inputMeter.setBounds (meterRow.removeFromLeft ((meterRow.getWidth() - killWidth) / 2 - 8));
    meterRow.removeFromLeft (8);
    killButton.setBounds (meterRow.removeFromLeft (killWidth));
    meterRow.removeFromLeft (8);
    outputMeter.setBounds (meterRow);

    // ── Device area (collapsible): input / output devices are always shown;
    //    expanding adds channel selection plus the driver / rate / buffer row.
    //    The expand/collapse toggle sits centred at the bottom. ─
    deviceBarBounds = area.removeFromTop (audioExpanded ? 168 : 94);
    auto bar = deviceBarBounds.reduced (16, 10);

    audioToggleButton.setBounds (bar.removeFromBottom (24).withSizeKeepingCentre (170, 24));
    bar.removeFromBottom (10);   // gap between the content and the toggle

    // Input | output columns; when expanded each device combo gets a compact
    // channel-pair selector on its right.
    auto labelRow = bar.removeFromTop (14);
    auto comboRow = bar.removeFromTop (26);
    const int barHalf = labelRow.getWidth() / 2;

    inputLabel .setBounds (labelRow.removeFromLeft (barHalf).withTrimmedRight (8));
    outputLabel.setBounds (labelRow);

    auto inputCol  = comboRow.removeFromLeft (barHalf).withTrimmedRight (8);
    auto outputCol = comboRow;

    if (audioExpanded)
    {
        const int channelWidth = 74;
        inputChannelSelector .setBounds (inputCol .removeFromRight (channelWidth));
        outputChannelSelector.setBounds (outputCol.removeFromRight (channelWidth));
        inputCol .removeFromRight (6);
        outputCol.removeFromRight (6);
    }

    inputSelector .setBounds (inputCol);
    outputSelector.setBounds (outputCol);

    if (audioExpanded)
    {
        bar.removeFromTop (12);
        auto settingsLabels = bar.removeFromTop (14);
        auto settingsCombos = bar.removeFromTop (26);

        // TEST sits at the right end of the control row (plays a test tone).
        testButton.setBounds (settingsCombos.removeFromRight (64));
        settingsCombos.removeFromRight (14);
        settingsLabels.removeFromRight (64 + 14);

        auto placeColumn = [&] (juce::Label& label, juce::ComboBox& box, int width)
        {
            label.setBounds (settingsLabels.removeFromLeft (width));
            box.setBounds (settingsCombos.removeFromLeft (width));
            settingsLabels.removeFromLeft (14);
            settingsCombos.removeFromLeft (14);
        };

        // The driver name (e.g. "Windows Audio (Exclusive Mode)") is the longest,
        // so it gets the widest column.
        placeColumn (driverLabel, deviceTypeSelector, 230);
        placeColumn (rateLabel,   sampleRateSelector, 120);
        placeColumn (bufferLabel, bufferSizeSelector, 185);

        bar.removeFromTop (4);
        rateHint.setBounds (bar.removeFromTop (16));
    }

    // ── Toolbar: scan (left) | add plugin (big, middle) | presets (right) ─────
    toolbarBounds = area.removeFromTop (52);
    auto toolbar = toolbarBounds.reduced (16, 8);
    scanButton.setBounds (toolbar.removeFromLeft (130));
    presetsButton.setBounds (toolbar.removeFromRight (110));
    toolbar.removeFromRight (8);
    limiterButton.setBounds (toolbar.removeFromRight (90));
    toolbar.removeFromLeft (12);
    toolbar.removeFromRight (12);
    addPluginButton.setBounds (toolbar);   // fills the middle — the primary action

    auto footer = area.removeFromBottom (26);
    statusLabel.setBounds (footer.reduced (16, 0));

    viewport.setBounds (area.reduced (12, 4));
    chainView.setSize (viewport.getMaximumVisibleWidth(), chainView.getIdealHeight());
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        if (engine.canUndoRemove())
        {
            engine.undoRemove();
            return true;
        }
    }

    return false;
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
    updateLimiterButton();
    updateStatusText();
}

void MainComponent::timerCallback()
{
    const float inL = engine.readInputPeak (0);
    const float inR = engine.readInputPeak (1);
    inputMeter.pushLevels (inL, inR);
    outputMeter.pushLevels (engine.readOutputPeak (0), engine.readOutputPeak (1));

    // Silence watchdog: if a running input has produced no signal for ~10 s, hint
    // that the DJ software may not be routed to it. Only nag when an input device
    // is actually selected.
    const bool haveInput = engine.deviceManager.getCurrentAudioDevice() != nullptr
                             && engine.deviceManager.getAudioDeviceSetup().inputDeviceName.isNotEmpty();

    if (haveInput && juce::jmax (inL, inR) < 1.0e-5f)
        ++silentTicks;
    else
        silentTicks = 0;

    const bool nowSilent = haveInput && silentTicks >= 30 * 10;   // 10 s at 30 Hz

    if (nowSilent != noInputSignal)
    {
        noInputSignal = nowSilent;
        updateStatusText();
    }

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

void MainComponent::showScanMenu()
{
    const auto userPaths = scanner.getUserScanPaths();

    juce::PopupMenu menu;
    menu.addItem (1, scanner.isScanning() ? "Scanning..." : "Scan now", ! scanner.isScanning());
    menu.addSeparator();
    menu.addItem (2, "Add folder to scan...");

    if (! userPaths.isEmpty())
    {
        juce::PopupMenu removeMenu;
        for (int i = 0; i < userPaths.size(); ++i)
            removeMenu.addItem (1000 + i, userPaths[i]);

        menu.addSubMenu ("Remove scan folder", removeMenu);
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scanButton),
        [this, userPaths] (int result)
        {
            if (result == 1)
            {
                scanner.startScan();
                updateScanButton();
            }
            else if (result == 2)
            {
                addScanFolder();
            }
            else if (result >= 1000 && result - 1000 < userPaths.size())
            {
                scanner.removeUserScanPath (userPaths[result - 1000]);
            }
        });
}

void MainComponent::addScanFolder()
{
    scanFolderChooser = std::make_unique<juce::FileChooser> (
        "Choose a folder to scan for VST3 plugins", juce::File(), juce::String());

    scanFolderChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this] (const juce::FileChooser& fc)
        {
            const auto folder = fc.getResult();

            if (folder.isDirectory())
            {
                scanner.addUserScanPath (folder);
                scanner.startScan();   // pick up the new folder right away
                updateScanButton();
            }
        });
}

void MainComponent::showHelp()
{
    auto doc = std::make_unique<juce::TextEditor>();
    doc->setMultiLine (true);
    doc->setReadOnly (true);
    doc->setCaretVisible (false);
    doc->setScrollbarsShown (true);
    doc->setColour (juce::TextEditor::backgroundColourId, panelBackground);
    doc->setColour (juce::TextEditor::outlineColourId, gridLine);
    doc->setColour (juce::TextEditor::focusedOutlineColourId, gridLine);
    doc->setColour (juce::TextEditor::textColourId, textNormal);
    doc->setFont (juce::Font (juce::FontOptions (14.0f)));
    doc->setText (
        "PLUGIN PLAY — HELP\n"
        "\n"
        "Plugin Play hosts VST3 effects between your DJ software and your\n"
        "speakers, so you can add reverb, EQ, filters and more to the whole mix\n"
        "in real time.\n"
        "\n"
        "GETTING STARTED\n"
        "\n"
        "1.  VIRTUAL CABLE — click this to install and configure a virtual audio\n"
        "    cable. Your DJ software outputs to the cable, and Plugin Play reads\n"
        "    the cable as its input.\n"
        "\n"
        "2.  AUDIO SETTINGS — choose your INPUT (the cable) and OUTPUT (your\n"
        "    speakers or interface). Expand the panel for the channel pairs, the\n"
        "    driver type, and the sample rate and buffer size. 'Auto' matches the\n"
        "    source rate to avoid an extra resample; a smaller buffer means lower\n"
        "    latency but less stability. TEST plays a tone through your output.\n"
        "\n"
        "3.  SCAN PLUGINS — finds the VST3 effects installed on your system. Run\n"
        "    this once (and again after installing new plugins).\n"
        "\n"
        "BUILDING A CHAIN\n"
        "\n"
        "  •  Add Plugin — appends an effect to the end of the chain. Audio flows\n"
        "     top to bottom through the chain.\n"
        "  •  Drag a card by its grip dots to reorder effects.\n"
        "  •  ON / OFF — bypass a single effect.\n"
        "  •  OPEN — open the plugin's own editor (or double-click the card).\n"
        "  •  FLOAT — a toggle that pins an open editor on top of other windows so\n"
        "     it stays visible over your DJ software. It does not open the editor.\n"
        "  •  ×  — remove the effect from the chain. Press Ctrl+Z to undo a\n"
        "     removal (the plugin comes back with its settings and position).\n"
        "  •  FX ON / OFF (top of the meters) — master bypass for every effect,\n"
        "     crossfaded so it won't click.\n"
        "\n"
        "LIMITER\n"
        "\n"
        "  A brickwall safety limiter sits on the output (on by default). It stops\n"
        "  a misbehaving plugin from sending a runaway level to your speakers.\n"
        "  Leave it on unless you have your own limiter at the end of the chain.\n"
        "\n"
        "SCAN PLUGINS\n"
        "\n"
        "  Click SCAN for a menu: scan now, or add/remove extra folders to search\n"
        "  if you keep VST3s outside the standard locations.\n"
        "\n"
        "PRESETS\n"
        "\n"
        "  Save the current chain and reload it later from the PRESETS menu.\n",
        false);
    doc->setSize (560, 540);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (doc.release());
    options.dialogTitle = "Plugin Play — Help";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

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
                const auto file = presetFiles[result - 1000];
                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::QuestionIcon, "Delete preset",
                    "Delete the preset \"" + file.getFileNameWithoutExtension() + "\"?",
                    "Delete", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([file] (int r)
                    {
                        if (r == 1)
                            file.deleteFile();
                    }));
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

            if (file.existsAsFile())
            {
                juce::AlertWindow::showOkCancelBox (
                    juce::MessageBoxIconType::QuestionIcon, "Overwrite preset",
                    "A preset named \"" + name + "\" already exists. Overwrite it?",
                    "Overwrite", "Cancel", nullptr,
                    juce::ModalCallbackFunction::create ([this, file] (int r)
                    {
                        if (r == 1)
                            savePresetTo (file);
                    }));
            }
            else
            {
                savePresetTo (file);
            }
        }),
        true);   // delete the window when dismissed
}

void MainComponent::savePresetTo (const juce::File& file)
{
    if (! engine.savePreset (file))
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon, "Couldn't save preset",
            "The preset file couldn't be written:\n" + file.getFullPathName());
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

    refreshDeviceTypes();
    refreshChannelSelectors();
    refreshSampleRates();
    refreshBufferSizes();
    checkSampleRate();
}

void MainComponent::refreshDeviceTypes()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    deviceTypeSelector.clear (juce::dontSendNotification);

    const auto current = engine.deviceManager.getCurrentAudioDeviceType();

    int id = 1;
    for (auto* type : engine.deviceManager.getAvailableDeviceTypes())
    {
        deviceTypeSelector.addItem (type->getTypeName(), id);
        if (type->getTypeName() == current)
            deviceTypeSelector.setSelectedId (id, juce::dontSendNotification);
        ++id;
    }

    deviceTypeSelector.setEnabled (deviceTypeSelector.getNumItems() > 1);
}

void MainComponent::refreshChannelSelectors()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    auto* device = engine.deviceManager.getCurrentAudioDevice();

    // Each item is a stereo pair of the device's channels ("1+2", "3+4", ...),
    // with the item id encoding the pair's first channel index.
    auto fill = [device] (juce::ComboBox& box, bool wantInputs)
    {
        box.clear (juce::dontSendNotification);

        if (device == nullptr)
            return;

        const auto numChannels = wantInputs ? device->getInputChannelNames() .size()
                                            : device->getOutputChannelNames().size();
        const auto active      = wantInputs ? device->getActiveInputChannels()
                                            : device->getActiveOutputChannels();

        for (int start = 0; start < numChannels; start += 2)
        {
            auto text = juce::String (start + 1);
            if (start + 1 < numChannels)
                text << "+" << start + 2;

            box.addItem (text, start / 2 + 1);
        }

        const auto firstActive = active.findNextSetBit (0);
        box.setSelectedId (firstActive >= 0 ? firstActive / 2 + 1 : 1,
                           juce::dontSendNotification);

        box.setEnabled (box.getNumItems() > 1);
    };

    fill (inputChannelSelector,  true);
    fill (outputChannelSelector, false);
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

void MainComponent::refreshBufferSizes()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    bufferSizeSelector.clear (juce::dontSendNotification);

    auto* device = engine.deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    const auto current = device->getCurrentBufferSizeSamples();
    const auto rate    = device->getCurrentSampleRate();

    // The id is the buffer size in frames, so it maps straight back on change.
    for (auto frames : device->getAvailableBufferSizes())
    {
        juce::String text (frames);
        text << " smp";
        if (rate > 0.0)
            text << "  (" << juce::String (1000.0 * frames / rate, 1) << " ms)";

        bufferSizeSelector.addItem (text, frames);
    }

    bufferSizeSelector.setSelectedId (current, juce::dontSendNotification);
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

void MainComponent::applyChannelSelection (bool isInput)
{
    auto* device = engine.deviceManager.getCurrentAudioDevice();
    if (device == nullptr)
        return;

    auto& box = isInput ? inputChannelSelector : outputChannelSelector;
    const auto start = 2 * (box.getSelectedId() - 1);
    if (start < 0)
        return;

    const auto numChannels = isInput ? device->getInputChannelNames() .size()
                                     : device->getOutputChannelNames().size();

    juce::BigInteger channels;
    channels.setBit (start);
    if (start + 1 < numChannels)
        channels.setBit (start + 1);

    auto setup = engine.deviceManager.getAudioDeviceSetup();

    if (isInput)
    {
        setup.useDefaultInputChannels = false;
        setup.inputChannels = channels;
    }
    else
    {
        setup.useDefaultOutputChannels = false;
        setup.outputChannels = channels;
    }

    auto error = engine.deviceManager.setAudioDeviceSetup (setup, true);

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Couldn't change channels", error);
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

void MainComponent::applyBufferSize (int frames)
{
    if (frames <= 0)
        return;

    auto* device = engine.deviceManager.getCurrentAudioDevice();
    if (device == nullptr || device->getCurrentBufferSizeSamples() == frames)
        return;

    auto setup = engine.deviceManager.getAudioDeviceSetup();
    setup.bufferSize = frames;
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

    // No warning to show: the source rate is unknown (e.g. non-WASAPI input) or
    // we're already matched to it.
    if (source <= 0.0 || std::abs (current - source) < 1.0)
    {
        rateHint.setText ({}, juce::dontSendNotification);
        return;
    }

    // Mismatch: source and Plugin Play disagree, so audio is being resampled.
    if (autoRate && std::abs (source - lastAutoAppliedRate) > 1.0)
    {
        lastAutoAppliedRate = source;   // guard against a re-apply loop if it can't switch
        applySampleRate (source);
        rateHint.setText ({}, juce::dontSendNotification);
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
        auto window = std::make_unique<PluginWindow> (
            *processor, slot.description.name,
            [this, nodeID] { closePluginWindow (nodeID); });

        // Honour the FLOAT toggle if it was set while the editor was closed.
        if (isSlotFloating (slotIndex))
            window->setAlwaysOnTop (true);

        pluginWindows[key] = std::move (window);
    }
}

void MainComponent::setPluginFloating (int slotIndex, bool shouldFloat)
{
    if (! juce::isPositiveAndBelow (slotIndex, engine.getNumPlugins()))
        return;

    const auto key = engine.getSlot (slotIndex).nodeID.uid;
    floatingSlots[key] = shouldFloat;

    // Apply immediately if the editor is open; this never opens a closed editor.
    if (auto existing = pluginWindows.find (key); existing != pluginWindows.end())
        existing->second->setAlwaysOnTop (shouldFloat);
}

bool MainComponent::isSlotFloating (int slotIndex) const
{
    if (! juce::isPositiveAndBelow (slotIndex, engine.getNumPlugins()))
        return false;

    const auto it = floatingSlots.find (engine.getSlot (slotIndex).nodeID.uid);
    return it != floatingSlots.end() && it->second;
}

void MainComponent::closePluginWindow (juce::AudioProcessorGraph::NodeID nodeID)
{
    pluginWindows.erase (nodeID.uid);
}

//==============================================================================
void MainComponent::setAudioExpanded (bool shouldExpand)
{
    if (audioExpanded == shouldExpand)
        return;

    audioExpanded = shouldExpand;
    uiPrefs->setValue ("audioExpanded", audioExpanded);

    juce::Component* expandedControls[] { &inputChannelSelector, &outputChannelSelector,
                                          &driverLabel, &deviceTypeSelector,
                                          &rateLabel, &bufferLabel,
                                          &sampleRateSelector, &bufferSizeSelector,
                                          &testButton, &rateHint };
    for (auto* c : expandedControls)
        c->setVisible (audioExpanded);

    updateAudioToggle();
    resized();   // grows/shrinks the device area and reflows everything below it
    repaint();   // the panel/toolbar backgrounds are painted, so redraw them too
}

void MainComponent::updateAudioToggle()
{
    // "+" to expand, "−" to collapse.
    const juce::String sign = audioExpanded ? juce::String::fromUTF8 ("\xe2\x88\x92")   // minus sign
                                            : "+";
    audioToggleButton.setButtonText (sign + "  AUDIO SETTINGS");
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

void MainComponent::updateLimiterButton()
{
    const auto on = engine.isLimiterEnabled();

    limiterButton.setButtonText (on ? "LIMITER" : "LIMITER OFF");
    limiterButton.setToggleState (on, juce::dontSendNotification);
    limiterButton.setColour (juce::TextButton::textColourOffId, on ? gridText : metricWarn);
}

void MainComponent::updateStatusText()
{
    // A running input with no signal is the most common setup mistake — surface it
    // ahead of the normal device readout.
    if (noInputSignal && ! scanner.isScanning())
    {
        statusLabel.setColour (juce::Label::textColourId, metricWarn);
        statusLabel.setText ("No input signal — check that your DJ software outputs to "
                             + engine.deviceManager.getAudioDeviceSetup().inputDeviceName,
                             juce::dontSendNotification);
        return;
    }

    statusLabel.setColour (juce::Label::textColourId, gridText);

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
