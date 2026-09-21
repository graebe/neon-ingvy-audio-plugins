#include "PluginProcessor.h"
#include "PluginEditor.h"

TranceGateProcessor::TranceGateProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    core = tg_core_create (44100.0);
}

TranceGateProcessor::~TranceGateProcessor()
{
    tg_core_destroy (core);
    core = nullptr;
}

void TranceGateProcessor::prepareToPlay (double sampleRate, int)
{
    const juce::ScopedLock sl (engineLock);
    tg_core_set_sample_rate (core, sampleRate);
}

bool TranceGateProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    /* Stereo in, stereo out. Mono would work -- the gate is a gain -- but the
     * engine's buffers are stereo throughout and a mono claim we do not test
     * is a claim that breaks in somebody's session, not in ours. */
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TranceGateProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int frames = buffer.getNumSamples();
    if (frames <= 0) return;

    /*
     * THE TRANSPORT, TRANSLATED.
     *
     * On Move this came from two host callbacks. Here it is the DAW's play
     * head, and the two fields that matter are the ones the engine's PLL
     * locks to: whether we are playing at all, and the song position in
     * BEATS. `ppqPosition` is already in beats, which is what
     * beats/beats_per_step expects -- no conversion, and none should be
     * invented.
     *
     * A host with no play head (offline render, some validators) reports
     * nothing. That is "stopped", which the engine reads as "hold the gate
     * open" -- the dry signal passes, which is the right failure.
     */
    tg_transport_t t {};
    t.running = 0;
    t.beats   = -1.0;
    t.bpm     = 120.0f;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm())
                t.bpm = static_cast<float> (*bpm);

            if (auto ppq = pos->getPpqPosition())
            {
                /* isPlaying() false with a valid position means the playhead
                 * is parked. The engine wants that as stopped, or a paused
                 * session would sit gating a frozen phase. */
                if (pos->getIsPlaying())
                {
                    t.running = 1;
                    t.beats   = *ppq;
                }
            }
        }
    }

    const juce::ScopedLock sl (engineLock);

    /* Non-interleaved is what JUCE hands us, and the engine has a path for it
     * that is bit-identical to the interleaved one. */
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
    tg_core_process_f32_split (core, L, R, frames, &t);
}

void TranceGateProcessor::engineSet (const juce::String& key, const juce::String& value)
{
    const juce::ScopedLock sl (engineLock);
    tg_core_set_param (core, key.toRawUTF8(), value.toRawUTF8());
}

juce::String TranceGateProcessor::engineGet (const juce::String& key) const
{
    const juce::ScopedLock sl (engineLock);
    /* Sized for the state blob, which is the longest thing the engine emits:
     * 8 slots x (8 hex steps + 8 hex ties + length + 32 x 2 hex depths). */
    std::vector<char> buf (2048, 0);
    const int n = tg_core_get_param (core, key.toRawUTF8(), buf.data(), (int) buf.size());
    if (n < 0) return {};
    return juce::String::fromUTF8 (buf.data());
}

bool TranceGateProcessor::patchFromString (const juce::String& s)
{
    const auto trimmed = s.trim();
    if (! trimmed.startsWith ("{")) return false;    /* not a patch blob */
    engineSet ("state", trimmed);
    return true;
}

/*
 * THE SAVED STATE IS THE MOVE PATCH, VERBATIM.
 *
 * Not a JUCE ValueTree wrapping our own idea of the parameters: the engine
 * already emits a complete, versioned patch as a string, and the Move module
 * parses the same one. Storing anything else would mean two serialisers for
 * one patch, and they would disagree about something eventually.
 */
void TranceGateProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto patch = patchToString();
    destData.replaceAll (patch.toRawUTF8(), patch.getNumBytesAsUTF8());
}

void TranceGateProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    patchFromString (juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes));
}

juce::AudioProcessorEditor* TranceGateProcessor::createEditor()
{
    return new TranceGateEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranceGateProcessor();
}
