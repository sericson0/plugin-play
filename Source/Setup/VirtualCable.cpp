#include "VirtualCable.h"
#include "../Theme.h"

#include <regex>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <shellapi.h>
#endif

namespace play
{

using namespace play::Colours;

//==============================================================================
namespace VirtualCable
{
    bool nameLooksLikeCable (const juce::String& deviceName)
    {
        const auto n = deviceName.toLowerCase();

        // VB-CABLE ("CABLE Input/Output (VB-Audio Virtual Cable)"), plus the other
        // common virtual cables so we recognise a working setup the user already has.
        return n.contains ("vb-audio")
            || n.contains ("cable input")
            || n.contains ("cable output")
            || n.contains ("voicemeeter")
            || n.contains ("virtual audio cable")
            || n.contains ("vac ");
    }

    juce::String findInstalled (juce::AudioDeviceManager& deviceManager, bool rescan)
    {
        // getAvailableDeviceTypes() runs the one-time startup scan if it hasn't
        // happened yet, so the fast path never reads an unscanned list.
        for (auto* type : deviceManager.getAvailableDeviceTypes())
        {
            if (type == nullptr)
                continue;

            if (rescan)
                type->scanForDevices();

            for (bool wantInputs : { true, false })
                for (const auto& name : type->getDeviceNames (wantInputs))
                    if (nameLooksLikeCable (name))
                        return name;
        }

        return {};
    }

    static std::unique_ptr<juce::InputStream> openStream (const juce::String& url, int timeoutMs)
    {
        return juce::URL (url).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs (timeoutMs));
    }

    juce::String resolveLatestZipUrl()
    {
        if (auto in = openStream (downloadPage, 10000))
        {
            const auto html = in->readEntireStreamAsString().toStdString();

            // The page links VBCABLE_Driver_PackNN.zip; pick the highest NN so we
            // always fetch the newest pack even after VB-Audio bumps the version.
            const std::regex re (R"(VBCABLE_Driver_Pack(\d+)\.zip)");
            int best = -1;

            // getIntValue never throws (unlike std::stoi, which would terminate the
            // background install thread on a pathologically long digit run).
            for (auto it = std::sregex_iterator (html.begin(), html.end(), re);
                 it != std::sregex_iterator(); ++it)
                best = juce::jmax (best, juce::String ((*it)[1].str()).getIntValue());

            if (best >= 0)
                return "https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack"
                       + juce::String (best) + ".zip";
        }

        return fallbackZipUrl;
    }

    bool downloadTo (const juce::String& url, const juce::File& dest)
    {
        auto in = openStream (url, 15000);
        if (in == nullptr)
            return false;

        const auto expected = in->getTotalLength();   // -1 if the server didn't say

        dest.deleteFile();
        juce::FileOutputStream out (dest);
        if (! out.openedOk())
            return false;

        const auto written = out.writeFromInputStream (*in, -1);
        out.flush();

        // A dropped connection yields a partial zip that would otherwise "succeed"
        // and then fail deeper in unzip with a misleading message. Reject it here if
        // the byte count falls short of the advertised Content-Length.
        if (expected > 0 && written < expected)
        {
            dest.deleteFile();
            return false;
        }

        return dest.getSize() > 0;
    }

    bool launchInstaller (const juce::File& exe)
    {
       #if JUCE_WINDOWS
        const auto path = exe.getFullPathName();

        SHELLEXECUTEINFOW sei { };
        sei.cbSize = sizeof (sei);
        sei.fMask  = SEE_MASK_NOASYNC;
        sei.lpVerb = L"runas";                       // trigger the UAC elevation prompt
        sei.lpFile = path.toWideCharPointer();
        sei.nShow  = SW_SHOWNORMAL;

        return ShellExecuteExW (&sei) != FALSE;      // FALSE if the user declines UAC
       #else
        return exe.startAsProcess();
       #endif
    }

    bool reboot()
    {
       #if JUCE_WINDOWS
        // shutdown.exe runs with the calling user's normal shutdown right, so we
        // don't need to hand-adjust the SE_SHUTDOWN privilege. /t 5 leaves a few
        // seconds during which "shutdown /a" could abort it.
        juce::ChildProcess proc;
        return proc.start ("shutdown /r /t 5");
       #else
        return false;
       #endif
    }
} // namespace VirtualCable

//==============================================================================
struct CableSetupComponent::InstallThread : public juce::Thread
{
    explicit InstallThread (CableSetupComponent& o)
        : juce::Thread ("VB-CABLE install"), owner (&o) {}

    template <typename Fn>
    void post (Fn&& fn)
    {
        juce::MessageManager::callAsync (
            [safe = owner, fn = std::forward<Fn> (fn)]
            {
                if (auto* c = safe.getComponent())
                    fn (*c);
            });
    }

    void run() override
    {
        post ([] (auto& c) { c.showProgress ("Finding the latest version..."); });

        const auto zipUrl = VirtualCable::resolveLatestZipUrl();
        if (threadShouldExit()) return;

        auto workDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("PluginPlay_VBCABLE");
        workDir.deleteRecursively();
        workDir.createDirectory();

        post ([] (auto& c) { c.showProgress ("Downloading VB-CABLE..."); });

        const auto zipFile = workDir.getChildFile ("VBCABLE.zip");
        if (! VirtualCable::downloadTo (zipUrl, zipFile))
        {
            post ([] (auto& c) { c.installFailed (
                "Download failed. Check your connection, or use \"Open download page instead\"."); });
            return;
        }
        if (threadShouldExit()) return;

        post ([] (auto& c) { c.showProgress ("Extracting..."); });

        const auto extractDir = workDir.getChildFile ("extracted");
        extractDir.createDirectory();

        juce::ZipFile zip (zipFile);
        if (zip.uncompressTo (extractDir, true).failed())
        {
            post ([] (auto& c) { c.installFailed (
                "Couldn't unzip the download. Use \"Open download page instead\" to install manually."); });
            return;
        }
        if (threadShouldExit()) return;

        auto installer = extractDir.getChildFile (VirtualCable::installerExe);
        if (! installer.existsAsFile())
        {
            auto found = extractDir.findChildFiles (juce::File::findFiles, true,
                                                    VirtualCable::installerExe);
            if (found.isEmpty())
                found = extractDir.findChildFiles (juce::File::findFiles, true, "VBCABLE_Setup*.exe");

            if (found.isEmpty())
            {
                post ([] (auto& c) { c.installFailed (
                    "Downloaded, but couldn't find the installer inside the zip. "
                    "Use \"Open download page instead\" to install manually."); });
                return;
            }
            installer = found.getReference (0);
        }
        if (threadShouldExit()) return;

        post ([] (auto& c) { c.showProgress (
            "Starting the VB-CABLE installer - approve the Windows prompt..."); });

        if (! VirtualCable::launchInstaller (installer))
        {
            post ([path = installer.getFullPathName()] (auto& c) { c.installFailed (
                "Couldn't start the installer (you may have declined the Windows prompt).\n"
                "You can run it yourself from:\n" + path); });
            return;
        }

        post ([] (auto& c) { c.installLaunched(); });
    }

    juce::Component::SafePointer<CableSetupComponent> owner;
};

//==============================================================================
CableSetupComponent::CableSetupComponent (juce::AudioDeviceManager& dm)
    : deviceManager (dm)
{
    heading.setText ("Virtual Cable Setup", juce::dontSendNotification);
    heading.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    heading.setColour (juce::Label::textColourId, textBright);
    addAndMakeVisible (heading);

    statusLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    body.setMultiLine (true);
    body.setReadOnly (true);
    body.setCaretVisible (false);
    body.setScrollbarsShown (true);
    body.setColour (juce::TextEditor::backgroundColourId, panelBackground);
    body.setColour (juce::TextEditor::outlineColourId, gridLine);
    body.setColour (juce::TextEditor::focusedOutlineColourId, gridLine);
    body.setColour (juce::TextEditor::textColourId, textNormal);
    body.setFont (juce::Font (juce::FontOptions (14.0f)));
    addAndMakeVisible (body);

    installButton.onClick = [this] { startInstall(); };
    rebootButton.onClick  = [this] { confirmAndReboot(); };
    recheckButton.onClick = [this] { recheck(); };
    closeButton.onClick   = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };

    addAndMakeVisible (installButton);
    addChildComponent (rebootButton);   // shown only after the installer has launched
    addAndMakeVisible (recheckButton);
    addAndMakeVisible (closeButton);

    for (auto* link : { &pageLink, &donateLink })
    {
        link->setJustificationType (juce::Justification::centredLeft);
        link->setColour (juce::HyperlinkButton::textColourId, accent);
        addAndMakeVisible (*link);
    }
    donateLink.setColour (juce::HyperlinkButton::textColourId, textNormal);

    detectedCable = VirtualCable::findInstalled (deviceManager, false);
    refreshContent();

    setSize (520, 470);
}

CableSetupComponent::~CableSetupComponent()
{
    if (installThread != nullptr)
        installThread->stopThread (4000);
}

//==============================================================================
void CableSetupComponent::launch (juce::AudioDeviceManager& deviceManager)
{
    // Only one setup dialog at a time: a second click (from the header button or the
    // "Set up cable" offer in an alert) raises the existing window instead of
    // stacking a duplicate mid-install.
    static juce::Component::SafePointer<juce::DialogWindow> openDialog;

    if (openDialog != nullptr)
    {
        openDialog->toFront (true);
        return;
    }

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new CableSetupComponent (deviceManager));
    options.dialogTitle = "Virtual Cable Setup";
    options.dialogBackgroundColour = background;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;

    if (auto* window = options.launchAsync())
    {
        openDialog = window;
        applyDarkTitleBar (*window);
    }
}

//==============================================================================
void CableSetupComponent::startInstall()
{
    if (busy)
        return;

    setBusy (true);
    installThread = std::make_unique<InstallThread> (*this);
    installThread->startThread();
}

void CableSetupComponent::setBusy (bool shouldBeBusy)
{
    busy = shouldBeBusy;
    installButton.setEnabled (! busy);
    recheckButton.setEnabled (! busy);
}

void CableSetupComponent::showProgress (const juce::String& message)
{
    statusLabel.setColour (juce::Label::textColourId, textBright);
    statusLabel.setText (message, juce::dontSendNotification);
}

void CableSetupComponent::installFailed (const juce::String& message)
{
    setBusy (false);
    statusLabel.setColour (juce::Label::textColourId, metricWarn);
    statusLabel.setText ("Install couldn't finish", juce::dontSendNotification);
    body.setText (message, juce::dontSendNotification);
}

void CableSetupComponent::installLaunched()
{
    setBusy (false);
    statusLabel.setColour (juce::Label::textColourId, metricGood);
    statusLabel.setText ("Installer launched", juce::dontSendNotification);
    body.setText (
        "The VB-CABLE installer is now open.\n"
        "\n"
        "1.  Click \"Install Driver\" in the installer window.\n"
        "2.  Approve any Windows security prompts.\n"
        "3.  When it finishes, click \"Reboot now to finish\" below (or reboot yourself).\n"
        "4.  After the restart, reopen Plugin Play and click \"Re-check\".\n"
        "\n"
        "VB-CABLE is donationware from VB-Audio - if you find it useful, please "
        "consider paying what you can using the link below.",
        juce::dontSendNotification);

    installButton.setVisible (false);
    rebootButton.setVisible (true);
}

void CableSetupComponent::confirmAndReboot()
{
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        "Restart Windows?",
        "Windows needs to restart to finish installing VB-CABLE.\n\n"
        "Save any work in your DJ software and other apps first - everything will "
        "close. Restart now?",
        "Restart now",
        "Not yet",
        this,
        juce::ModalCallbackFunction::create (
            [safe = juce::Component::SafePointer<CableSetupComponent> (this)] (int result)
            {
                if (result == 1 && safe != nullptr)
                    if (! VirtualCable::reboot())
                        safe->statusLabel.setText ("Couldn't start the restart - please reboot manually.",
                                                   juce::dontSendNotification);
            }));
}

//==============================================================================
void CableSetupComponent::recheck()
{
    // The user is explicitly asking "is it there NOW?" (usually right after an
    // install), so force a fresh driver scan rather than trusting the lists.
    detectedCable = VirtualCable::findInstalled (deviceManager, true);
    refreshContent();
}

void CableSetupComponent::refreshContent()
{
    installButton.setVisible (true);
    rebootButton.setVisible (false);

    const bool installed = detectedCable.isNotEmpty();

    if (installed)
    {
        statusLabel.setColour (juce::Label::textColourId, metricGood);
        statusLabel.setText ("Detected:  " + detectedCable, juce::dontSendNotification);

        body.setText (
            "A virtual cable is installed, so you can route your DJ software through "
            "Plugin Play as a device.\n"
            "\n"
            "1.  In your DJ software, set the OUTPUT device to\n"
            "    \"CABLE Input (VB-Audio Virtual Cable)\".\n"
            "\n"
            "2.  In Plugin Play -> Audio Settings, set the INPUT device to\n"
            "    \"CABLE Output (VB-Audio Virtual Cable)\".\n"
            "\n"
            "3.  Set Plugin Play's OUTPUT to your interface (ASIO recommended).\n"
            "\n"
            "Signal flow:\n"
            "    DJ software  ->  cable  ->  Plugin Play (FX)  ->  your DAC\n"
            "\n"
            "Keep every stage at the same sample rate (usually 44.1 kHz) and leave "
            "\"Audio Enhancements\" off on the cable to stay bit-transparent.\n",
            juce::dontSendNotification);

        installButton.setButtonText ("RE-INSTALL VB-CABLE");
    }
    else
    {
        statusLabel.setColour (juce::Label::textColourId, metricWarn);
        statusLabel.setText ("No virtual cable found", juce::dontSendNotification);

        body.setText (
            "Plugin Play needs a virtual cable to receive sound from your other apps - "
            "both the app picker in the INPUT dropdown and manual device routing run "
            "through it.\n"
            "\n"
            "To install the recommended free cable, VB-CABLE:\n"
            "\n"
            "1.  Click \"Download & Install VB-CABLE\" below. Plugin Play will fetch the "
            "latest version from VB-Audio and open its installer for you.\n"
            "2.  Click \"Install Driver\" in the installer and approve the Windows prompts.\n"
            "3.  Reboot Windows.\n"
            "4.  Come back here and click \"Re-check\".\n"
            "\n"
            "Prefer to do it by hand? Use \"Open download page instead\" below.",
            juce::dontSendNotification);

        installButton.setButtonText ("DOWNLOAD & INSTALL VB-CABLE");
    }
}

//==============================================================================
void CableSetupComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void CableSetupComponent::resized()
{
    auto area = getLocalBounds().reduced (18, 16);

    heading.setBounds (area.removeFromTop (30));
    area.removeFromTop (2);
    statusLabel.setBounds (area.removeFromTop (24));
    area.removeFromTop (8);

    auto buttonRow = area.removeFromBottom (34);
    closeButton.setBounds (buttonRow.removeFromRight (100));
    buttonRow.removeFromRight (8);
    recheckButton.setBounds (buttonRow.removeFromRight (110));
    installButton.setBounds (buttonRow.removeFromLeft (230));
    rebootButton.setBounds (installButton.getBounds());   // same slot; only one is shown

    area.removeFromBottom (10);
    donateLink.setBounds (area.removeFromBottom (20));
    pageLink.setBounds (area.removeFromBottom (20));
    area.removeFromBottom (8);

    body.setBounds (area);
}

} // namespace play
