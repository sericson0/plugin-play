#include "MainComponent.h"
#include "PluginPicker.h"
#include "InputSourcePicker.h"
#include "../Audio/WasapiEndpoints.h"
#include "BinaryData.h"

namespace play
{

using namespace play::Colours;

namespace
{
//==============================================================================
/** Content for the Help dialog: the Plugin Play logo as a banner, a row of
    tab buttons, and a scrolling, read-only text panel whose contents change
    with the selected tab. */
class HelpContent : public juce::Component
{
public:
    HelpContent()
    {
        logo = juce::ImageCache::getFromMemory (BinaryData::full_logo_png,
                                                BinaryData::full_logo_pngSize);

        // The source art sits on a wide black field with tall black margins above
        // and below the artwork; trim most of that vertical black so the logo
        // fills the banner instead of shrinking into a narrow strip.
        if (logo.isValid())
        {
            const int y = juce::roundToInt (logo.getHeight() * 0.26f);
            const int h = juce::roundToInt (logo.getHeight() * 0.50f);
            logo = logo.getClippedImage ({ 0, y, logo.getWidth(), h });
        }

        const char* names[numTabs] = { "Overview", "Virtual Cable", "Audio Settings",
                                       "Plugins", "Troubleshooting" };
        for (int i = 0; i < numTabs; ++i)
        {
            auto& b = tabButtons[i];
            b.setButtonText (names[i]);
            b.setClickingTogglesState (false);
            b.setColour (juce::TextButton::buttonColourId,   buttonBg);
            b.setColour (juce::TextButton::buttonOnColourId, buttonSelected);
            b.setColour (juce::TextButton::textColourOffId,  textNormal);
            b.setColour (juce::TextButton::textColourOnId,   textBright);
            b.onClick = [this, i] { selectTab (i); };
            addAndMakeVisible (b);
        }

        doc.setMultiLine (true);
        doc.setReadOnly (true);
        doc.setCaretVisible (false);
        doc.setScrollbarsShown (true);
        doc.setWantsKeyboardFocus (false);
        doc.setColour (juce::TextEditor::backgroundColourId, panelBackground);
        doc.setColour (juce::TextEditor::outlineColourId, gridLine);
        doc.setColour (juce::TextEditor::focusedOutlineColourId, gridLine);
        addAndMakeVisible (doc);

        setWantsKeyboardFocus (true);
        setSize (820, bannerHeight + tabRowHeight + 380);
        selectTab (0);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::tabKey)
        {
            const int next = key.getModifiers().isShiftDown()
                                 ? (selectedTab + numTabs - 1) % numTabs
                                 : (selectedTab + 1) % numTabs;
            selectTab (next);
            return true;
        }
        return false;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (background);

        // Keep the banner black to match the logo art's own black field.
        auto banner = getLocalBounds().removeFromTop (bannerHeight);
        g.setColour (juce::Colours::black);
        g.fillRect (banner);
        g.setColour (gridLine);
        g.fillRect (banner.getX(), banner.getBottom() - 1, banner.getWidth(), 1);

        if (logo.isValid())
            g.drawImageWithin (logo, banner.getX(), banner.getY(),
                               banner.getWidth(), banner.getHeight(),
                               juce::RectanglePlacement::centred, false);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        area.removeFromTop (bannerHeight);

        auto tabRow = area.removeFromTop (tabRowHeight).reduced (2, 4);
        const int tabW = tabRow.getWidth() / numTabs;
        for (int i = 0; i < numTabs; ++i)
            tabButtons[i].setBounds (tabRow.removeFromLeft (
                i < numTabs - 1 ? tabW : tabRow.getWidth()).reduced (2, 0));

        doc.setBounds (area.reduced (2));
    }

private:
    void selectTab (int index)
    {
        selectedTab = index;
        for (int i = 0; i < numTabs; ++i)
            tabButtons[i].setToggleState (i == index, juce::dontSendNotification);
        populateContent (index);
    }

    void addHeading (const juce::String& text)
    {
        doc.setColour (juce::TextEditor::textColourId, accentBright);
        doc.setFont (juce::Font (juce::FontOptions (15.0f)).boldened());
        doc.insertTextAtCaret (text + "\n");
    }

    void addBody (const juce::String& text)
    {
        doc.setColour (juce::TextEditor::textColourId, textNormal);
        doc.setFont (juce::Font (juce::FontOptions (14.0f)));
        doc.insertTextAtCaret (text + "\n\n");
    }

    void populateContent (int tabIndex)
    {
        doc.clear();

        switch (tabIndex)
        {
            case 0: populateOverview();        break;
            case 1: populateVirtualCable();    break;
            case 2: populateAudioSettings();   break;
            case 3: populatePlugins();         break;
            case 4: populateTroubleshooting(); break;
        }

        doc.setCaretPosition (0);
    }

    void populateOverview()
    {
        addHeading ("PLUGIN PLAY");
        addBody ("Plugin Play hosts VST3 effects between your DJ software and your "
                 "speakers, so you can add reverb, EQ, filters and more to the whole "
                 "mix in real time.");

        addHeading ("HOW IT WORKS");
        addBody ("Your DJ software's audio is routed into Plugin Play, passes through "
                 "the chain of effects you build, and comes out of your chosen output "
                 "device. Nothing is added to your tracks on disk - it is all live.");

        addHeading ("GETTING STARTED");
        addBody ("1.  VIRTUAL CABLE - set up the audio route from your DJ software\n"
                 "     into Plugin Play. See the Virtual Cable tab.\n"
                 "2.  AUDIO SETTINGS - choose your input and output devices. See the\n"
                 "     Audio Settings tab.\n"
                 "3.  SCAN PLUGINS - find the VST3 effects on your system, then add\n"
                 "     them to the chain. See the Plugins tab.");

        addHeading ("TIPS");
        addBody ("- Hover over any control for a tooltip that explains it.\n"
                 "- Save a chain you like as a preset from the PRESETS menu.\n"
                 "- The LIMITER guards your speakers - leave it on unless you\n"
                 "   already have your own limiter at the end of the chain.");

        addHeading ("CONTACT");
        addBody ("For questions, suggestions or bug reports, email "
                 "TangoToolkit@gmail.com.");
    }

    void populateVirtualCable()
    {
        addHeading ("WHAT IT'S FOR");
        addBody ("Plugin Play needs your DJ software's audio as an input. A virtual "
                 "audio cable is a software 'wire' that carries sound between apps: "
                 "your DJ software plays into the cable, and Plugin Play reads the "
                 "cable as its input.");

        addHeading ("SETTING IT UP");
        addBody ("1.  Click VIRTUAL CABLE in the header. Plugin Play checks\n"
                 "     whether a cable is already installed.\n"
                 "2.  If none is found, it walks you through installing VB-CABLE:\n"
                 "     download it, run the installer as admin, then reboot.\n"
                 "3.  After rebooting, open VIRTUAL CABLE again and Re-check.\n"
                 "     Once a cable is detected, the routing steps appear.");

        addHeading ("ROUTING YOUR DJ SOFTWARE");
        addBody ("In your DJ software, set the master / output device to the cable "
                 "(e.g. 'CABLE Input'). Then in Plugin Play's Audio Settings, set "
                 "INPUT to the matching 'CABLE Output' and OUTPUT to your speakers "
                 "or interface.");

        addHeading ("WITHOUT A VIRTUAL CABLE");
        addBody ("You don't need VB-CABLE if you already have another way to feed "
                 "your DJ audio back as an input. If your audio interface offers "
                 "hardware loopback, or your DJ software can output to a device you "
                 "can also select as an input, set Plugin Play's INPUT to that "
                 "source and skip the install. A virtual cable is simply the easiest "
                 "route if you don't already have one.");

        addHeading ("NOTES");
        addBody ("- VB-CABLE is a free download from vb-audio.com. Plugin Play\n"
                 "   fetches the latest version and opens its installer for you.\n"
                 "- Don't set your DJ software's output volume too low, or the\n"
                 "   captured signal will be quiet and noisy.");
    }

    void populateAudioSettings()
    {
        addHeading ("INPUT & OUTPUT");
        addBody ("The device bar below the meters has INPUT and OUTPUT selectors. "
                 "Set INPUT to your virtual cable (the sound coming from your DJ "
                 "software) and OUTPUT to your speakers or audio interface.");

        addHeading ("EXPANDING THE PANEL");
        addBody ("Use the expand toggle to reveal the advanced controls:\n"
                 "- INPUT / OUTPUT PAIR - which channels of a multi-channel device.\n"
                 "- DRIVER - the audio driver type (e.g. Windows Audio or ASIO).\n"
                 "- RATE - the sample rate.\n"
                 "- BUFFER - the block size.");

        addHeading ("SAMPLE RATE & AUTO-MATCH");
        addBody ("'Auto - match source' sets the rate to match your input so the "
                 "audio isn't resampled on the way through. If the rate doesn't match "
                 "the source, a hint warns you that audio is being resampled. Matching "
                 "rates end to end gives the cleanest, lowest-latency path.");

        addHeading ("BUFFER SIZE");
        addBody ("A smaller buffer means lower latency but needs more CPU and can "
                 "crackle if the machine can't keep up. A larger buffer is more "
                 "stable. Raise it if you hear dropouts.");

        addHeading ("TEST OUTPUT");
        addBody ("TEST plays a short tone through the current output device so you "
                 "can confirm it reaches your speakers before you start.");
    }

    void populatePlugins()
    {
        addHeading ("SCANNING FOR PLUGINS");
        addBody ("Click SCAN PLUGINS to find the VST3 effects installed on your "
                 "system. Run it once, and again after installing new plugins. The "
                 "menu also lets you add or remove extra folders to search if you "
                 "keep VST3s outside the standard locations.");

        addHeading ("BUILDING A CHAIN");
        addBody ("+ Add Plugin appends an effect to the end of the chain. Audio "
                 "flows top to bottom, so the order of the cards is the order of "
                 "processing. Drag a card by its grip dots to reorder effects.");

        addHeading ("PER-EFFECT CONTROLS");
        addBody ("- ON / OFF - bypass a single effect.\n"
                 "- OPEN - open the plugin's own editor (or double-click the card).\n"
                 "- FLOAT - pin an open editor on top of other windows so it stays\n"
                 "   visible over your DJ software. It does not open the editor.\n"
                 "- X - remove the effect. Press Ctrl+Z to undo a removal (the\n"
                 "   plugin returns with its settings and position).");

        addHeading ("MASTER CONTROLS");
        addBody ("- FX ON / OFF (above the meters) - master bypass for every\n"
                 "   effect, crossfaded so it won't click.\n"
                 "- LIMITER - a brickwall safety limiter on the output, on by\n"
                 "   default. It stops a misbehaving plugin from sending a runaway\n"
                 "   level to your speakers. Leave it on unless you have your own\n"
                 "   limiter at the end.");

        addHeading ("PRESETS");
        addBody ("Save the current chain and reload it later from the PRESETS menu.");
    }

    void populateTroubleshooting()
    {
        addHeading ("NO SOUND AT ALL");
        addBody ("- Check OUTPUT is set to the right device and press TEST OUTPUT.\n"
                 "- Check FX ON is showing (not OFF) and the master isn't bypassed.\n"
                 "- Make sure your DJ software is actually playing.");

        addHeading ("NO INPUT / METERS NOT MOVING");
        addBody ("- Confirm your DJ software's output is set to the virtual cable.\n"
                 "- Confirm Plugin Play's INPUT is the matching cable output.\n"
                 "- Make sure the DJ software's output volume isn't turned down\n"
                 "   too low.");

        addHeading ("A PLUGIN DOESN'T APPEAR");
        addBody ("- Run SCAN PLUGINS again; use the menu to add its folder if it\n"
                 "   lives outside the standard VST3 locations.\n"
                 "- Only VST3 effect plugins are supported.");

        addHeading ("CRACKLES OR DROPOUTS");
        addBody ("- Raise the BUFFER size in the expanded Audio Settings.\n"
                 "- Match the sample rate to the source (Auto - match source) so\n"
                 "   the audio isn't being resampled.\n"
                 "- Bypass heavy effects to find the one overloading the CPU.");

        addHeading ("AUDIO SOUNDS RESAMPLED");
        addBody ("If the rate hint warns about resampling, enable 'Auto - match "
                 "source' or set RATE to match your DJ software's sample rate.");

        addHeading ("STILL STUCK?");
        addBody ("Email TangoToolkit@gmail.com describing your setup and what "
                 "you're seeing.");
    }

    static constexpr int numTabs      = 5;
    static constexpr int bannerHeight = 150;
    static constexpr int tabRowHeight = 34;

    juce::Image logo;
    juce::TextButton tabButtons[numTabs];
    juce::TextEditor doc;
    int selectedTab = 0;
};
} // namespace

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

    clipGlowPhase += 0.3f;
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
    auto lampArea = area.removeFromRight (10);
    auto lamp = lampArea.reduced (0, 6).toFloat();

    if (clipped)
    {
        // Soft pulsing glow behind the lamp so a clip is unmissable peripherally.
        const float pulse = 0.5f + 0.5f * std::sin (clipGlowPhase);
        auto glowCentre = lamp.getCentre();
        for (int ring = 3; ring >= 1; --ring)
        {
            const float r = (float) ring * 5.0f;
            g.setColour (metricBad.withAlpha (0.10f + 0.10f * pulse));
            g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (glowCentre));
        }
    }

    g.setColour (clipped ? metricBad : sliderTrack);
    g.fillRoundedRectangle (lamp, 2.0f);
    area.removeFromRight (4);

    auto proportionFor = [] (float level)
    {
        const auto dB = juce::Decibels::gainToDecibels (level, -60.0f);
        return juce::jlimit (0.0f, 1.0f, juce::jmap (dB, -60.0f, 0.0f, 0.0f, 1.0f));
    };

    // Zone boundaries as bar proportions: green up to -12 dB, amber to -3 dB, red above.
    const float amberStart = proportionFor (juce::Decibels::decibelsToGain (-12.0f));
    const float redStart   = proportionFor (juce::Decibels::decibelsToGain (-3.0f));

    auto barArea = area.reduced (0, 4);
    const int barHeight = (barArea.getHeight() - 3) / 2;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = ch == 0 ? barArea.removeFromTop (barHeight)
                           : barArea.removeFromBottom (barHeight);
        auto barF = bar.toFloat();

        g.setColour (sliderTrack);
        g.fillRoundedRectangle (barF, 2.0f);

        // Faint gain-staging ticks at the -12 and -3 dB zone edges.
        g.setColour (gridLine.brighter (0.4f));
        for (float tick : { amberStart, redStart })
        {
            const float x = barF.getX() + barF.getWidth() * tick;
            g.fillRect (x, barF.getY(), 1.0f, barF.getHeight());
        }

        const auto proportion = proportionFor (display[ch]);

        if (proportion > 0.001f)
        {
            // Clip the coloured zones to the rounded fill so only the lit portion
            // of each band shows, with green/amber/red segments in place.
            auto fill = barF.withWidth (barF.getWidth() * proportion);
            juce::Path fillPath;
            fillPath.addRoundedRectangle (fill, 2.0f);

            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (fillPath);

            const float amberX = barF.getX() + barF.getWidth() * amberStart;
            const float redX   = barF.getX() + barF.getWidth() * redStart;

            g.setColour (metricGood);
            g.fillRect (barF.getX(), barF.getY(), amberX - barF.getX(), barF.getHeight());
            g.setColour (metricWarn);
            g.fillRect (amberX, barF.getY(), redX - amberX, barF.getHeight());
            g.setColour (metricBad);
            g.fillRect (redX, barF.getY(), barF.getRight() - redX, barF.getHeight());
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
    sourceButton.onClick    = [this] { InputSourcePicker::launch (engine); };
    cableButton.onClick     = [this] { CableSetupComponent::launch (engine.deviceManager); };
    helpButton.onClick      = [this] { showHelp(); };
    presetsButton.onClick   = [this] { showPresetsMenu(); };
    killButton.onClick      = [this] { engine.setMasterBypass (! engine.isMasterBypassed()); };
    killButton.setColour (juce::TextButton::textColourOffId, accentBright);
    killButton.setColour (juce::TextButton::buttonOnColourId, metricBad.darker (0.5f));

    // Styled to match the FX ON kill switch it sits beside on the meter row:
    // gray background with orange text in the normal (limiter on) state, with the
    // toggled/coloured background reserved for the alert (limiter off) state.
    limiterButton.onClick   = [this] { engine.setLimiterEnabled (! engine.isLimiterEnabled()); updateLimiterButton(); };
    limiterButton.setColour (juce::TextButton::textColourOffId, accentBright);
    limiterButton.setColour (juce::TextButton::buttonOnColourId, metricBad.darker (0.5f));

    // "Add Plugin" is the primary action but should read like the other toolbar
    // buttons rather than a filled orange call-to-action.
    addPluginButton.onClick = [this] { showAddPluginMenu (addPluginButton.getScreenPosition()); };

    scanButton    .setTooltip ("Scan for installed VST3 plugins, or manage extra scan folders");
    sourceButton  .setTooltip ("Choose the input: an audio device, or capture a running app directly (no install)");
    cableButton   .setTooltip ("Set up the virtual audio cable that captures your DJ software");
    helpButton    .setTooltip ("Open the Plugin Play help & documentation");
    presetsButton .setTooltip ("Save the current chain, or load a saved one");
    killButton    .setTooltip ("Master bypass - turn every effect on or off at once");
    limiterButton .setTooltip ("Brickwall safety limiter on the output - protects against runaway plugin levels");
    addPluginButton.setTooltip ("Add a plugin to the end of the effect chain");

    addAndMakeVisible (scanButton);
    addAndMakeVisible (sourceButton);
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

    for (auto* l : { &inputLabel, &outputLabel, &inputChannelLabel, &outputChannelLabel,
                     &driverLabel, &rateLabel, &bufferLabel })
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

    inputSelector       .setTooltip ("Audio input device - the capture source (usually the virtual cable)");
    outputSelector      .setTooltip ("Audio output device - where processed audio is sent");
    inputChannelSelector .setTooltip ("Which channel pair of the input device to capture");
    outputChannelSelector.setTooltip ("Which channel pair of the output device to play to");
    deviceTypeSelector  .setTooltip ("Audio driver type - ASIO gives the lowest latency if available");
    sampleRateSelector  .setTooltip ("Sample rate - 'Auto' follows your source to avoid an extra resample");
    bufferSizeSelector  .setTooltip ("Buffer size - smaller means lower latency, larger means more stable");
    testButton          .setTooltip ("Play a short test tone through the current output device");

    testButton.setColour (juce::TextButton::textColourOffId, accentBright);
    testButton.onClick = [this] { engine.deviceManager.playTestSound(); };

    autoRate      = uiPrefs->getBoolValue ("autoRate", true);
    audioExpanded = uiPrefs->getBoolValue ("audioExpanded", true);

    // Left with the default button colours so it reads like the other toolbar buttons.
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
    addAndMakeVisible (inputChannelLabel);
    addAndMakeVisible (outputChannelLabel);
    addAndMakeVisible (inputChannelSelector);
    addAndMakeVisible (outputChannelSelector);
    addAndMakeVisible (deviceTypeSelector);
    addAndMakeVisible (sampleRateSelector);
    addAndMakeVisible (bufferSizeSelector);
    addAndMakeVisible (testButton);
    addAndMakeVisible (rateHint);

    // Input/output device selection is always visible; the channel, driver,
    // rate and buffer controls only show while the audio area is expanded.
    juce::Component* expandedControls[] { &inputChannelLabel, &outputChannelLabel,
                                          &inputChannelSelector, &outputChannelSelector,
                                          &driverLabel, &deviceTypeSelector,
                                          &rateLabel, &bufferLabel,
                                          &sampleRateSelector, &bufferSizeSelector,
                                          &testButton, &rateHint };
    for (auto* c : expandedControls)
        c->setVisible (audioExpanded);

    chainView.onOpenEditor  = [this] (int index) { openPluginEditor (index); };
    chainView.onFloatEditor = [this] (int index, bool shouldFloat) { setPluginFloating (index, shouldFloat); };
    chainView.isFloating    = [this] (int index) { return isSlotFloating (index); };
    chainView.onAddPlugin   = [this] { showAddPluginMenu (addPluginButton.getScreenPosition()); };

    viewport.setViewedComponent (&chainView, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, gridText);
    statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    addAndMakeVisible (statusLabel);

    cpuLabel.setJustificationType (juce::Justification::centredRight);
    cpuLabel.setColour (juce::Label::textColourId, gridText);
    cpuLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    cpuLabel.setTooltip ("Audio processing load - amber over 60%, red over 85%");
    addAndMakeVisible (cpuLabel);

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

    // App logo, then the wordmark beside it. The source icon carries tall black
    // margins above and below the artwork, so trim them (once) to a tighter band
    // that fills the header height instead of floating in a black square.
    int textX = 16;
    static const auto logo = [
    ]
    {
        auto img = juce::ImageCache::getFromMemory (BinaryData::exe_logo_transparent_png,
                                                    BinaryData::exe_logo_transparent_pngSize);
        if (img.isValid())
        {
            const int y = juce::roundToInt (img.getHeight() * 0.20f);
            const int h = juce::roundToInt (img.getHeight() * 0.60f);
            img = img.getClippedImage ({ 0, y, img.getWidth(), h });
        }
        return img;
    }();
    if (logo.isValid())
    {
        const int logoW = 46, logoH = 40;
        g.drawImageWithin (logo, 12, (56 - logoH) / 2, logoW, logoH,
                           juce::RectanglePlacement::centred, false);
        textX = 12 + logoW + 8;
    }

    const juce::Font wordmarkFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.setFont (wordmarkFont);

    // Measure "PLUGIN " so "PLAY" sits flush regardless of font/size changes.
    const int firstWidth = juce::GlyphArrangement::getStringWidthInt (wordmarkFont, "PLUGIN ");

    g.setColour (accent);
    g.drawText ("PLUGIN", textX, 0, firstWidth, 56, juce::Justification::centredLeft);
    g.setColour (textBright);
    g.drawText ("PLAY", textX + firstWidth, 0, 90, 56, juce::Justification::centredLeft);

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
    header.removeFromRight (8);
    sourceButton.setBounds (header.removeFromRight (118));

    // Meter row: IN meter | FX kill switch | OUT meter | LIMITER.
    auto meterRow = area.removeFromTop (40).reduced (16, 6);
    const int killWidth    = 84;
    const int limiterWidth = 104;   // wider — holds "LIMITER OFF"
    limiterButton.setBounds (meterRow.removeFromRight (limiterWidth));
    meterRow.removeFromRight (8);
    const int meterWidth = (meterRow.getWidth() - killWidth - 16) / 2;
    inputMeter.setBounds (meterRow.removeFromLeft (meterWidth));
    meterRow.removeFromLeft (8);
    killButton.setBounds (meterRow.removeFromLeft (killWidth));
    meterRow.removeFromLeft (8);
    outputMeter.setBounds (meterRow);

    // ── Device area (collapsible): input / output devices are always shown;
    //    expanding adds channel selection plus the driver / rate / buffer row.
    //    The expand/collapse toggle sits centred at the bottom. ─
    deviceBarBounds = area.removeFromTop (audioExpanded ? 216 : 94);
    auto bar = deviceBarBounds.reduced (16, 10);

    audioToggleButton.setBounds (bar.removeFromBottom (24).withSizeKeepingCentre (170, 24));
    bar.removeFromBottom (10);   // gap between the content and the toggle

    // Input | output device columns.
    auto labelRow = bar.removeFromTop (14);
    auto comboRow = bar.removeFromTop (26);
    const int barHalf = labelRow.getWidth() / 2;

    inputLabel .setBounds (labelRow.removeFromLeft (barHalf).withTrimmedRight (8));
    outputLabel.setBounds (labelRow);

    inputSelector .setBounds (comboRow.removeFromLeft (barHalf).withTrimmedRight (8));
    outputSelector.setBounds (comboRow);

    if (audioExpanded)
    {
        // Channel-pair selectors get their own row beneath the device combos.
        bar.removeFromTop (10);
        auto channelLabels = bar.removeFromTop (14);
        auto channelCombos = bar.removeFromTop (26);

        inputChannelLabel .setBounds (channelLabels.removeFromLeft (barHalf).withTrimmedRight (8));
        outputChannelLabel.setBounds (channelLabels);
        inputChannelSelector .setBounds (channelCombos.removeFromLeft (barHalf).withTrimmedRight (8));
        outputChannelSelector.setBounds (channelCombos);

        bar.removeFromTop (12);
        auto settingsLabels = bar.removeFromTop (14);
        auto settingsCombos = bar.removeFromTop (26);

        // TEST OUTPUT sits at the right end of the control row (plays a test tone).
        const int testWidth = 104;
        testButton.setBounds (settingsCombos.removeFromRight (testWidth));
        settingsCombos.removeFromRight (14);
        settingsLabels.removeFromRight (testWidth + 14);

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
    toolbar.removeFromLeft (12);
    toolbar.removeFromRight (12);
    addPluginButton.setBounds (toolbar);   // fills the middle - the primary action

    auto footer = area.removeFromBottom (26).reduced (16, 0);
    cpuLabel.setBounds (footer.removeFromRight (90));
    statusLabel.setBounds (footer);

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
    auto content = std::make_unique<HelpContent>();

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = "Plugin Play - Help";
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
    // The "source rate" only means something for the WASAPI shared-mode path: it's
    // the Windows mix-format rate of the input endpoint. Under ASIO (or exclusive
    // WASAPI) the device runs at its own rate, and the shared mix rate is unrelated
    // — consulting it there produces a bogus "being resampled" warning and, with
    // Auto on, a pointless device reopen chasing a rate that doesn't apply. So only
    // report it for shared Windows Audio input.
    const auto deviceType = engine.deviceManager.getCurrentAudioDeviceType();
    if (! deviceType.containsIgnoreCase ("Windows Audio")
          || deviceType.containsIgnoreCase ("Exclusive"))
        return 0.0;

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
                          + " - audio is being resampled.",
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
            // The user hit the window's own close button, so we're running inside
            // PluginWindow::closeButtonPressed (dispatched from the native WM_CLOSE
            // handler). Erasing the window here would delete it — and its peer —
            // mid-callback, a use-after-free on unwind. Defer to the next message
            // loop, guarded in case the whole component is torn down first.
            [safeThis = juce::Component::SafePointer<MainComponent> (this), nodeID]
            {
                juce::MessageManager::callAsync ([safeThis, nodeID]
                {
                    if (safeThis != nullptr)
                        safeThis->closePluginWindow (nodeID);
                });
            });

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

    juce::Component* expandedControls[] { &inputChannelLabel, &outputChannelLabel,
                                          &inputChannelSelector, &outputChannelSelector,
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
    killButton.setColour (juce::TextButton::textColourOffId, killed ? metricBad : accentBright);

    // All effects bypassed is a loud, safety-relevant state — pulse a red ring.
    killButton.setAlert (killed, metricBad);
}

void MainComponent::updateLimiterButton()
{
    const auto on = engine.isLimiterEnabled();

    limiterButton.setButtonText (on ? "LIMITER" : "LIMITER OFF");
    // Toggle on == the alert (limiter off) state, so the normal state keeps the
    // default gray background with orange text like the FX ON button.
    limiterButton.setToggleState (! on, juce::dontSendNotification);
    limiterButton.setColour (juce::TextButton::textColourOffId, on ? accentBright : metricBad);

    // The safety limiter being off is a safety-relevant state — flag it in the same
    // red as FX OFF.
    limiterButton.setAlert (! on, metricBad);
}

void MainComponent::updateStatusText()
{
    statusLabel.setColour (juce::Label::textColourId, gridText);

    juce::String text;
    juce::String cpuText;

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
             << "  |  out " << juce::String (latencyMs, 1) << " ms";

        // CPU load in its own label so it can be tinted before dropouts become audible.
        const auto cpu = engine.deviceManager.getCpuUsage() * 100.0;
        cpuText = "CPU " + juce::String (cpu, 0) + "%";
        cpuLabel.setColour (juce::Label::textColourId,
                            cpu > 85.0 ? metricBad : (cpu > 60.0 ? metricWarn : gridText));
    }
    else
    {
        text = "No audio device - open Audio Settings";
    }

    statusLabel.setText (text, juce::dontSendNotification);
    cpuLabel.setText (cpuText, juce::dontSendNotification);
}

} // namespace play
