// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * A single filter's shell. SingleFilter.h says what it does.
 */
#include "SingleFilter.h"

#include "Editor.h"
#include "EngineParameter.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ni::sf
{

namespace
{
/* Product `S`'s table, as the shared adapter reads it (EngineParameter.h). */
template <int S>
struct Table
{
    using Info = NiParamInfo;
    static bool info (int i, Info* out) { return sf_param_info (S, i, out); }
    static bool isChoice (const Info& i) { return i.kind == NiParamKind_Choice; }
    static int key (int i, char* b, int n) { return sf_param_key (S, i, b, n); }
    static int name (int i, char* b, int n) { return sf_param_name (S, i, b, n); }
    static int label (int i, int option, char* b, int n) { return sf_param_label (S, i, option, b, n); }
    static int text (int i, double v, char* b, int n) { return sf_param_text (S, i, v, b, n); }
    static bool parse (int i, const char* t, double* out) { return sf_param_parse (S, i, t, out); }
    static double normalise (int i, double v) { return sf_param_normalise (S, i, v); }
    static double denormalise (int i, double n) { return sf_param_denormalise (S, i, n); }
    static double snap (int i, double v) { return sf_param_snap (S, i, v); }
};

template <int S>
std::unique_ptr<juce::RangedAudioParameter> make (int index)
{
    return ni::engineParameter<Table<S>> (index);
}

/* Each product's maker, by its shape. */
using Maker = std::unique_ptr<juce::RangedAudioParameter> (*) (int);
constexpr std::array<Maker, 10> makers { nullptr, &make<1>, &make<2>, &make<3>, &make<4>, &make<5>, &make<6>, &make<7>, &make<8>, &make<9> };

bool monoOrStereo (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}

static_assert (Picture::points == SF_POINTS);
static_assert (field::count == SF_FIELDS && field::analyzer == SF_FIELD_ANALYZER && field::freq == SF_FIELD_FREQ);
static_assert (shape::bell == SF_SHAPE_BELL && shape::tilt == SF_SHAPE_TILT && shape::allPass == SF_SHAPE_ALL_PASS);
static_assert (uses::q == SF_USES_Q && uses::ripple == SF_USES_RIPPLE && uses::stopband == SF_USES_STOPBAND);
} // namespace

Processor::Processor (int s)
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      product (s),
      count (sf_param_count ((uint32_t) s)),
      shell (sf_shell_create ((uint32_t) s, 44100.0)),
      published (std::make_unique<SfPicture>())
{
    jassert (shell != nullptr && count > 0 && count <= (int) SF_PARAM_MAX);
    for (int i = 0; i < count; ++i)
    {
        auto p = makers[(size_t) s](i);
        params[(size_t) i] = p.get();
        addParameter (p.release());
    }
    /* LAST, so the engine's keep their indices and IDs. */
    auto b = std::make_unique<ni::Parameter> (ni::bypassSpec());
    bypass = b.get();
    addParameter (b.release());
    sf_picture_init (published.get());
}

Processor::~Processor() = default;

juce::String Processor::productName() const
{
    static const char* const names[] = { "", "Bell", "Low Shelf", "High Shelf", "High Pass", "Low Pass", "Band Pass", "Notch", "All Pass", "Tilt" };
    return names[juce::jlimit (0, 9, product)];
}

int Processor::indexOf (int f) const
{
    return sf_field_index ((uint32_t) product, (uint32_t) f);
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return monoOrStereo (in) && layout.getMainOutputChannelSet() == in;
}

void Processor::prepare (double sampleRate, int maximumBlockSize)
{
    for (auto& copy : listening)
        copy.assign ((size_t) juce::jmax (1, maximumBlockSize), 0.0f);
    const double r = sampleRate > 0.0 ? sampleRate : 44100.0;
    rate.store (r);
    /* Every parameter again at the first block, and the bypass counted from
     * there. */
    pushed.fill (std::numeric_limits<float>::quiet_NaN());
    idleAfter = (std::int64_t) std::ceil (r * idleAfterSeconds);
    bypassedFor = 0;
    idle = false;
    sf_shell_post_sample_rate (shell.get(), r);
    /* A host that deactivated the plugin and starts it again hears nothing
     * held from before. */
    sf_shell_post_reset (shell.get());
}

/* Snapped again in double: the float a parameter holds is the nearest to a
 * value on the resolution, and this is that value. */
double Processor::plain (int index) const
{
    const auto* p = params[(size_t) index];
    return sf_param_snap ((uint32_t) product, index, (double) p->convertFrom0to1 (p->getValue()));
}

std::array<double, SF_PARAM_MAX> Processor::values() const
{
    std::array<double, SF_PARAM_MAX> v {};
    for (int i = 0; i < count; ++i)
        v[(size_t) i] = plain (i);
    return v;
}

void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    run (buffer, true);
}

void Processor::run (juce::AudioBuffer<float>& buffer, bool heard)
{
    const int frames = buffer.getNumSamples();
    if (frames <= 0 || buffer.getNumChannels() < 1)
        return;
    SfCore* core = sf_shell_begin (shell.get());
    push (core);
    const bool stereo = buffer.getNumChannels() > 1;
    if (heard)
    {
        /* Back from idling: from silence, under the Bypass's fade. */
        if (idle)
            sf_core_reset (core);
        idle = false;
        bypassedFor = 0;
        sf_core_process_f32 (core, buffer.getWritePointer (0), stereo ? buffer.getWritePointer (1) : nullptr, (uint32_t) frames);
    }
    else
    {
        idle = idle || bypassedFor >= idleAfter;
        if (idle)
        {
            /* Idle: the engine hears the input, for the analyzer, and
             * filters nothing; the audio passes untouched. */
            sf_core_listen_f32 (core, buffer.getWritePointer (0), stereo ? buffer.getWritePointer (1) : nullptr, (uint32_t) frames);
        }
        else
        {
            bypassedFor += frames;
            /* On a copy, in pieces the size the host announced: the output
             * is the input, the engine hears it all. */
            const int room = (int) listening[0].size();
            for (int at = 0; at < frames; at += room)
            {
                const int n = juce::jmin (room, frames - at);
                std::copy_n (buffer.getReadPointer (0, at), n, listening[0].data());
                if (stereo)
                    std::copy_n (buffer.getReadPointer (1, at), n, listening[1].data());
                sf_core_process_f32 (core, listening[0].data(), stereo ? listening[1].data() : nullptr, (uint32_t) n);
            }
        }
    }
    sf_shell_end (shell.get(), (uint32_t) frames);
}

/* Bypassed, by the host or by the Bypass: the audio passes as JUCE passes it
 * (no latency to keep) while the engine runs on a copy, then idles. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    run (buffer, false);
    ni::Processor::processBypassed (buffer, midi);
}

/* Only what moved: a parameter's 0..1 value is one atomic read, the engine's
 * setter a call across the C ABI with the table's arithmetic behind it. */
void Processor::push (SfCore* core)
{
    for (int i = 0; i < count; ++i)
    {
        const float v = params[(size_t) i]->getValue();
        if (! juce::exactlyEqual (v, pushed[(size_t) i]))
        {
            pushed[(size_t) i] = v;
            sf_core_set_num (core, i, plain (i));
        }
    }
}

/* ------------------------------------------------------------ the model -- */

bool Processor::bipolar (int index) const
{
    NiParamInfo info {};
    return sf_param_info ((uint32_t) product, index, &info) && info.bipolar;
}

double Processor::getTailLengthSeconds() const
{
    const auto v = values();
    return sf_tail_seconds ((uint32_t) product, v.data(), (size_t) count, rate.load());
}

void Processor::watch()
{
    const int analyzer = indexOf (field::analyzer);
    const bool want = windowOpen && analyzer >= 0 && (int) plain (analyzer) != 0;
    if (want != watching)
    {
        watching = want;
        sf_shell_post_watching (shell.get(), want);
    }
}

const Picture& Processor::picture()
{
    watch();
    const auto v = values();
    if (! sf_shell_read (shell.get(), v.data(), (size_t) count, published.get()))
        return latest;
    const auto& p = *published;
    latest.serial = p.serial;
    latest.sampleRate = p.sample_rate;
    latest.analyzer = p.analyzer;
    latest.clamped = p.clamped;
    latest.handleHz = p.handle_hz;
    latest.handleDb = p.handle_db;
    latest.edgeHz = { p.edge_hz[0], p.edge_hz[1] };
    std::copy (std::begin (p.hz), std::end (p.hz), latest.hz.begin());
    std::copy (std::begin (p.curve_db), std::end (p.curve_db), latest.curve.begin());
    std::copy (std::begin (p.total_db), std::end (p.total_db), latest.total.begin());
    std::copy (std::begin (p.phase_deg), std::end (p.phase_deg), latest.phase.begin());
    std::copy (std::begin (p.pre_db), std::end (p.pre_db), latest.pre.begin());
    std::copy (std::begin (p.post_db), std::end (p.post_db), latest.post.begin());
    return latest;
}

unsigned Processor::uses (int order, int family) const
{
    return sf_uses ((uint32_t) product, order, family);
}

int Processor::orderPlayed (int family, int order) const
{
    return sf_order_played ((uint32_t) product, family, order);
}

double Processor::qForEdge (double freq, double edgeHz) const
{
    return sf_q_for_edge (freq, edgeHz);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Editor> (*this), ni::PluginEditor::FollowDesign {});
}

void Processor::editorOpened()
{
    windowOpen = true;
    watch();
}

void Processor::editorClosed()
{
    windowOpen = false;
    watch();
}

} // namespace ni::sf
