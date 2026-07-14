#include "MainComponent.h"
#include "PluginPicker.h"
#include "WelcomePopup.h"
#include "SupportPanel.h"
#include "../Audio/WasapiEndpoints.h"
#include "../Setup/UpdateCheck.h"
#include "BinaryData.h"

namespace play
{

using namespace play::Colours;

namespace
{
//==============================================================================
/** Content for the Help dialog: the Plugin Play logo as a banner, a row of
    tab buttons (plus a GUIDE button that replays the first-run walkthrough),
    and a read-only text panel whose contents change with the selected tab. */
class HelpContent : public juce::Component
{
public:
    HelpContent (juce::PropertiesFile& uiPrefsToUse, juce::AudioDeviceManager& dm)
        : uiPrefs (uiPrefsToUse), deviceManager (dm)
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

        guideButton.setColour (juce::TextButton::textColourOffId, accentBright);
        guideButton.setTooltip ("Replay the step-by-step welcome walkthrough");
        guideButton.onClick = [this] { WelcomePopup::show (uiPrefs, deviceManager); };
        addAndMakeVisible (guideButton);

        // The cable setup lives here once the header button has given way to
        // CHECK UPDATES (i.e. once a cable is installed).
        cableSetupButton.setColour (juce::TextButton::textColourOffId, accentBright);
        cableSetupButton.setTooltip ("Check for - or install - the virtual audio cable");
        cableSetupButton.onClick = [this] { CableSetupComponent::launch (deviceManager); };
        addAndMakeVisible (cableSetupButton);

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
        cableSetupButton.setBounds (tabRow.removeFromRight (112).reduced (2, 0));
        guideButton.setBounds (tabRow.removeFromRight (78).reduced (2, 0));
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
        addHeading ("PLUGIN PLAY - Version " + juce::JUCEApplication::getInstance()->getApplicationVersion());
        addBody (
            "Plugin Play is a user-friendly host for VST3 effects. "
                 "Easily add EQ, dehissing, limiters and more. "
                 "It can connect to any audio source or DJ software.");

        addHeading ("GETTING STARTED");
        addBody ("1.  VIRTUAL CABLE - Plugin Play uses a virtual cable to connect to\n"
                 "     your audio player. See the Virtual Cable tab to install.\n"
                 "2.  INPUT & OUTPUT - pick your audio player as the input, and your\n"
                 "     speakers as the output. See Audio Settings. You can also set\n"
                 "     CABLE Output as your DJ software's output and Plugin Play's input.\n"
                 "3.  Add and rearrange plugins to build your effect chain. See the\n"
                 "     Plugins tab.");

        addHeading ("TIPS");
        addBody ("- Hover over any control for a tooltip that explains it.\n"
                 "- Save a chain you like as a preset from the PRESETS menu.\n"
                 "- Drag cards by their grip dots to rearrange plugins.\n"
                 "- Use a card's FLOAT button to keep its plugin window on top.\n"
                 "- The GUIDE button above replays the welcome walkthrough.");

        addHeading ("CONTACT & SUPPORT");
        addBody ("Questions, suggestions or bugs: TangoToolkit@gmail.com. Plugin Play "
                 "is free and open source - if it helps your sets, consider a tip via "
                 "SUPPORT in the header (Card / Apple Pay, Venmo or Zelle).");
    }

    void populateVirtualCable()
    {
        addHeading ("WHAT IT'S FOR");
        addBody ("A virtual cable is a software 'wire' that carries sound between apps:\n"
                 "your DJ software plays into the cable, and Plugin Play reads the cable as its input.");

        addHeading ("SETTING IT UP");
        addBody ("1.  Click VIRTUAL CABLE (in the header until a cable is installed,\n"
                 "     and always at the top of this window) - it checks for a cable.\n"
                 "2.  If none is found, it walks you through installing VB-CABLE\n"
                 "     (a free download): run the installer as admin, then reboot.\n"
                 "3.  After rebooting, open VIRTUAL CABLE again and Re-check.");

        addHeading ("ROUTING YOUR DJ SOFTWARE");
        addBody ("You can route in two ways. First, set your DJ software's master output to the cable (e.g. 'CABLE "
                 "Input'). Then in Plugin Play, set INPUT to the matching 'CABLE Output' and OUTPUT to your speakers or interface.\n"
                 "Second, you can select your audio player directly as the input in Plugin Play.");

        addHeading ("NOTE");
        addBody ("Don't set your DJ software's output volume too low - doing so can raise the noise floor.\n"
                 "Instead, adjust the volume with your computer's output volume, DAC or mixer.");
    }

    void populateAudioSettings()
    {
        addHeading ("INPUT & OUTPUT");
        addBody ("The device bar below the meters has INPUT and OUTPUT selectors.\n"
                 "Set OUTPUT to your speakers or audio interface. Set INPUT to the "
                 "virtual cable, and set that cable as the output in your DJ software.");

        addHeading ("SENDING AN APP THROUGH PLUGIN PLAY");
        addBody ("The INPUT dropdown also lists running apps. Pick one (e.g. Spotify) "
                 "and Plugin Play routes it through the cable for you.\n"
                 "It returns the app's output to normal when you switch away or quit.");

        addHeading ("ADVANCED CONTROLS");
        addBody ("Expand the panel for INPUT / OUTPUT PAIR (which channels of a "
                 "multi-channel device), DRIVER (e.g. Windows Audio or ASIO), RATE "
                 "and BUFFER.");

        addHeading ("SAMPLE RATE & BUFFER SIZE");
        addBody ("'Auto - match source' follows your input's rate so audio isn't "
                 "resampled on the way through - a hint warns if the rates differ. "
                 "A smaller buffer means lower latency; raise it if you hear "
                 "crackles or dropouts.");

        addHeading ("TEST OUTPUT");
        addBody ("TEST plays a short tone through the current output device so you "
                 "can confirm it reaches your speakers before you start.");
    }

    void populatePlugins()
    {
        addHeading ("SCANNING FOR PLUGINS");
        addBody ("Your VST3 effects are scanned automatically on first launch. Run "
                 "SCAN PLUGINS again after installing new plugins, or use its menu "
                 "to add folders outside the standard VST3 locations.");

        addHeading ("BUILDING A CHAIN");
        addBody ("+ Add Plugin appends an effect. Audio flows top to bottom, so the "
                 "card order is the processing order - drag a card by its grip dots "
                 "to reorder.");

        addHeading ("PER-EFFECT CONTROLS");
        addBody ("- ON / OFF - bypass a single effect.\n"
                 "- OPEN - open the plugin's own editor (or double-click the card).\n"
                 "- FLOAT - pin an open editor on top of other windows so it stays\n"
                 "   visible over your DJ software. It does not open the editor.\n"
                 "- X - remove the effect; Ctrl+Z brings it back, settings intact.");

        addHeading ("MASTER CONTROLS & PRESETS");
        addBody ("FX ON / OFF above the meters bypasses every effect at once, "
                 "crossfaded so it won't click. LIMITER is a brickwall safety "
                 "limiter on the output - leave it on unless you have your own at "
                 "the end. Save and reload chains from the PRESETS menu.");
    }

    void populateTroubleshooting()
    {
        addHeading ("NO SOUND / METERS NOT MOVING");
        addBody ("- Check OUTPUT is set to the right device and press TEST OUTPUT.\n"
                 "- Make sure your DJ software is playing, and its output is set\n"
                 "   to the virtual cable.\n"
                 "- Confirm INPUT is the matching cable output, and the DJ\n"
                 "   software's output volume isn't turned down too low.");

        addHeading ("EFFECTS AREN'T CHANGING THE SOUND");
        addBody ("Check the master button reads FX ON, check the effect's own "
                 "ON / OFF button, and open the plugin - its own mix or bypass "
                 "may be the culprit.");

        addHeading ("A PLUGIN DOESN'T APPEAR");
        addBody ("Run SCAN PLUGINS again, using its menu to add the plugin's folder "
                 "if needed. Only VST3 effect plugins are supported.");

        addHeading ("CRACKLES, DROPOUTS OR RESAMPLING");
        addBody ("Raise the BUFFER size in Audio Settings, use 'Auto - match source' "
                 "so the audio isn't resampled, and bypass heavy effects to find one "
                 "overloading the CPU.");

        addHeading ("STILL STUCK?");
        addBody ("Email TangoToolkit@gmail.com describing your setup and what "
                 "you're seeing.");
    }

    static constexpr int numTabs      = 5;
    static constexpr int bannerHeight = 150;
    static constexpr int tabRowHeight = 34;

    juce::PropertiesFile& uiPrefs;
    juce::AudioDeviceManager& deviceManager;
    juce::Image logo;
    juce::TextButton tabButtons[numTabs];
    juce::TextButton guideButton { "GUIDE" };
    juce::TextButton cableSetupButton { "VIRTUAL CABLE" };
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

    // Zone boundaries as bar proportions: green up to -18 dB, amber to -6 dB, red
    // above — weighted so the hot amber/red zones take a bigger share of the bar.
    const float amberStart = proportionFor (juce::Decibels::decibelsToGain (-18.0f));
    const float redStart   = proportionFor (juce::Decibels::decibelsToGain (-6.0f));

    auto barArea = area.reduced (0, 4);
    const int barHeight = (barArea.getHeight() - 3) / 2;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = ch == 0 ? barArea.removeFromTop (barHeight)
                           : barArea.removeFromBottom (barHeight);
        auto barF = bar.toFloat();

        g.setColour (sliderTrack);
        g.fillRoundedRectangle (barF, 2.0f);

        // Faint gain-staging ticks at the -18 and -6 dB zone edges.
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
void RefreshButton::paintButton (juce::Graphics& g, bool isHighlighted, bool isDown)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (isDown ? sliderTrack : isHighlighted ? buttonBgHover : buttonBg);
    g.fillRoundedRectangle (bounds, 4.0f);

    // Two opposed arcs with arrowheads — the usual circular "rescan" glyph, drawn
    // by hand so it doesn't depend on the font shipping a refresh character.
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.27f;

    g.setColour (isHighlighted || isDown ? textBright : textNormal);

    for (float offset : { 0.0f, juce::MathConstants<float>::pi })
    {
        // Angles are radians clockwise from 12 o'clock (JUCE's arc convention);
        // the point at angle t is centre + radius * (sin t, -cos t).
        const float start = offset + 0.35f;
        const float end   = offset + juce::MathConstants<float>::pi - 0.55f;

        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
        g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Arrowhead at the arc's end, pointing along the direction of travel.
        const juce::Point<float> tip (centre.x + radius * std::sin (end),
                                      centre.y - radius * std::cos (end));
        const juce::Point<float> tangent (std::cos (end), std::sin (end));
        const juce::Point<float> normal (-tangent.y, tangent.x);

        const float length = radius * 0.8f, width = radius * 0.55f;

        juce::Path head;
        head.addTriangle (tip + tangent * length,
                          tip - tangent * (length * 0.2f) + normal * width,
                          tip - tangent * (length * 0.2f) - normal * width);
        g.fillPath (head);
    }
}

//==============================================================================
MainComponent::MainComponent (AudioEngine& engineToUse, PluginScanner& scannerToUse)
    : engine (engineToUse), scanner (scannerToUse)
{
    scanButton.onClick      = [this] { showScanMenu(); };
    cableButton.onClick     = [this] { CableSetupComponent::launch (engine.deviceManager); };
    updateButton.onClick    = [this] { checkForUpdates(); };
    supportButton.onClick   = [this] { SupportPanel::launch(); };
    helpButton.onClick      = [this] { showHelp(); };
    presetsButton.onClick   = [this] { showPresetsMenu(); };
    supportButton.setColour (juce::TextButton::textColourOffId, accentBright);
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
    cableButton   .setTooltip ("Set up the virtual audio cable that captures your DJ software");
    updateButton  .setTooltip ("Check GitHub for a newer version of Plugin Play");
    supportButton .setTooltip ("Support Plugin Play - Card / Apple Pay, Venmo or Zelle");
    helpButton    .setTooltip ("Open the Plugin Play help & documentation (and the GUIDE walkthrough)");
    presetsButton .setTooltip ("Save the current chain, or load a saved one");
    killButton    .setTooltip ("Master bypass - turn every effect on or off at once");
    limiterButton .setTooltip ("Brickwall safety limiter on the output - protects against runaway plugin levels");
    addPluginButton.setTooltip ("Add a plugin to the end of the effect chain");

    addAndMakeVisible (scanButton);
    addAndMakeVisible (cableButton);
    addChildComponent (updateButton);   // shares the cable button's slot; visibility
                                        // is decided by updateHeaderButtons()
    addAndMakeVisible (supportButton);
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
                     &monitorLabel, &monitorPairLabel, &driverLabel, &rateLabel, &bufferLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        l->setColour (juce::Label::textColourId, gridText);
        addAndMakeVisible (*l);
    }

    inputSelector .setTextWhenNoChoicesAvailable ("No inputs");
    inputSelector .setTextWhenNothingSelected ("None - choose a source");
    outputSelector.setTextWhenNoChoicesAvailable ("No outputs");
    inputSelector .onChange = [this] { if (! updatingSelectors) applyInputSelection(); };
    outputSelector.onChange = [this] { if (! updatingSelectors) applyDeviceSelection(); };

    rescanButton.setTooltip ("Re-scan the input and output lists - picks up devices "
                             "and apps opened after Plugin Play started");
    rescanButton.onClick = [this] { rescanDevices(); };
    addAndMakeVisible (rescanButton);

    inputChannelSelector .setTextWhenNoChoicesAvailable ("-");
    outputChannelSelector.setTextWhenNoChoicesAvailable ("-");
    inputChannelSelector .onChange = [this] { if (! updatingSelectors) applyChannelSelection (true); };
    outputChannelSelector.onChange = [this] { if (! updatingSelectors) applyChannelSelection (false); };

    monitorSelector.setTextWhenNoChoicesAvailable ("No outputs");
    monitorSelector.setTextWhenNothingSelected ("None");
    monitorSelector.onChange = [this] { if (! updatingSelectors) applyMonitorSelection(); };

    monitorPairSelector.setTextWhenNoChoicesAvailable ("-");
    monitorPairSelector.onChange = [this] { if (! updatingSelectors) applyMonitorPairSelection(); };

    deviceTypeSelector.onChange = [this]
    {
        if (updatingSelectors)
            return;

        // The synthetic "Enable ASIO" item scans + switches to ASIO on demand (it isn't
        // scanned at startup, where a flaky driver could hang the app). Briefly show a
        // busy cursor since the driver scan can take a moment.
        if (deviceTypeSelector.getSelectedId() == asioEnableItemId)
        {
            juce::MouseCursor::showWaitCursor();
            const bool ok = engine.ensureAsioEnabled();
            juce::MouseCursor::hideWaitCursor();

            if (! ok)
            {
                buildDeviceSelectors();   // revert the dropdown
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon, "ASIO unavailable",
                    "No ASIO driver could be loaded. Make sure your audio interface is "
                    "connected and its driver is installed.");
            }
            return;
        }

        engine.deviceManager.setCurrentAudioDeviceType (deviceTypeSelector.getText(), true);
        // The device manager broadcasts the change; buildDeviceSelectors() then
        // repopulates every selector for the new driver's devices.
    };

    inputSelector       .setTooltip ("Where Plugin Play gets its audio: an input device (e.g. the virtual "
                                     "cable), or a running app - pick an app and Plugin Play routes it "
                                     "through the cable for you (needs a virtual cable installed)");
    outputSelector      .setTooltip ("Audio output device - where processed audio is sent");
    inputChannelSelector .setTooltip ("Which channel pair of the input device to capture");
    outputChannelSelector.setTooltip ("Which channel pair of the output device to play to");
    monitorSelector      .setTooltip ("Optional second output that mirrors the main output - e.g. your "
                                      "interface's headphone jack or booth speakers. Uses shared Windows "
                                      "Audio so it runs alongside ASIO, and plays a fraction of a second "
                                      "behind the main output (fine for a separate room or headphones)");
    monitorPairSelector  .setTooltip ("Which channel pair of the monitor device to play to");
    deviceTypeSelector  .setTooltip ("Audio driver type. Pick 'Enable ASIO' to turn on low-latency ASIO "
                                     "for your interface (loaded on demand so a driver can't slow startup)");
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
    addAndMakeVisible (monitorSelector);
    addAndMakeVisible (monitorPairSelector);
    addAndMakeVisible (deviceTypeSelector);
    addAndMakeVisible (sampleRateSelector);
    addAndMakeVisible (bufferSizeSelector);
    addAndMakeVisible (testButton);
    addAndMakeVisible (rateHint);

    // Input/output device selection is always visible; the channel, driver,
    // rate and buffer controls only show while the audio area is expanded.
    juce::Component* expandedControls[] { &inputChannelLabel, &outputChannelLabel,
                                          &inputChannelSelector, &outputChannelSelector,
                                          &monitorLabel, &monitorSelector,
                                          &monitorPairLabel, &monitorPairSelector,
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

    // Header source readout: empty until an app is being sent through Plugin Play.
    sourceIndicator.setJustificationType (juce::Justification::centredRight);
    sourceIndicator.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    sourceIndicator.setTooltip ("Plugin Play is routing this app's audio through the effect chain. "
                                "Choose a different input to stop.");
    addAndMakeVisible (sourceIndicator);

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
    // No buildDeviceSelectors() here: at construction the session hasn't loaded yet
    // (startup defers AudioEngine::loadSession() until after the first paint, so the
    // window appears immediately) and the device list is still unscanned. The engine's
    // change broadcast right after loadSession() populates every selector.
    updateStatusText();
    updateSourceIndicator();

    // Show the first-run walkthrough once the window is up (deferred so it centres
    // over a fully-constructed main window).
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)]
    {
        if (safe != nullptr)
            safe->maybeShowFirstRunGuide();
    });
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
    helpButton.setBounds (header.removeFromRight (58));
    header.removeFromRight (8);

    // VIRTUAL CABLE and CHECK UPDATES share one slot; updateHeaderButtons() decides
    // which is visible (setup until a cable is installed, updates afterwards).
    const auto cableSlot = header.removeFromRight (118);
    cableButton .setBounds (cableSlot);
    updateButton.setBounds (cableSlot);

    header.removeFromRight (8);
    supportButton.setBounds (header.removeFromRight (78));

    // The rest of the header (right of the wordmark) is the source readout naming
    // the app currently sent through Plugin Play.
    header.removeFromRight (8);
    header.removeFromLeft (210);   // keep clear of the painted logo + wordmark
    sourceIndicator.setBounds (header);

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
    deviceBarBounds = area.removeFromTop (audioExpanded ? 266 : 94);
    auto bar = deviceBarBounds.reduced (16, 10);

    audioToggleButton.setBounds (bar.removeFromBottom (24).withSizeKeepingCentre (170, 24));
    bar.removeFromBottom (10);   // gap between the content and the toggle

    // Input | output device columns, with the rescan button at the row's right end
    // (re-checks both lists so late-started apps / hot-plugged devices show up).
    auto labelRow = bar.removeFromTop (14);
    auto comboRow = bar.removeFromTop (26);

    rescanButton.setBounds (comboRow.removeFromRight (26));
    comboRow.removeFromRight (6);
    labelRow.removeFromRight (32);

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

        // Monitor (second) output: device on the left, its channel pair on the right.
        bar.removeFromTop (10);
        auto monitorLabels = bar.removeFromTop (14);
        auto monitorCombos = bar.removeFromTop (26);

        monitorLabel    .setBounds (monitorLabels.removeFromLeft (barHalf).withTrimmedRight (8));
        monitorPairLabel.setBounds (monitorLabels);
        monitorSelector .setBounds (monitorCombos.removeFromLeft (barHalf).withTrimmedRight (8));
        monitorPairSelector.setBounds (monitorCombos);

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
    updateSourceIndicator();
    updateHeaderButtons();
}

void MainComponent::timerCallback()
{
    const float inL = engine.readInputPeak (0);
    const float inR = engine.readInputPeak (1);
    inputMeter.pushLevels (inL, inR);
    outputMeter.pushLevels (engine.readOutputPeak (0), engine.readOutputPeak (1));

    // Count down a transient "<app> closed" notice; refresh the readout when it expires.
    if (redirectNoticeTicks > 0 && --redirectNoticeTicks == 0)
    {
        redirectNotice.clear();
        updateSourceIndicator();
    }

    if (++timerTicks % 30 == 0)
    {
        updateStatusText();
        checkSampleRate();          // the source's rate can change while we're running
        checkRedirectedAppAlive();   // notice the routed app quitting (would go silent)
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
    // Already open: bring it forward rather than stacking another identical window.
    if (helpWindow != nullptr)
    {
        helpWindow->toFront (true);
        return;
    }

    auto content = std::make_unique<HelpContent> (*uiPrefs, engine.deviceManager);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (content.release());
    options.dialogTitle = "Plugin Play - Help";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    if (auto* window = options.launchAsync())
    {
        applyDarkTitleBar (*window);
        helpWindow = window;
    }
}

void MainComponent::maybeShowFirstRunGuide()
{
    // Show on the very first launch, and again if the installer dropped a marker
    // (e.g. after a reinstall/upgrade) — otherwise respect "Don't show this again".
    const bool installerRequested = WelcomePopup::markerFile().existsAsFile();

    if (! installerRequested && uiPrefs->getBoolValue ("seenWelcome", false))
        return;

    // Consume the installer marker now, so it forces exactly one re-show however the
    // guide is dismissed (permanent suppression is the "Don't show again" toggle).
    WelcomePopup::markerFile().deleteFile();

    WelcomePopup::show (*uiPrefs, engine.deviceManager);
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

            // createLegalFileName strips illegal characters but not names that
            // reduce to nothing (e.g. "???" -> "") or Windows reserved device names
            // (CON, NUL, COM1...), which the filesystem refuses to create.
            auto base = juce::File::createLegalFileName (name).trim();
            while (base.endsWithChar ('.') || base.endsWithChar (' '))
                base = base.dropLastCharacters (1).trim();

            static const juce::StringArray reserved {
                "CON", "PRN", "AUX", "NUL",
                "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9" };

            if (base.isEmpty() || reserved.contains (base, true))
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Invalid preset name",
                    "\"" + name + "\" isn't a usable file name. Please choose another.");
                return;
            }

            const auto file = engine.getPresetsDirectory().getChildFile (base + ".xml");

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

    // Make sure the device types have scanned (getDeviceNames asserts otherwise).
    // A no-op except on a first call that somehow beats AudioEngine::loadSession().
    engine.deviceManager.getAvailableDeviceTypes();

    auto* type = engine.deviceManager.getCurrentDeviceTypeObject();
    auto setup = engine.deviceManager.getAudioDeviceSetup();

    outputSelector.clear (juce::dontSendNotification);

    if (type != nullptr)
    {
        int id = 1;
        for (const auto& name : type->getDeviceNames (false))
        {
            outputSelector.addItem (name, id);
            if (name == setup.outputDeviceName)
                outputSelector.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
    }

    refreshInputSelector();
    refreshMonitorSelector();
    refreshDeviceTypes();
    refreshChannelSelectors();
    refreshSampleRates();
    refreshBufferSizes();
    checkSampleRate();
}

void MainComponent::refreshMonitorSelector()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    monitorSelector.clear (juce::dontSendNotification);
    monitorSelector.addItem ("None", monitorNoneId);

    const auto current    = engine.monitorOutputName();
    const auto mainOutput = engine.deviceManager.getAudioDeviceSetup().outputDeviceName;
    const auto names      = engine.availableMonitorOutputs();
    monitorDeviceCount = names.size();

    int id = monitorNoneId + 1;
    bool found = false;

    for (const auto& name : names)
    {
        // The monitor is a second destination, so leave the main output out of the
        // list — sending to the same device would just double the audio. (id keeps
        // advancing so the id range still lines up with monitorDeviceCount.)
        if (name != mainOutput)
        {
            monitorSelector.addItem (name, id);
            if (name == current)
            {
                monitorSelector.setSelectedId (id, juce::dontSendNotification);
                found = true;
            }
        }
        ++id;
    }

    if (current.isEmpty())
        monitorSelector.setSelectedId (monitorNoneId, juce::dontSendNotification);
    else if (! found)
    {
        // A saved monitor device that isn't present right now (unplugged) stays
        // visible as the selection; the engine keeps the intent and reopens it
        // when it's back. Re-picking this entry is inert.
        monitorSelector.addItem (current + "   [not connected]", id);
        monitorSelector.setSelectedId (id, juce::dontSendNotification);
    }

    // The monitor device's channel pairs, in the same "1+2" form as the main
    // device's pair selectors. Empty (and disabled) while the monitor is off.
    monitorPairSelector.clear (juce::dontSendNotification);

    const auto numChannels = engine.monitorOutputChannels();
    for (int start = 0; start < numChannels; start += 2)
    {
        auto text = juce::String (start + 1);
        if (start + 1 < numChannels)
            text << "+" << start + 2;

        monitorPairSelector.addItem (text, start / 2 + 1);
    }

    monitorPairSelector.setSelectedId (engine.monitorOutputPairStart() / 2 + 1,
                                       juce::dontSendNotification);
    monitorPairSelector.setEnabled (monitorPairSelector.getNumItems() > 1);
}

void MainComponent::refreshInputSelector()
{
    const juce::ScopedValueSetter<bool> guard (updatingSelectors, true);

    auto* type = engine.deviceManager.getCurrentDeviceTypeObject();
    const auto selectedDevice = engine.deviceManager.getAudioDeviceSetup().inputDeviceName;
    const bool redirecting = engine.isRedirectingApp();
    const auto redirectedApp = engine.redirectedAppName();

    inputSelector.clear (juce::dontSendNotification);

    // 1) Audio input devices (ids 1..N) — virtual cable, hardware loopback, etc.
    if (type != nullptr)
    {
        int id = 1;
        for (const auto& name : type->getDeviceNames (true))
        {
            inputSelector.addItem (name, id);
            if (! redirecting && name == selectedDevice)
                inputSelector.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
    }

    // 2) Running apps (ids captureItemBase + index). Picking one redirects that app's
    //    audio into the cable and reads it back — Plugin Play wires the cable for you.
    captureSources = engine.availableCaptureSources();

    if (! captureSources.empty())
    {
        inputSelector.addSeparator();
        if (auto* root = inputSelector.getRootMenu())
            root->addSectionHeader ("Send an app through Plugin Play");

        for (size_t i = 0; i < captureSources.size(); ++i)
        {
            const auto& s = captureSources[i];
            auto label = s.executable.isNotEmpty() ? s.executable : ("PID " + juce::String (s.pid));
            if (s.displayName.isNotEmpty() && ! s.displayName.equalsIgnoreCase (s.executable))
                label << "  \xe2\x80\x94  " << s.displayName;
            if (s.active)
                label << "   [ACTIVE]";

            inputSelector.addItem (label, captureItemBase + (int) i);
        }
    }

    // Reflect the app we're currently redirecting (matched by executable name). If it
    // isn't in the enumerated list any more (went idle), add a synthetic entry so the
    // choice still shows as selected.
    if (redirecting)
    {
        auto it = std::find_if (captureSources.begin(), captureSources.end(),
                                [&] (const AudioSource& s) { return s.executable.equalsIgnoreCase (redirectedApp); });

        if (it != captureSources.end())
        {
            inputSelector.setSelectedId (captureItemBase + (int) std::distance (captureSources.begin(), it),
                                         juce::dontSendNotification);
        }
        else
        {
            const int id = captureItemBase + (int) captureSources.size();
            inputSelector.addItem (redirectedApp + "   [routed]", id);
            inputSelector.setSelectedId (id, juce::dontSendNotification);
        }
    }
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

   #if JUCE_WINDOWS
    // ASIO is enabled on demand — scanning it at startup can hang on a flaky driver.
    if (! engine.isAsioEnabled())
        deviceTypeSelector.addItem ("Enable ASIO (low latency)\xe2\x80\xa6", asioEnableItemId);
   #endif

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

void MainComponent::applyInputSelection()
{
    const auto id = inputSelector.getSelectedId();

    // An app: redirect its audio into the cable and read it back. The engine sets the
    // input device to the cable's recording endpoint for us. If no cable is installed
    // (or routing is unsupported), tell the user and offer the cable setup, then revert
    // the selection to whatever the input device currently is.
    if (id >= captureItemBase)
    {
        const auto index = (size_t) (id - captureItemBase);
        if (index >= captureSources.size())
            return;

        const auto& source = captureSources[index];
        const auto error = engine.setRedirectedApp (source.pid, source.executable);

        if (error.isNotEmpty())
        {
            buildDeviceSelectors();   // revert the dropdown to the real input device

            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::InfoIcon, "Couldn't route that app",
                error, "Set up cable", "Cancel", this,
                juce::ModalCallbackFunction::create (
                    [safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
                    {
                        if (r == 1 && safe != nullptr)
                            CableSetupComponent::launch (safe->engine.deviceManager);
                    }));
        }
        return;
    }

    // A real audio input device: stop any app redirect, then apply the device.
    if (engine.isRedirectingApp())
        engine.clearRedirectedApp();

    applyDeviceSelection();
}

void MainComponent::applyDeviceSelection()
{
    auto setup = engine.deviceManager.getAudioDeviceSetup();

    // Only drive the input device from the selector when it points at an actual
    // device; while an app is selected the input selector holds an app entry whose
    // text is an app name, not a device (e.g. an OUTPUT change must not clobber the
    // cable-output input device the redirect set up with it).
    const auto inputId = inputSelector.getSelectedId();
    if (inputId > 0 && inputId < captureItemBase)
        setup.inputDeviceName = inputSelector.getText();

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

void MainComponent::applyMonitorSelection()
{
    const auto id = monitorSelector.getSelectedId();

    // The synthetic "[not connected]" entry isn't an openable device; reselecting
    // it changes nothing.
    if (id > monitorNoneId + monitorDeviceCount)
        return;

    const auto name  = id == monitorNoneId ? juce::String() : monitorSelector.getText();
    const auto error = engine.setMonitorOutput (name);

    if (error.isNotEmpty())
    {
        buildDeviceSelectors();   // revert the dropdown to the engine's actual state
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Couldn't open monitor output", error);
    }
}

void MainComponent::applyMonitorPairSelection()
{
    const auto id = monitorPairSelector.getSelectedId();
    if (id <= 0)
        return;

    const auto error = engine.setMonitorOutputPair (2 * (id - 1));

    if (error.isNotEmpty())
    {
        buildDeviceSelectors();   // revert the dropdown to the engine's actual state
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Couldn't change monitor channels", error);
    }
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
                                          &monitorLabel, &monitorSelector,
                                          &monitorPairLabel, &monitorPairSelector,
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
        // A just-opened device can momentarily report a 0 sample rate — guard the
        // latency division so it doesn't produce inf/NaN in the status line.
        const auto rate = device->getCurrentSampleRate();
        const auto latencyMs = rate > 0.0 ? 1000.0 * device->getOutputLatencyInSamples() / rate
                                          : 0.0;

        text << engine.deviceManager.getCurrentAudioDeviceType()
             << "  |  " << device->getName()
             << "  |  " << juce::String (rate / 1000.0, 1) << " kHz"
             << "  |  " << device->getCurrentBufferSizeSamples() << " smp"
             << "  |  out " << juce::String (latencyMs, 1) << " ms";

        // CPU load in its own label so it can be tinted before dropouts become audible.
        const auto cpu = engine.deviceManager.getCpuUsage() * 100.0;
        // roundToInt, not String (cpu, 0): JUCE treats 0 decimal places as "use
        // full precision", which printed strings like "CPU 2.24424%".
        cpuText = "CPU " + juce::String (juce::roundToInt (cpu)) + "%";
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

void MainComponent::updateSourceIndicator()
{
    juce::String text;
    juce::Colour colour = accentBright;

    if (redirectNotice.isNotEmpty())
    {
        // Transient notice (e.g. the routed app just closed).
        text   = redirectNotice;
        colour = metricWarn;
    }
    else if (engine.isRedirectingApp())
    {
        // Persistent "we're routing this app" readout: just the program name,
        // without its .exe suffix. \xe2\x96\xb6 = ▶
        auto app = engine.redirectedAppName();
        if (app.endsWithIgnoreCase (".exe"))
            app = app.dropLastCharacters (4);

        text << "\xe2\x96\xb6  " << app;
    }

    sourceIndicator.setText (text, juce::dontSendNotification);
    sourceIndicator.setColour (juce::Label::textColourId, colour);
}

void MainComponent::updateHeaderButtons()
{
    // Once a cable is installed the VIRTUAL CABLE setup button has done its job, so
    // its header slot becomes CHECK UPDATES (cable setup stays reachable from HELP
    // and the walkthrough). If the cable is ever uninstalled, the setup button comes
    // back. The no-rescan lookup just reads the already-enumerated device lists.
    cableInstalled = VirtualCable::findInstalled (engine.deviceManager, false).isNotEmpty();

    cableButton .setVisible (! cableInstalled);
    updateButton.setVisible (cableInstalled);
}

void MainComponent::rescanDevices()
{
    // Force a fresh driver scan so devices and apps that appeared after startup
    // (DJ software launched later, a cable just installed) become selectable.
    juce::MouseCursor::showWaitCursor();

    for (auto* type : engine.deviceManager.getAvailableDeviceTypes())
        if (type != nullptr)
            type->scanForDevices();

    juce::MouseCursor::hideWaitCursor();

    buildDeviceSelectors();     // re-lists devices AND running apps
    updateHeaderButtons();      // a cable may have (dis)appeared
}

void MainComponent::checkForUpdates()
{
    if (checkingForUpdates)
        return;

    checkingForUpdates = true;
    updateButton.setEnabled (false);
    updateButton.setButtonText ("CHECKING...");

    const auto current = juce::JUCEApplication::getInstance()->getApplicationVersion();

    UpdateCheck::checkAsync (current,
        [safe = juce::Component::SafePointer<MainComponent> (this), current] (const UpdateCheck::Result& result)
        {
            if (safe == nullptr)
                return;

            safe->checkingForUpdates = false;
            safe->updateButton.setEnabled (true);
            safe->updateButton.setButtonText ("CHECK UPDATES");

            if (! result.ok)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon, "Update check failed",
                    "Couldn't reach GitHub to check for updates. Check your internet "
                    "connection and try again.");
                return;
            }

            if (! result.updateAvailable)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon, "Up to date",
                    "You're on the latest version (" + current + ").");
                return;
            }

            const auto url = result.downloadUrl;
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon, "Update available",
                "Version " + result.latestVersion + " is available (you have "
                    + current + ").\n\nDownload the new installer?",
                "Download", "Later", nullptr,
                juce::ModalCallbackFunction::create ([url] (int r)
                {
                    if (r == 1)
                        juce::URL (url).launchInDefaultBrowser();
                }));
        });
}

void MainComponent::checkRedirectedAppAlive()
{
    if (! engine.isRedirectingApp() || engine.isRedirectedAppRunning())
        return;

    // The app we were routing has exited; audio would otherwise just go silent with a
    // stale selection. Stop routing (restores the app's own output) and tell the user.
    const auto app = engine.redirectedAppName();
    engine.clearRedirectedApp();
    buildDeviceSelectors();

    redirectNotice      = app + " closed \xe2\x80\x94 routing stopped";
    redirectNoticeTicks = 30 * 6;   // ~6 seconds at 30 Hz
    updateSourceIndicator();
}

} // namespace play
