// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Vocalize's shell. Vocalize.h says what it does.
 */
#include "Vocalize.h"

#include "Editor.h"
#include "EngineParameter.h"
#include "MachineSettings.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace ni::vz
{

namespace
{
/* The engine's table, as the shared adapter reads it (EngineParameter.h):
 * every number on the engine's travel and in its words, every choice of its
 * labels, automatable as the table says -- Mode not, since switching engines
 * changes the latency, which a host takes note of between blocks. */
struct Table
{
    using Info = NiParamInfo;
    static bool info (int i, Info* out) { return vz_param_info (i, out); }
    static bool isChoice (const Info& i) { return i.kind == NiParamKind_Choice; }
    static int key (int i, char* b, int n) { return vz_param_key (i, b, n); }
    static int name (int i, char* b, int n) { return vz_param_name (i, b, n); }
    static int label (int i, int option, char* b, int n) { return vz_param_label (i, option, b, n); }
    static int text (int i, double v, char* b, int n) { return vz_param_text (i, v, b, n); }
    static bool parse (int i, const char* t, double* out) { return vz_param_parse (i, t, out); }
    static double normalise (int i, double v) { return vz_param_normalise (i, v); }
    static double denormalise (int i, double n) { return vz_param_denormalise (i, n); }
    static double snap (int i, double v) { return vz_param_snap (i, v); }
};

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
        auto p = ni::engineParameter<Table> (i);
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
    /* A capture under way reads `formats`: it is called off -- the analysis
     * within a frame, the read between chunks -- and waited for before they
     * go. */
    closing.store (true);
    {
        const juce::ScopedLock lock (voiceLock);
        if (running != nullptr)
            vz_capture_cancel (running.get());
    }
    if (captures != nullptr)
        captures->removeAllJobs (true, 30000);
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
    if (resumeNext)
    {
        vz_core_resume (core);
        resumeNext = false;
    }
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    vz_core_process_f32 (core, left, right, (uint32_t) frames);
    vz_shell_end (shell.get(), (uint32_t) frames);
}

void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int frames = buffer.getNumSamples();
    beat.tick (readClock (getPlayHead()), frames);
    /* The engine listens on, so the block after the bypass resumes from the
     * sound it missed. */
    if (frames > 0 && buffer.getNumChannels() > 0)
    {
        VzCore* core = vz_shell_begin (shell.get());
        for (int i = 0; i < paramCount; ++i)
            vz_core_set_num (core, i, plain (i));
        vz_core_listen_f32 (core, buffer.getReadPointer (0),
                            buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : nullptr, (uint32_t) frames);
        vz_shell_end (shell.get(), (uint32_t) frames);
        resumeNext = true;
    }
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
    NiParamInfo info {};
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
    auto job = std::shared_ptr<VzCapture> (vz_capture_new(), vz_capture_free);
    std::uint32_t startedIn = 0;
    {
        const juce::ScopedLock lock (voiceLock);
        if (state.status == Voice::Status::capturing)
            return;
        state.status = Voice::Status::capturing;
        state.name = recording.getFileName();
        ++state.serial;
        startedIn = generation;
        running = job;
    }
    if (captures == nullptr)
        captures = std::make_unique<juce::ThreadPool> (
            juce::ThreadPoolOptions {}.withThreadName ("NI Vocalize capture").withNumberOfThreads (1));

    /* Read, downmixed and analysed on the capture's thread; the answer goes
     * back to the message thread, to a processor that may be gone by then. */
    captures->addJob ([this, recording, job, startedIn, self = juce::WeakReference<Processor> (this)]
    {
        const auto name = recording.getFileName();
        auto answer = [self, name, startedIn] (std::optional<VzVoice> v, Voice::Status status)
        {
            juce::MessageManager::callAsync ([self, name, v, status, startedIn]
            {
                if (auto* p = self.get())
                    p->captured (v, status, name, startedIn);
            });
        };
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (recording));
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
            if (closing.load())
                return;
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
        switch (vz_capture_run (job.get(), mono.data(), mono.size(), reader->sampleRate, &v))
        {
            case VzCaptured_Voice:     return answer (v, Voice::Status::captured);
            case VzCaptured_Unsung:    return answer (std::nullopt, Voice::Status::unsung);
            case VzCaptured_NoVowels:  return answer (std::nullopt, Voice::Status::noVowels);
            case VzCaptured_Cancelled: return;
        }
    });
}

/* A capture that failed changes what is said of the latest capture, never
 * what is sung: the voice before it, if any, stays, under its own name. A
 * capture started before the last set was read answers for a set that is
 * gone, and is dropped. */
void Processor::captured (std::optional<VzVoice> v, Voice::Status status, const juce::String& name,
                          std::uint32_t startedIn)
{
    {
        const juce::ScopedLock lock (voiceLock);
        if (startedIn != generation)
            return;
        running.reset();
    }
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
    {
        const juce::ScopedLock lock (voiceLock);
        if (held)
        {
            forgotten = held;
            forgottenName = state.sungName;
        }
    }
    useVoice (nullptr, {}, true);
}

void Processor::restoreVoice()
{
    std::optional<VzVoice> back;
    juce::String name;
    {
        const juce::ScopedLock lock (voiceLock);
        if (! forgotten || held)
            return;
        back = forgotten;
        name = forgottenName;
    }
    useVoice (&*back, name, true);
}

void Processor::useVoice (const VzVoice* v, const juce::String& name, bool byHand)
{
    {
        const juce::ScopedLock lock (voiceLock);
        held = v != nullptr ? std::optional<VzVoice> (*v) : std::nullopt;
        if (v != nullptr)
            forgotten.reset();
        state.status = v != nullptr ? Voice::Status::captured : Voice::Status::none;
        state.name = v != nullptr ? name : juce::String();
        state.sung = v != nullptr;
        state.sungName = state.name;
        for (size_t i = 0; i < state.found.size(); ++i)
        {
            state.found[i] = v != nullptr && v->found[i];
            state.confidence[i] = v != nullptr ? v->confidence[i] : 0.0;
        }
        state.restorable = forgotten.has_value() && v == nullptr;
        ++state.serial;
        vz_shell_post_voice (shell.get(), v);
    }
    if (byHand)
        nonParameterStateChanged();
}

/*
 * <voice format="1" name="take3.wav" scale="1.17" order="1">
 *   <vowel name="A" found="1" frames="48" confidence="0.82"
 *          freq="... five Hz ..." bandwidth="..."/>
 *   ... E I O U
 * </voice>
 *
 * Fifty numbers and what the capture found, never the recording. `format`
 * says which shape this is: a build that meets a newer one refuses it rather
 * than misread it.
 */
static constexpr int voiceFormat = 1;

void Processor::writeExtraState (juce::XmlElement& root)
{
    const juce::ScopedLock lock (voiceLock);
    if (! held)
        return;
    auto* e = root.createNewChildElement ("voice");
    e->setAttribute ("format", voiceFormat);
    e->setAttribute ("name", state.sungName);
    e->setAttribute ("scale", held->scale);
    e->setAttribute ("order", held->by_order ? 1 : 0);
    static const char* const names[] = { "A", "E", "I", "O", "U" };
    for (int vowel = 0; vowel < 5; ++vowel)
    {
        auto* row = e->createNewChildElement ("vowel");
        row->setAttribute ("name", names[vowel]);
        row->setAttribute ("found", held->found[vowel] ? 1 : 0);
        row->setAttribute ("frames", (int) held->frames[vowel]);
        row->setAttribute ("confidence", juce::String (held->confidence[vowel], 3));
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

/* A set without a voice, with one that is not a voice, or with one of a
 * format newer than this build reads, sings the textbook's: what was captured
 * before it was read is forgotten either way, and a capture under way answers
 * for a set that is gone. */
void Processor::readExtraState (const juce::XmlElement& root)
{
    {
        const juce::ScopedLock lock (voiceLock);
        ++generation;
        forgotten.reset();
        /* Its answer would be dropped: it need not finish. */
        if (running != nullptr)
            vz_capture_cancel (running.get());
        running.reset();
    }
    const auto* e = root.getChildByName ("voice");
    if (e == nullptr || e->getIntAttribute ("format", voiceFormat) > voiceFormat)
        return useVoice (nullptr, {}, false);
    VzVoice v {};
    vz_voice_textbook (&v);
    v.scale = e->getDoubleAttribute ("scale", 1.0);
    v.by_order = e->getIntAttribute ("order") != 0;
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
        v.confidence[vowel] = row->getDoubleAttribute ("confidence", 0.0);
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
    vz_shell_post_watching (shell.get(), true);
}

void Processor::editorClosed()
{
    beat.setActive (false);
    vz_shell_post_watching (shell.get(), false);
}

} // namespace ni::vz

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::vz::Processor();
}
