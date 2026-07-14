#include "MonitorOutput.h"

namespace play
{

MonitorOutput::MonitorOutput()
{
    fifoBuffer.setSize (2, fifoCapacity);
    fifoBuffer.clear();
}

MonitorOutput::~MonitorOutput()
{
    close();
}

//==============================================================================
juce::AudioIODeviceType* MonitorOutput::findMonitorType (juce::AudioDeviceManager& manager)
{
    // getAvailableDeviceTypes() ensures every registered type has scanned.
    const auto& types = manager.getAvailableDeviceTypes();

   #if JUCE_WINDOWS
    for (auto* type : types)
        if (type->getTypeName() == "Windows Audio")   // shared-mode WASAPI
            return type;
   #endif

    if (auto* current = manager.getCurrentDeviceTypeObject())
        return current;

    return types.isEmpty() ? nullptr : types.getFirst();
}

juce::StringArray MonitorOutput::availableDevices (juce::AudioDeviceManager& manager)
{
    if (auto* type = findMonitorType (manager))
        return type->getDeviceNames (false);

    return {};
}

//==============================================================================
juce::String MonitorOutput::open (juce::AudioDeviceManager& manager, const juce::String& deviceName,
                                  int startChannel)
{
    JUCE_ASSERT_MESSAGE_THREAD
    close();

    auto* type = findMonitorType (manager);
    if (type == nullptr)
        return "No audio driver is available for the monitor output.";

    std::unique_ptr<juce::AudioIODevice> newDevice (type->createDevice (deviceName, {}));
    if (newDevice == nullptr)
        return "The output device \"" + deviceName + "\" isn't available.";

    // Run at the rate closest to the main device's, so the resampler has the
    // least to do (at identical rates it's a near-passthrough).
    const auto preferred = sourceRate.load() > 0.0 ? sourceRate.load() : 48000.0;
    double rate = 0.0;
    for (auto available : newDevice->getAvailableSampleRates())
        if (rate == 0.0 || std::abs (available - preferred) < std::abs (rate - preferred))
            rate = available;

    // The chosen stereo pair; a pair that no longer exists (device changed, or a
    // stale saved value) falls back to the first, and a mono device gets one bit.
    const auto numOuts = newDevice->getOutputChannelNames().size();
    if (startChannel < 0 || startChannel >= numOuts)
        startChannel = 0;

    juce::BigInteger outputs;
    outputs.setBit (startChannel);
    if (startChannel + 1 < numOuts)
        outputs.setBit (startChannel + 1);

    const auto error = newDevice->open ({}, outputs, rate, newDevice->getDefaultBufferSize());
    if (error.isNotEmpty())
        return error;

    device = std::move (newDevice);
    currentName = deviceName;
    device->start (this);   // audioDeviceAboutToStart sets up buffers, then flips `active`

    return {};
}

void MonitorOutput::close()
{
    active.store (false);

    if (device != nullptr)
    {
        device->stop();
        device->close();
        device = nullptr;
    }

    currentName.clear();
}

bool MonitorOutput::isRunning() const
{
    return device != nullptr && device->isPlaying();
}

int MonitorOutput::outputChannelCount() const
{
    return device != nullptr ? device->getOutputChannelNames().size() : 0;
}

//==============================================================================
void MonitorOutput::setSourceFormat (double sampleRate, int blockSize) noexcept
{
    sourceRate.store (sampleRate, std::memory_order_relaxed);
    sourceBlock.store (blockSize, std::memory_order_relaxed);
}

void MonitorOutput::push (const float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! active.load (std::memory_order_relaxed) || numSamples <= 0)
        return;

    // First two non-null channels; mono sources go to both ears.
    const float* left  = nullptr;
    const float* right = nullptr;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (channels[ch] == nullptr)
            continue;

        if (left == nullptr)
            left = channels[ch];
        else
        {
            right = channels[ch];
            break;
        }
    }

    if (left == nullptr)
        return;

    if (right == nullptr)
        right = left;

    // prepareToWrite grants only what fits; a full FIFO drops the excess (which
    // only happens while the monitor isn't draining — the drift control keeps a
    // running monitor's fill far below capacity).
    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);

    if (size1 > 0)
    {
        fifoBuffer.copyFrom (0, start1, left,  size1);
        fifoBuffer.copyFrom (1, start1, right, size1);
    }

    if (size2 > 0)
    {
        fifoBuffer.copyFrom (0, start2, left  + size1, size2);
        fifoBuffer.copyFrom (1, start2, right + size1, size2);
    }

    fifo.finishedWrite (size1 + size2);
}

//==============================================================================
int MonitorOutput::targetFillSamples() const noexcept
{
    const auto rate     = sourceRate.load (std::memory_order_relaxed);
    const auto srcBlock = (double) sourceBlock.load (std::memory_order_relaxed);
    const auto monBlockInSrc = monitorRate > 0.0 ? (double) monitorBlock * rate / monitorRate : 0.0;

    // One block of each side in flight, plus a scheduling-jitter margin of at
    // least 10 ms (or another block, whichever is larger).
    const auto margin = juce::jmax (0.010 * rate, srcBlock, monBlockInSrc);

    return juce::jmin (fifoCapacity / 2, (int) std::ceil (srcBlock + monBlockInSrc + margin));
}

void MonitorOutput::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    monitorRate  = d->getCurrentSampleRate();
    monitorBlock = d->getCurrentBufferSizeSamples();

    // Sized for the worst realistic ratio (96 kHz source into a 44.1 kHz monitor
    // is ~2.2 input samples per output sample) with generous slack; the render
    // callback re-checks and goes silent rather than ever allocating.
    const auto maxBlock = juce::jmax (monitorBlock, 2048);
    scratch.setSize (2, 4 * maxBlock + 64);
    discard.setSize (1, 4 * maxBlock);

    priming      = true;
    drainPending = true;
    fadeGain     = 0.0f;
    smoothedFill = 0.0;

    for (auto& resampler : resamplers)
        resampler.reset();

    active.store (true);
}

void MonitorOutput::audioDeviceStopped()
{
    active.store (false);
}

void MonitorOutput::audioDeviceIOCallbackWithContext (const float* const*, int,
                                                      float* const* output, int numOutputChannels,
                                                      int numSamples,
                                                      const juce::AudioIODeviceCallbackContext&)
{
    auto silence = [&]
    {
        for (int ch = 0; ch < numOutputChannels; ++ch)
            if (output[ch] != nullptr)
                juce::FloatVectorOperations::clear (output[ch], numSamples);
    };

    const auto srcRate = sourceRate.load (std::memory_order_relaxed);

    if (monitorRate <= 0.0 || srcRate <= 0.0 || numOutputChannels <= 0 || numSamples <= 0)
    {
        silence();
        return;
    }

    // First callback after a (re)start: throw away whatever an earlier run left
    // in the FIFO. Draining from the reader side is safe against a concurrent
    // writer, unlike AbstractFifo::reset().
    if (drainPending)
    {
        drainPending = false;
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);
        fifo.finishedRead (size1 + size2);
    }

    const auto target = targetFillSamples();
    const auto fill   = fifo.getNumReady();

    if (priming)
    {
        if (fill < target)
        {
            silence();
            return;
        }

        priming      = false;
        smoothedFill = (double) fill;
        fadeGain     = 0.0f;

        for (auto& resampler : resamplers)
            resampler.reset();
    }

    // Steer the resampling ratio to hold the FIFO at the target depth — this is
    // what absorbs the clock drift between the two devices. The fill level
    // jitters by a block either way, so it's smoothed hard, and the correction
    // is clamped to ±0.5%: dozens of times any real-world drift, far too small
    // to hear as pitch.
    smoothedFill += 0.02 * ((double) fill - smoothedFill);
    const auto error      = (smoothedFill - (double) target) / (double) target;
    const auto correction = juce::jlimit (-0.005, 0.005, 0.1 * error);
    const auto ratio      = (srcRate / monitorRate) * (1.0 + correction);

    const auto needed = (int) std::ceil (ratio * numSamples) + 8;

    if (fill < needed || needed > scratch.getNumSamples() || numSamples > discard.getNumSamples())
    {
        // Underrun — the mains stopped, stalled, or changed device. Go quiet and
        // refill to the target depth before resuming.
        silence();
        priming = true;
        return;
    }

    // Copy the (possibly wrapped) FIFO region into contiguous scratch for the
    // resamplers.
    int start1, size1, start2, size2;
    fifo.prepareToRead (needed, start1, size1, start2, size2);

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dst = scratch.getWritePointer (ch);
        juce::FloatVectorOperations::copy (dst, fifoBuffer.getReadPointer (ch, start1), size1);
        if (size2 > 0)
            juce::FloatVectorOperations::copy (dst + size1, fifoBuffer.getReadPointer (ch, start2), size2);
    }

    // Both resamplers advance from the same reset point with identical
    // (ratio, numSamples) calls, so they consume the same number of inputs.
    int consumed = 0;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto* dst = (ch < numOutputChannels && output[ch] != nullptr)
                        ? output[ch] : discard.getWritePointer (0);

        const auto used = resamplers[ch].process (ratio, scratch.getReadPointer (ch), dst, numSamples);

        if (ch == 0)
            consumed = used;
        else
            jassertquiet (used == consumed);
    }

    fifo.finishedRead (consumed);

    // Short fade-in coming out of priming, so resuming mid-signal doesn't click.
    if (fadeGain < 1.0f)
    {
        const auto step = (float) (1.0 / juce::jmax (1.0, 0.01 * monitorRate));   // ~10 ms ramp

        for (int ch = 0; ch < juce::jmin (2, numOutputChannels); ++ch)
        {
            if (output[ch] == nullptr)
                continue;

            auto gain = fadeGain;
            for (int i = 0; i < numSamples; ++i)
            {
                output[ch][i] *= gain;
                gain = juce::jmin (1.0f, gain + step);
            }
        }

        fadeGain = juce::jmin (1.0f, fadeGain + step * (float) numSamples);
    }

    // The device was opened with at most two channels, but be safe.
    for (int ch = 2; ch < numOutputChannels; ++ch)
        if (output[ch] != nullptr)
            juce::FloatVectorOperations::clear (output[ch], numSamples);
}

} // namespace play
