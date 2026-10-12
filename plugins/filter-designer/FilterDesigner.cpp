// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Torben Gräber

/*
 * NI Filter Designer's shell. FilterDesigner.h says what it does.
 */
#include "FilterDesigner.h"

#include "Editor.h"
#include "EngineParameter.h"
#include "MachineSettings.h"
#include "PluginEditor.h"

#include <algorithm>

namespace ni::fd
{

namespace
{
/* The engine's table, as the shared adapter reads it (EngineParameter.h). */
struct Table
{
    using Info = NiParamInfo;
    static bool info (int i, Info* out) { return fd_param_info (i, out); }
    static bool isChoice (const Info& i) { return i.kind == NiParamKind_Choice; }
    static int key (int i, char* b, int n) { return fd_param_key (i, b, n); }
    static int name (int i, char* b, int n) { return fd_param_name (i, b, n); }
    static int label (int i, int option, char* b, int n) { return fd_param_label (i, option, b, n); }
    static int text (int i, double v, char* b, int n) { return fd_param_text (i, v, b, n); }
    static bool parse (int i, const char* t, double* out) { return fd_param_parse (i, t, out); }
    static double normalise (int i, double v) { return fd_param_normalise (i, v); }
    static double denormalise (int i, double n) { return fd_param_denormalise (i, n); }
    static double snap (int i, double v) { return fd_param_snap (i, v); }
};

bool monoOrStereo (const juce::AudioChannelSet& set)
{
    return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
}

static_assert (param::count == FD_PARAM_COUNT);
static_assert (Picture::points == FD_POINTS);
static_assert (param::nodes == FD_NODES);
} // namespace

Processor::Processor()
    : ni::Processor (BusesProperties()
                         .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      shell (fd_shell_create (44100.0)),
      published (std::make_unique<FdPicture>())
{
    jassert (fd_param_count() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        auto p = ni::engineParameter<Table> (i);
        params[(size_t) i] = p.get();
        addParameter (p.release());
    }
    fd_picture_init (published.get());
}

Processor::~Processor() = default;

bool Processor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto in = layout.getMainInputChannelSet();
    return monoOrStereo (in) && layout.getMainOutputChannelSet() == in;
}

void Processor::prepare (double sampleRate, int)
{
    const double r = sampleRate > 0.0 ? sampleRate : 44100.0;
    fd_shell_post_sample_rate (shell.get(), r);
    /* A host that deactivated the plugin and starts it again hears nothing
     * held from before. */
    fd_shell_post_reset (shell.get());
    beat.prepare (r);
}

/* Snapped again in double: the float a parameter holds is the nearest to a
 * value on the resolution, and this is that value. */
double Processor::plain (int index) const
{
    const auto* p = params[(size_t) index];
    return fd_param_snap (index, (double) p->convertFrom0to1 (p->getValue()));
}

void Processor::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int frames = buffer.getNumSamples();
    const auto clock = readClock (getPlayHead());
    beat.tick (clock, frames);
    if (frames <= 0 || buffer.getNumChannels() < 1)
        return;

    FdCore* core = fd_shell_begin (shell.get());
    for (int i = 0; i < paramCount; ++i)
        fd_core_set_num (core, i, plain (i));
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    fd_core_process_f32 (core, left, right, (uint32_t) frames);
    fd_shell_end (shell.get(), (uint32_t) frames);
}

/* Bypassed, the audio passes as JUCE passes it (no latency to keep) and the
 * Ground keeps the host's time. */
void Processor::processBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    beat.tick (readClock (getPlayHead()), buffer.getNumSamples());
    ni::Processor::processBypassed (buffer, midi);
}

/* ------------------------------------------------------------ the model -- */

int Processor::takeRings (float* strengths, int capacity)
{
    return beat.takeRings (strengths, capacity);
}

bool Processor::motion() const
{
    return ni::MachineSettings::motion ("filter-designer");
}

void Processor::setMotion (bool on)
{
    ni::MachineSettings::setMotion ("filter-designer", on);
}

bool Processor::bipolar (int index) const
{
    NiParamInfo info {};
    return fd_param_info (index, &info) && info.bipolar;
}

const Picture& Processor::picture (int lane)
{
    std::array<double, paramCount> values {};
    for (int i = 0; i < paramCount; ++i)
        values[(size_t) i] = plain (i);
    if (! fd_shell_read (shell.get(), values.data(), values.size(), (uint32_t) lane, published.get()))
        return latest;
    const auto& p = *published;
    latest.serial = p.serial;
    latest.sampleRate = p.sample_rate;
    latest.analyzer = p.analyzer;
    std::copy (std::begin (p.hz), std::end (p.hz), latest.hz.begin());
    for (int n = 0; n < param::nodes; ++n)
    {
        std::copy (std::begin (p.node_db[n]), std::end (p.node_db[n]), latest.node[(size_t) n].begin());
        latest.clamped[(size_t) n] = p.node_clamped[n];
        latest.handleHz[(size_t) n] = p.handle_hz[n];
        latest.handleDb[(size_t) n] = p.handle_db[n];
        latest.edgeHz[(size_t) n] = { p.edge_hz[n][0], p.edge_hz[n][1] };
    }
    std::copy (std::begin (p.total_db), std::end (p.total_db), latest.total.begin());
    for (int b = 0; b < 3; ++b)
        std::copy (std::begin (p.iso_db[b]), std::end (p.iso_db[b]), latest.iso[(size_t) b].begin());
    std::copy (std::begin (p.iso_total_db), std::end (p.iso_total_db), latest.isoTotal.begin());
    std::copy (std::begin (p.pre_db), std::end (p.pre_db), latest.pre.begin());
    std::copy (std::begin (p.post_db), std::end (p.post_db), latest.post.begin());
    return latest;
}

unsigned Processor::typeUses (int type, int order) const
{
    return fd_type_uses (type, order);
}

int Processor::typeMaxOrder (int type) const
{
    return fd_type_max_order (type);
}

bool Processor::familyUsesRipple (int family) const
{
    return fd_family_uses_ripple (family);
}

bool Processor::familyUsesStopband (int family) const
{
    return fd_family_uses_stopband (family);
}

double Processor::qForEdge (double freq, double edgeHz) const
{
    return fd_q_for_edge (freq, edgeHz);
}

int Processor::typeForNew (double hz) const
{
    return fd_type_for_new (hz);
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new ni::PluginEditor (*this, std::make_unique<Editor> (*this), ni::PluginEditor::FollowDesign {});
}

void Processor::editorOpened()
{
    beat.setActive (true);
    fd_shell_post_watching (shell.get(), true);
}

void Processor::editorClosed()
{
    beat.setActive (false);
    fd_shell_post_watching (shell.get(), false);
}

} // namespace ni::fd

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ni::fd::Processor();
}
