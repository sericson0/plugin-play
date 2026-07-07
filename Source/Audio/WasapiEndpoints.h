#pragma once

#include <JuceHeader.h>

namespace play::WasapiEndpoints
{
    /** Native Windows shared-mode ("mix format") sample rate of the endpoint whose
        friendly name matches @p deviceName, or 0.0 if it can't be determined
        (e.g. an ASIO device, an unmatched name, or a non-Windows build).

        This is the rate the source is really running at in Windows, so it is the
        rate Plugin Play should match to avoid an extra resample on the signal we
        care about. @p isInput selects capture (true) vs render (false) endpoints. */
    double mixRateForDevice (const juce::String& deviceName, bool isInput);
}
