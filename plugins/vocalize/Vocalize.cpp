// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's shell. Vocalize.h says what it does.
 */
#include "Vocalize.h"

#include "Editor.h"
#include "MachineSettings.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace ni::vz
{

namespace
{
/* One of the engine's texts for a parameter: vz_param_key, _name or _text. */
template <typename Read>
juce::String engineText (Read read)
{
    char buf[64] {};
    return read (buf, (int) sizeof buf) >= 0 ? juce::String::fromUTF8 (buf) : juce::String();
}

/*
 * A NUMBER ON THE ENGINE'S TRAVEL AND IN ITS WORDS. The range maps through
 * vz_param_normalise and _denormalise and lands on the table's resolution
 * (vz_param_snap): a value a host keeps as a float comes back as the number
 * that was set -- 1.3, not 1.29999995. The text is vz_param_text ("E>I 30 %"),
 * and typing is read by vz_param_parse -- which takes a vowel's letter, and
 * refuses text with no value in it, and then the value stays where it is.
 */
class EngineFloat final : public juce::AudioParameterFloat
{
public:
    EngineFloat (int i, const juce::String& key, const juce::String& title, const VzParamInfo& info)
        : juce::AudioParameterFloat (
              juce::ParameterID { key, 1 }, title,
              juce::NormalisableRange<float> (
                  (float) info.min, (float) info.max,
                  [i] (float, float, float n) { return (float) vz_param_denormalise (i, (double) n); },
                  [i] (float, float, float v) { return (float) vz_param_normalise (i, (double) v); },
                  [i] (float, float, float v) { return (float) vz_param_snap (i, (double) v); }),
              (float) info.default_value,
              juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                  [i] (float v, int) { return engineText ([i, v] (char* b, int n) { return vz_param_text (i, (double) v, b, n); }); })),
          index (i)
    {
    }

    float getValueForText (const juce::String& text) const override
    {
        double v = 0.0;
        if (! vz_param_parse (index, text.toRawUTF8(), &v))
            return convertTo0to1 (get());
        return convertTo0to1 ((float) v);
    }

private:
    const int index;
};

std::unique_ptr<juce::RangedAudioParameter> fromEngine (int index)
{
    VzParamInfo info {};
    const bool known = vz_param_info (index, &info);
    jassert (known);
    juce::ignoreUnused (known);

    const auto key = engineText ([index] (char* b, int n) { return vz_param_key (index, b, n); });
    const auto name = engineText ([index] (char* b, int n) { return vz_param_name (index, b, n); });
    if (info.kind == VzParamKind_Choice)
    {
        /* A choice's labels are the engine's; switching engines changes the
         * latency, which a host takes note of between blocks, so it is not
         * offered for automation. */
        juce::StringArray labels;
        for (int option = 0; option <= (int) info.max; ++option)
            labels.add (engineText ([index, option] (char* b, int n) { return vz_param_label (index, option, b, n); }));
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { key, 1 }, name, labels,
                                                             (int) info.default_value,
                                                             juce::AudioParameterChoiceAttributes().withAutomatable (false));
    }
    return std::make_unique<EngineFloat> (index, key, name, info);
}

bool monoOrStereo (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      shell (vz_shell_create (44100.0)),
      published (std::make_unique<VzPicture>())
{
    jassert (vz_param_count() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        auto p = fromEngine (i);
        params[(size_t) i] = p.get();
        addParameter (p.release());
    }
    for (auto& line : bypassLine)
        line.assign ((size_t) bypassLength, 0.0f);
    /* WAV and AIFF, and on macOS the system's decoders: the codecs JUCE would
     * add are switched off (plugins/vocalize/CMakeLists.txt). */
    formats.registerBasicFormats();
    setLatencySamples (latencyForMode());
    startTimer (latencyCheckMs);
}

Processor::~Processor()
{
    stopTimer();
    /* A capture under way reads `formats`: it ends before they go. */
    captures.removeAllJobs (true, 30000);
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return monoOrStereo (in) && layout.getMainOutputChannelSet() == in;
}

void Processor::prepareToPlay (double sampleRate, int)
{
    const double r = sampleRate > 0.0 ? sampleRate : 44100.0;
    rate.store (r);
    vz_shell_post_sample_rate (shell.get(), r);
    /* A host that deactivated the plugin and starts it again hears nothing
     * held from before: the history and the resonators let go. */
    vz_shell_post_reset (shell.get());
    setLatencySamples (latencyForMode());
    for (auto& line : bypassLine)
        std::fill (line.begin(), line.end(), 0.0f);
    bypassPos = 0;
    beat.prepare (r);
}

/* Snapped again in double: the float a parameter holds is the nearest to a
 * value on the resolution, and this is that value. */
double Processor::plain (int index) const
{
    const auto* p = params[(size_t) index];
    return vz_param_snap (index, (double) p->convertFrom0to1 (p->getValue()));
}

int Processor::latencyForMode() const
{
    return (int) vz_latency (plain (param::mode), rate.load());
}

/* The message thread's: a host told only when the latency moved. */
void Processor::timerCallback()
{
    if (const int want = latencyForMode(); want != getLatencySamples())
        setLatencySamples (want);
}

void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    if (frames <= 0 || buffer.getNumChannels() < 1)
        return;

    VzCore* core = vz_shell_begin (shell.get());
    for (int i = 0; i < paramCount; ++i)
        vz_core_set_num (core, i, plain (i));
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    vz_core_process_f32 (core, left, right, (uint32_t) frames);
    vz_shell_end (shell.get(), (uint32_t) frames);
}

void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int frames = buffer.getNumSamples();
    beat.tick (readClock (getPlayHead()), frames);
    const int latency = getLatencySamples();
    if (latency <= 0)
    {
        ni::Processor::processBypassed (buffer, midi);
        return;
    }
    /* Each channel through its own line, `latency` samples late; the mask
     * wraps, the line being a power of two longer than any latency. */
    constexpr int mask = bypassLength - 1;
    const int channels = juce::jmin (buffer.getNumChannels(), (int) bypassLine.size());
    for (int ch = 0; ch < channels; ++ch)
    {
        auto& line = bypassLine[(size_t) ch];
        float* x = buffer.getWritePointer (ch);
        int pos = bypassPos;
        for (int i = 0; i < frames; ++i, pos = (pos + 1) & mask)
        {
            line[(size_t) pos] = x[i];
            x[i] = line[(size_t) ((pos - latency) & mask)];
        }
    }
    bypassPos = (bypassPos + frames) & mask;
}

/* ------------------------------------------------------------ the model -- */

int Processor::takeRings (float* strengths, int capacity)
{
    return beat.takeRings (strengths, capacity);
}

bool Processor::motion() const
{
    return ni::MachineSettings::motion ("vocalize");
}

void Processor::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("vocalize", on);
}

bool Processor::bipolar (int index) const
{
    VzParamInfo info {};
    return vz_param_info (index, &info) && info.bipolar;
}

Formants Processor::formants (double vowel)
{
    std::optional<VzVoice> v;
    {
        const juce::ScopedLock lock (voiceLock);
        v = held;
    }
    Formants f {};
    vz_formants (v ? &*v : nullptr, plain (param::voice), vowel, plain (param::size), plain (param::resonance), f.data());
    return f;
}

/* ------------------------------------------------------------ the voice -- */

Voice Processor::voice() const
{
    const juce::ScopedLock lock (voiceLock);
    return state;
}

juce::String Processor::recordingWildcard() const
{
    return formats.getWildcardForAllFormats();
}

void Processor::capture (const juce::File& recording)
{
    {
        const juce::ScopedLock lock (voiceLock);
        if (state.status == Voice::Status::capturing)
            return;
        state.status = Voice::Status::capturing;
        state.name = recording.getFileName();
        ++state.serial;
    }
    /* Read, downmixed and analysed on the capture's thread; the answer goes
     * back to the message thread, to a processor that may be gone by then. */
    captures.addJob ([this, recording, self = juce::WeakReference<Processor> (this)]
    {
        const auto name = recording.getFileName();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (recording));
        auto answer = [self, name] (std::optional<VzVoice> v, Voice::Status status)
        {
            juce::MessageManager::callAsync ([self, name, v, status]
            {
                if (auto* p = self.get())
                    p->captured (v, status, name);
            });
        };
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels == 0)
            return answer (std::nullopt, Voice::Status::unreadable);

        const auto frames = (int) juce::jmin (reader->lengthInSamples,
                                              (juce::int64) (longestRecordingS * reader->sampleRate));
        const int channels = (int) reader->numChannels;
        juce::AudioBuffer<float> chunk (channels, 65536);
        std::vector<float> mono;
        mono.reserve ((size_t) juce::jmax (0, frames));
        for (int at = 0; at < frames; at += chunk.getNumSamples())
        {
            const int n = juce::jmin (chunk.getNumSamples(), frames - at);
            if (! reader->read (&chunk, 0, n, at, true, true))
                return answer (std::nullopt, Voice::Status::unreadable);
            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;
                for (int ch = 0; ch < channels; ++ch)
                    sum += chunk.getSample (ch, i);
                mono.push_back (sum / (float) channels);
            }
        }
        VzVoice v {};
        if (vz_voice_capture (mono.data(), mono.size(), reader->sampleRate, &v))
            return answer (v, Voice::Status::captured);
        answer (std::nullopt, Voice::Status::unsung);
    });
}

/* A capture that failed changes what is said of the latest capture, never
 * what is sung: the voice before it, if any, stays, under its own name. */
void Processor::captured (std::optional<VzVoice> v, Voice::Status status, const juce::String& name)
{
    if (v)
    {
        useVoice (&*v, name, true);
        return;
    }
    const juce::ScopedLock lock (voiceLock);
    state.status = status;
    state.name = name;
    ++state.serial;
}

void Processor::forgetVoice()
{
    useVoice (nullptr, {}, true);
}

void Processor::useVoice (const VzVoice* v, const juce::String& name, bool byHand)
{
    {
        const juce::ScopedLock lock (voiceLock);
        held = v != nullptr ? std::optional<VzVoice> (*v) : std::nullopt;
        state.status = v != nullptr ? Voice::Status::captured : Voice::Status::none;
        state.name = v != nullptr ? name : juce::String();
        state.sung = v != nullptr;
        state.sungName = state.name;
        for (size_t i = 0; i < state.found.size(); ++i)
            state.found[i] = v != nullptr && v->found[i];
        ++state.serial;
        vz_shell_post_voice (shell.get(), v);
    }
    if (byHand)
        nonParameterStateChanged();
}

/*
 * <voice name="take3.wav" scale="1.17">
 *   <vowel name="A" found="1" frames="48" freq="... five Hz ..." bandwidth="..."/>
 *   ... E I O U
 * </voice>
 *
 * Fifty numbers and what the capture found, never the recording.
 */
void Processor::writeExtraState (juce::XmlElement& root)
{
    const juce::ScopedLock lock (voiceLock);
    if (! held)
        return;
    auto* e = root.createNewChildElement ("voice");
    e->setAttribute ("name", state.sungName);
    e->setAttribute ("scale", held->scale);
    static const char* const names[] = { "A", "E", "I", "O", "U" };
    for (int vowel = 0; vowel < 5; ++vowel)
    {
        auto* row = e->createNewChildElement ("vowel");
        row->setAttribute ("name", names[vowel]);
        row->setAttribute ("found", held->found[vowel] ? 1 : 0);
        row->setAttribute ("frames", (int) held->frames[vowel]);
        juce::StringArray freq, width;
        for (int k = 0; k < VZ_FORMANTS; ++k)
        {
            freq.add (juce::String (held->freq[5 * vowel + k], 6));
            width.add (juce::String (held->bandwidth[5 * vowel + k], 6));
        }
        row->setAttribute ("freq", freq.joinIntoString (" "));
        row->setAttribute ("bandwidth", width.joinIntoString (" "));
    }
}

/* A set without a voice, or with one that is not a voice, sings the
 * textbook's: what was captured before it was read is forgotten either way. */
void Processor::readExtraState (const juce::XmlElement& root)
{
    const auto* e = root.getChildByName ("voice");
    if (e == nullptr)
        return useVoice (nullptr, {}, false);
    VzVoice v {};
    vz_voice_textbook (&v);
    v.scale = e->getDoubleAttribute ("scale", 1.0);
    int vowel = 0;
    for (const auto* row : e->getChildWithTagNameIterator ("vowel"))
    {
        if (vowel == 5)
            break;
        const auto freq = juce::StringArray::fromTokens (row->getStringAttribute ("freq"), " ", "");
        const auto width = juce::StringArray::fromTokens (row->getStringAttribute ("bandwidth"), " ", "");
        /* Numbers as written here, nothing else: a word would read as 0, and
         * a voice of zeros is no voice. */
        const auto numbers = [] (const juce::StringArray& tokens)
        {
            return tokens.size() == VZ_FORMANTS
                   && std::all_of (tokens.begin(), tokens.end(), [] (const juce::String& t)
                                   { return t.containsOnly ("0123456789.+-eE") && t.containsAnyOf ("0123456789"); });
        };
        if (! numbers (freq) || ! numbers (width))
            return useVoice (nullptr, {}, false);
        for (int k = 0; k < VZ_FORMANTS; ++k)
        {
            v.freq[5 * vowel + k] = freq[k].getDoubleValue();
            v.bandwidth[5 * vowel + k] = width[k].getDoubleValue();
        }
        v.found[vowel] = row->getIntAttribute ("found") != 0;
        v.frames[vowel] = (uint32_t) juce::jmax (0, row->getIntAttribute ("frames"));
        ++vowel;
    }
    if (vowel != 5 || ! vz_voice_check (&v))
        return useVoice (nullptr, {}, false);
    useVoice (&v, e->getStringAttribute ("name"), false);
}

const Picture& Processor::picture()
{
    if (! vz_shell_read (shell.get(), published.get()) || published->serial == latest.serial)
        return latest;
    latest.serial = published->serial;
    latest.spectral = published->mode == 1;
    latest.latency = (int) published->latency;
    latest.sampleRate = published->sample_rate;
    latest.hz.assign (published->hz, published->hz + VZ_POINTS);
    latest.input.assign (published->input_db, published->input_db + VZ_POINTS);
    latest.output.assign (published->output_db, published->output_db + VZ_POINTS);
    return latest;
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Editor> (*this), ni::PluginEditor::FollowDesign {});
}

void Processor::editorOpened()
{
    beat.setActive (true);
}

void Processor::editorClosed()
{
    beat.setActive (false);
}

} // namespace ni::vz

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::vz::Processor();
}
