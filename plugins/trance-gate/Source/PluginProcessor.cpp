#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace {
/* The rate table, in the engine's order. These ARE the wire values -- the
 * engine matches rate by label -- so the two lists must not drift; the test
 * asserts every name here is accepted by the engine. */
const juce::StringArray kRateNames {
    "1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T",
    "1/16","1/16T","1/32","1/32T","1/64","1/128" };
constexpr int kRateDefault = 7;          /* 1/16, matching TG_RATE_DEFAULT */
constexpr int kMaxSteps = 128;

/*
 * A STEP COUNT IS NOT A SWEEP.
 *
 * AudioParameterBool and AudioParameterChoice override isDiscrete();
 * AudioParameterInt does NOT, and the base returns false. Every wrapper reads
 * that: the AU publishes such a parameter as a continuous 0..1 control and
 * adds kAudioUnitParameterFlag_CanRamp, so a host is entitled to RAMP Length
 * -- to slide through the step counts sample by sample on the way to the
 * value you asked for. Measured, not assumed: auval reported "Can Ramp" on
 * Length and Slot and "Indexed" on Rate, which is the same fact from the
 * other side.
 *
 * Length has 128 legal values and nothing in between them. Saying so costs
 * one override and is the difference between a host drawing 128 detents and
 * a host drawing a fader.
 */
struct DiscreteInt : juce::AudioParameterInt
{
    using juce::AudioParameterInt::AudioParameterInt;
    bool isDiscrete() const override { return true; }
};

const juce::String pRate{"rate"}, pLength{"length"}, pAmount{"amount"},
                   pGate{"hold"}, pAttack{"attack"}, pDecay{"decay"},
                   pSustain{"sustain"}, pRelease{"release"},
                   pSlot{"slot"}, pLegato{"legato"};
}

/*
 * The parameter ids are the ENGINE's keys, deliberately. One name for a
 * control end to end means the bridge below is a lookup rather than a
 * translation table -- and a translation table is a place for Rate to become
 * "resolution" on one side and drift.
 */
juce::AudioProcessorValueTreeState::ParameterLayout TranceGateProcessor::makeLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    /* Rate and Length are DISCRETE. A float "Length" that lands between steps
     * is a control nobody can set deliberately, and a host drawing it as a
     * continuous sweep would be lying about what it does. */
    l.add (std::make_unique<AudioParameterChoice> (ParameterID{pRate,1}, "Rate",
                                                   kRateNames, kRateDefault));
    l.add (std::make_unique<DiscreteInt>          (ParameterID{pLength,1}, "Length",
                                                   1, kMaxSteps, 16));
    /*
     * SLOT IS A SELECTION, NOT A QUANTITY -- so it is a choice, like Rate.
     * "Slot 4.5" is not a half-way-between pattern, it is nothing, and an int
     * parameter invites a host to interpolate towards it. Its raw value is
     * therefore the INDEX (0..7) while its displayed text is "1".."8", which
     * is also exactly what the engine's wire wants.
     */
    l.add (std::make_unique<AudioParameterChoice> (ParameterID{pSlot,1}, "Slot",
                                                   juce::StringArray { "1","2","3","4",
                                                                       "5","6","7","8" }, 0));
    l.add (std::make_unique<AudioParameterBool>   (ParameterID{pLegato,1}, "Legato", false));
    /*
     * HOW A VALUE PRINTS BELONGS TO THE PARAMETER, NOT TO THE KNOB.
     *
     * The obvious place to format these is the editor's Slider -- and it does
     * not work: SliderAttachment installs its OWN textFromValueFunction,
     * taken from the parameter, so anything set on the slider is overwritten
     * the moment it is attached. Which is the right design, because the same
     * text is what Live puts in its automation lane and what the AU returns
     * for kAudioUnitProperty_ParameterStringFromValue. Formatting here fixes
     * all three at once; formatting on the knob would have fixed none of them
     * and looked like it fixed one.
     *
     * The percentages are 0..1 in the parameter and 0..100 on the face, so
     * the inverse has to exist too -- otherwise typing "50" sets 50, clamps
     * to 1, and reads back as 100%.
     */
    const auto pct = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() * 0.01f; });
    const auto ms = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " ms"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });

    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pAmount,1}, "Amount",
                                                   NormalisableRange<float>(0.0f,1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pGate,1}, "Gate",
                                                   NormalisableRange<float>(0.05f,1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pAttack,1}, "Attack",
                                                   NormalisableRange<float>(0.0f,500.0f), 2.0f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pDecay,1}, "Decay",
                                                   NormalisableRange<float>(0.0f,500.0f), 20.0f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pSustain,1}, "Sustain",
                                                   NormalisableRange<float>(0.0f,1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pRelease,1}, "Release",
                                                   NormalisableRange<float>(0.0f,500.0f), 20.0f, ms));
    return l;
}

/* One listener for every parameter: on a change, write the engine. */
struct TranceGateProcessor::ParamBridge : juce::AudioProcessorValueTreeState::Listener
{
    explicit ParamBridge (TranceGateProcessor& p) : proc (p) {}
    void parameterChanged (const juce::String& id, float) override { proc.pullParam (id); }
    TranceGateProcessor& proc;
};

TranceGateProcessor::TranceGateProcessor()
    : AudioProcessor (BusesProperties()
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TranceGate", makeLayout())
{
    core = tg_core_create (44100.0);
    bridge = std::make_unique<ParamBridge> (*this);
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            apvts.addParameterListener (wp->paramID, bridge.get());
    /* Push the defaults down so the engine and the parameters agree before a
     * single block runs -- otherwise the first automation write is what
     * reconciles them, and until then they silently differ. */
    for (auto* p : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            pullParam (wp->paramID);
}

/* PARAMETER -> ENGINE. The engine speaks strings and expects the same wire
 * conventions the Move module uses: Length and Slot are option INDICES (so
 * they go out one lower than they display), Rate is its LABEL. */
void TranceGateProcessor::pullParam (const juce::String& id)
{
    if (suppressParamWrite.load()) return;
    auto* raw = apvts.getRawParameterValue (id);
    if (raw == nullptr) return;
    const float v = raw->load();

    /*
     * ROUND, DO NOT TRUNCATE.
     *
     * getRawParameterValue hands back `convertFrom0to1(normalised)` -- a
     * FLOAT, even for an AudioParameterInt. Today it is exactly integral,
     * because AudioParameterInt installs a snapToLegalValue that rounds; but
     * that is JUCE's implementation detail, not a promise its interface
     * makes, and the parameter's own rounding lives in `get()`, which this
     * path does not call. Truncating would turn a value one ulp low into a
     * step-off-by-one everywhere except the two exactly representable ends of
     * the range -- silent, and wrong in the middle only. The test asserts the
     * value IS integral, so if the assumption ever stops holding it is a
     * failing test rather than a drifting pattern.
     */
    if (id == pRate)
        engineSet (id, kRateNames[juce::jlimit (0, kRateNames.size() - 1, juce::roundToInt (v))]);
    else if (id == pSlot)
        engineSet (id, juce::String (juce::roundToInt (v)));       /* a choice: already the index */
    else if (id == pLength)
        engineSet (id, juce::String (juce::roundToInt (v) - 1));   /* display -> index */
    else if (id == pLegato)
        engineSet (id, v > 0.5f ? "1" : "0");
    else
        engineSet (id, juce::String (v, 3));

    /*
     * SLOT IS NOT LIKE THE OTHERS: it does not set a value, it swaps which
     * pattern every other value describes. The new slot carries its own
     * Length, so leaving the parameters alone would show the previous slot's
     * -- and the first touch of the Length knob would then snap the pattern
     * to a number the user never chose.
     *
     * Asynchronously, because this arrives on whatever thread the host
     * automates from and setValueNotifyingHost belongs to the message
     * thread. The engine is already correct; this is only the display
     * catching up, so a frame of lag costs nothing.
     */
    if (id == pSlot) triggerAsyncUpdate();
}

void TranceGateProcessor::handleAsyncUpdate() { syncParamsFromEngine(); }

/*
 * ENGINE -> PARAMETERS, after a patch has been loaded underneath them.
 *
 * suppressParamWrite is what stops this ping-ponging: without it every value
 * pushed in here fires the listener, which writes the engine, which is at
 * best redundant and at worst rounds a value on every load.
 */
void TranceGateProcessor::syncParamsFromEngine()
{
    suppressParamWrite.store (true);

    auto setIf = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    const int rateIdx = kRateNames.indexOf (engineGet (pRate));
    if (rateIdx >= 0) setIf (pRate, (float) rateIdx);
    setIf (pLength,  (float) (engineGet (pLength).getIntValue() + 1));
    setIf (pSlot,    (float)  engineGet (pSlot).getIntValue());
    setIf (pLegato,  engineGet (pLegato).getIntValue() ? 1.0f : 0.0f);
    for (auto& id : { pAmount, pGate, pAttack, pDecay, pSustain, pRelease })
        setIf (id, (float) engineGet (id).getDoubleValue());

    suppressParamWrite.store (false);
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
    /* TG_STATE_MAX, NOT A NUMBER OF MY OWN. The longest thing the engine
     * emits is the state blob, and it grew by an order of magnitude when the
     * patterns went to 128 steps -- past the 2048 that used to be written
     * here. get_param snprintfs, so a short buffer does not fail: it returns
     * a truncated patch, which getStateInformation would then hand the host
     * as the project's saved state. */
    std::vector<char> buf (TG_STATE_MAX, 0);
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
    if (patchFromString (juce::String::fromUTF8 (static_cast<const char*> (data), sizeInBytes)))
        syncParamsFromEngine();     /* the half that is easy to forget */
}

juce::AudioProcessorEditor* TranceGateProcessor::createEditor()
{
    return new TranceGateEditor (*this);
}

/*
 * One step, rendered at the duration that step actually lasts.
 *
 * The rate is expressed through the TRANSPORT rather than by copying the rate
 * parameter: a step of `msStep` is the same shape whatever subdivision
 * produced that duration, so a nominal 1/4 at a tempo where one beat IS one
 * step keeps this independent of the rate table's order -- one fewer thing to
 * keep in step with the engine.
 */
TgEnvelopeShape TgEnvelopeShape::render (const TranceGateProcessor& src, double msStep)
{
    TgEnvelopeShape out;
    out.msStep   = msStep;
    out.holdFrac = juce::jlimit (0.0, 1.0, src.engineGet ("hold").getDoubleValue());
    if (msStep <= 0.0) return out;

    constexpr double sr = 44100.0;
    tg_core_t* scratch = tg_core_create (sr);
    if (scratch == nullptr) return out;
    const struct Guard { tg_core_t* c; ~Guard() { tg_core_destroy (c); } } guard { scratch };

    for (auto* k : { "attack", "decay", "sustain", "release", "hold" })
        tg_core_set_param (scratch, k, src.engineGet (k).toRawUTF8());

    /* A single always-on step at full depth and full amount. */
    tg_core_set_param (scratch, "length", "0");        /* option index: 1 step */
    tg_core_set_param (scratch, "cursor", "0");
    tg_core_set_param (scratch, "step",   "On");
    tg_core_set_param (scratch, "amount", "1.0");
    tg_core_set_param (scratch, "step_amount", "1.0");
    tg_core_set_param (scratch, "rate",   "1/4");      /* one beat per step */

    const int frames = (int) juce::jlimit (16.0, sr * 2.0, sr * msStep / 1000.0);
    out.env.assign ((size_t) frames, 0.0f);

    constexpr int kBlock = 64;
    float bl[kBlock], br[kBlock];
    double beats = 0.0;
    /* One beat across the whole render, which is one step. The engine derives
     * the same increment from bpm and sample rate, so its PLL finds the
     * transport already where it expected it. */
    const double beatsPerSample = 1.0 / (double) frames;

    for (int i = 0; i < frames; i += kBlock)
    {
        const int n = juce::jmin (kBlock, frames - i);
        for (int k = 0; k < n; ++k) { bl[k] = 1.0f; br[k] = 1.0f; }

        tg_transport_t t {};
        t.running = 1;
        t.beats   = beats;
        t.bpm     = (float) (60.0 / (msStep / 1000.0));
        tg_core_process_f32_split (scratch, bl, br, n, &t);

        for (int k = 0; k < n; ++k) out.env[(size_t) (i + k)] = bl[k];
        beats += beatsPerSample * n;
    }

    /* Did the release still have somewhere to go when the step ended? That is
     * the fact the drawing exists to make visible. */
    out.truncated = ! out.env.empty() && out.env.back() > 0.02f && out.holdFrac < 1.0;
    return out;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranceGateProcessor();
}
