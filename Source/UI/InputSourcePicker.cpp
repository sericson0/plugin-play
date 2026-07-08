#include "InputSourcePicker.h"
#include "../Theme.h"

namespace play
{

using namespace play::Colours;

//==============================================================================
InputSourcePicker::InputSourcePicker (AudioEngine& engineToUse)
    : engine (engineToUse)
{
    heading.setText ("Choose Input Source", juce::dontSendNotification);
    heading.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    heading.setColour (juce::Label::textColourId, textBright);
    addAndMakeVisible (heading);

    body.setMultiLine (true);
    body.setReadOnly (true);
    body.setCaretVisible (false);
    body.setScrollbarsShown (true);
    body.setColour (juce::TextEditor::backgroundColourId, panelBackground);
    body.setColour (juce::TextEditor::outlineColourId, gridLine);
    body.setColour (juce::TextEditor::focusedOutlineColourId, gridLine);
    body.setColour (juce::TextEditor::textColourId, textNormal);
    body.setFont (juce::Font (juce::FontOptions (14.0f)));
    body.setText (
        "Plugin Play can take its input two ways:\n\n"
        "\xe2\x80\xa2  Audio input device \xe2\x80\x94 read whatever your INPUT selector points at "
        "(a virtual cable, or a hardware loopback). Set your DJ software's output to "
        "that device.\n\n"
        "\xe2\x80\xa2  Capture an application (no install) \xe2\x80\x94 grab a running app's audio "
        "directly by picking it below. Plugin Play mutes that app's sound at your "
        "speakers (a red mute may appear on the volume icon) and sends the processed "
        "audio to your chosen OUTPUT device instead. Keep the app's own volume at 100% "
        "and send Plugin Play's output to a different device than the one being muted.",
        juce::dontSendNotification);
    addAndMakeVisible (body);

    sourceBox.setColour (juce::ComboBox::backgroundColourId, panelBackground);
    sourceBox.setColour (juce::ComboBox::outlineColourId, gridLine);
    sourceBox.setColour (juce::ComboBox::textColourId, textBright);
    addAndMakeVisible (sourceBox);

    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, accentBright);
    addAndMakeVisible (statusLabel);

    applyButton.onClick   = [this] { apply(); };
    refreshButton.onClick = [this] { refresh(); };
    closeButton.onClick   = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    addAndMakeVisible (applyButton);
    addAndMakeVisible (refreshButton);
    addAndMakeVisible (closeButton);

    refresh();
    setSize (520, 420);
}

//==============================================================================
void InputSourcePicker::launch (AudioEngine& engine)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new InputSourcePicker (engine));
    options.dialogTitle = "Choose Input Source";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
        applyDarkTitleBar (*window);
}

//==============================================================================
void InputSourcePicker::refresh()
{
    sources = engine.availableCaptureSources();

    sourceBox.clear (juce::dontSendNotification);
    sourceBox.addItem ("Audio input device (INPUT selector)", deviceItemId);
    sourceBox.addSeparator();

    if (sources.empty())
    {
        sourceBox.addItem ("(no running audio apps found \xe2\x80\x94 start one and Refresh)", 1000);
        sourceBox.setItemEnabled (1000, false);
    }

    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& s = sources[i];
        auto label = s.executable.isNotEmpty() ? s.executable : ("PID " + juce::String (s.pid));
        if (s.displayName.isNotEmpty() && ! s.displayName.equalsIgnoreCase (s.executable))
            label << "  \xe2\x80\x94  " << s.displayName;
        if (s.active)
            label << "   [ACTIVE]";

        sourceBox.addItem (label, (int) i + 2);
    }

    // Preselect whatever is currently in use.
    if (engine.isCapturingInput())
    {
        const auto pid = engine.capturedSourcePid();
        auto it = std::find_if (sources.begin(), sources.end(),
                                [pid] (const AudioSource& s) { return s.pid == pid; });
        sourceBox.setSelectedId (it != sources.end()
                                     ? (int) std::distance (sources.begin(), it) + 2
                                     : deviceItemId,
                                 juce::dontSendNotification);
    }
    else
    {
        sourceBox.setSelectedId (deviceItemId, juce::dontSendNotification);
    }

    updateStatus();
}

void InputSourcePicker::apply()
{
    const auto id = sourceBox.getSelectedId();

    if (id == deviceItemId)
    {
        engine.setDeviceInput();
    }
    else
    {
        const auto index = (size_t) (id - 2);
        if (index >= sources.size())
            return;

        engine.setCaptureSource (sources[index].pid);
    }

    updateStatus();
}

void InputSourcePicker::updateStatus()
{
    if (engine.isCapturingInput())
    {
        const auto pid = engine.capturedSourcePid();
        auto it = std::find_if (sources.begin(), sources.end(),
                                [pid] (const AudioSource& s) { return s.pid == pid; });
        const auto name = it != sources.end() && it->executable.isNotEmpty()
                              ? it->executable : ("PID " + juce::String (pid));
        statusLabel.setText ("Capturing: " + name + "  (endpoint muted)", juce::dontSendNotification);
    }
    else
    {
        statusLabel.setText ("Using the selected audio input device.", juce::dontSendNotification);
    }
}

//==============================================================================
void InputSourcePicker::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void InputSourcePicker::resized()
{
    auto area = getLocalBounds().reduced (20);

    heading.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    body.setBounds (area.removeFromTop (180));
    area.removeFromTop (14);

    auto pickRow = area.removeFromTop (28);
    refreshButton.setBounds (pickRow.removeFromRight (90));
    pickRow.removeFromRight (8);
    sourceBox.setBounds (pickRow);

    area.removeFromTop (12);
    statusLabel.setBounds (area.removeFromTop (22));

    auto buttonRow = area.removeFromBottom (34);
    closeButton.setBounds (buttonRow.removeFromRight (90));
    buttonRow.removeFromRight (8);
    applyButton.setBounds (buttonRow.removeFromRight (150));
}

} // namespace play
