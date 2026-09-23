#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
                   pSlot{"slot"}, pLegato{"legato"}, pTimeMode{"time_mode"},
                   pCurve{"curve"};

/* The two units an envelope stage can be written in; the engine's enum in the
 * order the choice publishes them. */
const juce::StringArray kTimeModes { "ms", "% Step" };
/* The engine's TG_CURVE_* in the order the choice publishes them, so the
 * index IS the wire value. */
const juce::StringArray kCurves { "Linear", "Exponential", "S-Curve" };
/* A stage runs from 0 to twice the gate's WIDTH. The stored number IS the
 * percentage, so % is a straight readout and ms is `value/100 * width_ms` --
 * two readings of one number rather than two modes. See stage_samples. */
constexpr float kStageMax = 200.0f;
}

/*
 * The parameter ids are the ENGINE's keys, deliberately. One name for a
 * control end to end means the bridge below is a lookup rather than a
 * translation table -- and a translation table is a place for Rate to become
 * "resolution" on one side and drift.
 */
juce::AudioProcessorValueTreeState::ParameterLayout
TranceGateProcessor::makeLayout (std::shared_ptr<std::atomic<int>> mode,
                                 std::shared_ptr<std::atomic<float>> width)
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
    l.add (std::make_unique<AudioParameterBool>   (ParameterID{pLegato,1}, "Join Neighbors", false));
    /*
     * WHAT THE ENVELOPE'S TIMES MEAN. In "% Step" a stage is a share of the
     * step rather than of a second, so the shape survives a change of rate --
     * a patch built at 1/16 sounds like itself at 1/128 instead of being cut
     * off. The number on the knob does not move when this changes; only what
     * it is read as does.
     */
    l.add (std::make_unique<AudioParameterChoice> (ParameterID{pTimeMode,1}, "Env Time",
                                                   kTimeModes, 0));
    /*
     * THE PATH EACH STAGE TAKES between its endpoints. A stage still starts
     * and ends where it did and still lasts as long -- see env_shape in the
     * engine -- so this changes the feel of the gate without changing any
     * time on any knob.
     */
    l.add (std::make_unique<AudioParameterChoice> (ParameterID{pCurve,1}, "Env Curve",
                                                   kCurves, 0));
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
        /* "Values carry their unit in ink-muted after the number: 32 ms, 1/64,
         * 54 %" -- the space is what lets the readout split the two and colour
         * them differently, so it is structure, not typography. */
        /*
         * TWO DECIMALS, which is a DEPARTURE from the system's type note --
         * its sample for a percentage is "54 %". The note assumes a percent
         * is a fine enough step; these knobs are continuous, so an integer
         * reading made every one of them look like it moved in jumps, and
         * the value between two percents was both reachable and invisible.
         * The system's own rule that a readout must not jitter while a knob
         * turns is the same rule pointing the other way.
         */
        .withStringFromValueFunction ([] (float v, int) { return String (v * 100.0f, 2) + " %"; })
        .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() * 0.01f; });
    /*
     * ONE NUMBER, TWO READINGS. The stage value is 0..500 in both modes; the
     * mode decides whether that is milliseconds or hundredths of a step. The
     * lambdas capture the shared flag rather than a copy of it, so flipping
     * the mode re-reads every stage's text without touching a stored value.
     */
    const auto ms = AudioParameterFloatAttributes()
        .withStringFromValueFunction ([mode, width] (float v, int)
        {
            /* TWO DECIMALS, because one number serves a 500 ms range and a
             * 100% one: a whole percent is five milliseconds, so rounding to
             * it made a continuous knob look like it stepped -- drag a little
             * and the reading jumped 50 to 51 with everything between it
             * unreachable. The value was always continuous; only the text
             * was not. */
            /* % is the stored number. ms is what that percentage is WORTH
             * right now -- so it moves when the rate or Width does, while the
             * percentage stays put, which is the point of measuring against
             * the gate. */
            if (mode && mode->load() == 1) return String (v, 2) + " %";
            return String (v * 0.01f * (width ? width->load() : 0.0f), 1) + " ms";
        })
        .withValueFromStringFunction ([mode, width] (const String& t)
        {
            const float n = t.getFloatValue();
            if (mode && mode->load() == 1) return n;
            const float w = width ? width->load() : 0.0f;
            return (w > 0.0f) ? n / w * 100.0f : n;
        });

    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pAmount,1}, "Amount",
                                                   NormalisableRange<float>(0.0f,1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pGate,1}, "Width",
                                                   NormalisableRange<float>(0.05f,1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pAttack,1}, "Attack",
                                                   NormalisableRange<float>(0.0f,kStageMax), 1.6f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pDecay,1}, "Decay",
                                                   NormalisableRange<float>(0.0f,kStageMax), 16.0f, ms));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pSustain,1}, "Sustain",
                                                   NormalisableRange<float>(0.0f,1.0f), 1.0f, pct));
    l.add (std::make_unique<AudioParameterFloat>  (ParameterID{pRelease,1}, "Release",
                                                   NormalisableRange<float>(0.0f,kStageMax), 16.0f, ms));
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
      apvts (*this, nullptr, "TranceGate", makeLayout (timeMode, widthMs))
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
    else if (id == pTimeMode)
    {
        const int m = juce::jlimit (0, 1, juce::roundToInt (v));
        if (timeMode) timeMode->store (m);
        engineSet (id, juce::String (m));
    }
    else if (id == pCurve)
        engineSet (id, juce::String (juce::jlimit (0, 2, juce::roundToInt (v))));
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
    /* Rate and Width both move what a stage's percentage is worth in
     * milliseconds, so the readouts have to be told. */
    if (id == pRate || id == pGate) refreshWidth();

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
void TranceGateProcessor::refreshWidth()
{
    if (widthMs) widthMs->store ((float) engineGet ("width_ms").getDoubleValue());
}

void TranceGateProcessor::syncParamsFromEngine()
{
    refreshWidth();
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
    {
        const int m = juce::jlimit (0, 1, engineGet (pTimeMode).getIntValue());
        if (timeMode) timeMode->store (m);
        setIf (pTimeMode, (float) m);
    }
    setIf (pCurve, (float) juce::jlimit (0, 2, engineGet (pCurve).getIntValue()));
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

    /* THE DRY SIGNAL, before the engine overwrites it in place. Copied rather
     * than referenced for exactly that reason, and into a fixed buffer
     * because an allocation here is the one thing an audio callback must
     * never do. A block longer than the buffer simply captures its first
     * part; the scope is a picture, not a measurement. */
    const int capN = juce::jmin (frames, (int) std::size (dryScratch));
    std::memcpy (dryScratch, buffer.getReadPointer (0), sizeof (float) * (size_t) capN);

    /* Non-interleaved is what JUCE hands us, and the engine has a path for it
     * that is bit-identical to the interleaved one. */
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;
    tg_core_process_f32_split (core, L, R, frames, &t);

    captureBlock (dryScratch, L, capN);
}

/*
 * One block into the sweep. The column comes from the engine's own pattern
 * phase, so the sweep is locked to the pattern rather than to wall time --
 * a scope triggered by the music.
 */
void TranceGateProcessor::captureBlock (const float* dry, const float* wet, int frames)
{
    if (frames <= 0) return;

    const double phase = tg_core_phase01 (core);
    const int col = juce::jlimit (0, Capture::columns - 1,
                                  (int) (phase * (double) Capture::columns));

    /* A column that went BACKWARDS is the pattern wrapping: publish the sweep
     * and start again from the left. Forwards by more than one is a block
     * that spanned several columns, which at 512 columns and a fast rate is
     * normal -- the skipped ones keep the previous pass's bounds rather than
     * being blanked, so the picture does not strobe. */
    if (col < capCol)
    {
        cap.filled.store (Capture::columns, std::memory_order_release);
        cap.sweep.fetch_add (1, std::memory_order_relaxed);
        capCol = -1;
    }

    if (col != capCol)
    {
        if (capCol >= 0)
        {
            cap.dryLo[capCol].store (capDryLo, std::memory_order_relaxed);
            cap.dryHi[capCol].store (capDryHi, std::memory_order_relaxed);
            cap.wetLo[capCol].store (capWetLo, std::memory_order_relaxed);
            cap.wetHi[capCol].store (capWetHi, std::memory_order_relaxed);
            /* Publish AFTER the column is written, so a reader below `filled`
             * never sees half of one. */
            cap.filled.store (capCol + 1, std::memory_order_release);
        }
        capCol = col;
        capDryLo = capDryHi = dry[0];
        capWetLo = capWetHi = wet[0];
    }

    for (int i = 0; i < frames; ++i)
    {
        capDryLo = juce::jmin (capDryLo, dry[i]);
        capDryHi = juce::jmax (capDryHi, dry[i]);
        capWetLo = juce::jmin (capWetLo, wet[i]);
        capWetHi = juce::jmax (capWetHi, wet[i]);
    }
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

/* ============================================== the preview time base == */

namespace {

/*
 * WHAT A SCRATCH ENGINE HAS TO BE TOLD, AND WHY THE OLD ANSWER WAS WRONG.
 *
 * A preview needs two things true at once: one engine step must last exactly
 * T ms, and one rendered sample must stand for T/N ms -- otherwise attack_ms,
 * which the engine converts using the SAMPLE RATE, lands on the wrong number
 * of samples. The second requirement pins sr = N*1000/T. The first then pins
 *
 *     bpm = 60000 * B / T          B = beats per step, from the rate table
 *
 * and tg_block_setup accepts a tempo only inside (1, 1000):
 *
 *     if (t && t->bpm > 1.0f && t->bpm < 1000.0f) bpm = t->bpm;   // else 120
 *
 * The old code nailed the rate to 1/4 (B = 1) and derived bpm = 60000/T, so
 * every step shorter than 60.06 ms -- 1/64 and 1/128 at any normal tempo,
 * 1/32 above 125 BPM -- silently fell back to 120. The scratch engine then
 * believed a step lasted 500 ms while one step's worth of samples was
 * rendered: `frac` never reached `hold`, the gate never closed, and the
 * picture showed an attack and a decay that ran on forever. It was not a
 * drawing that needed more room; it was the wrong answer, drawn accurately.
 *
 * B is the free variable. Pick the rung of the rate ladder that puts bpm
 * nearest 120 and the tempo is always in range.
 */
struct TimeBase
{
    juce::String rate = "1/4";
    double beats   = 1.0;
    double bpm     = 120.0;
    bool   clamped = false;
};

/*
 * B PER RATE, MEASURED FROM THE ENGINE RATHER THAN COPIED OUT OF IT.
 *
 * tg_rates is private to the C file and its comment says as much; a second
 * copy here is a table that drifts. On a core that has not yet processed a
 * block last_bpm is 120, and set_param("rate") calls recalc_ms_per_step, so
 * the ui readout's ms_step field is 60000*B/120 -- that is, B = ms_step/500.
 */
const std::vector<double>& rateBeats()
{
    static const std::vector<double> beats = []
    {
        std::vector<double> v ((size_t) kRateNames.size(), 1.0);
        if (tg_core_t* c = tg_core_create (44100.0))
        {
            std::vector<char> buf (TG_STATE_MAX, 0);
            for (int i = 0; i < kRateNames.size(); ++i)
            {
                tg_core_set_param (c, "rate", kRateNames[i].toRawUTF8());
                if (tg_core_get_param (c, "ui", buf.data(), (int) buf.size()) < 0) continue;

                const auto parts = juce::StringArray::fromTokens (
                                       juce::String::fromUTF8 (buf.data()), ":", "");
                if (parts.size() > 4)
                    if (const double ms = parts[4].getDoubleValue(); ms > 0.0)
                        v[(size_t) i] = ms / 500.0;
            }
            tg_core_destroy (c);
        }
        return v;
    }();
    return beats;
}

TimeBase chooseTimeBase (double stepMs)
{
    TimeBase tb;
    const double t = juce::jlimit (2.0, 8000.0, stepMs);
    tb.clamped = (stepMs < 2.0 || stepMs > 8000.0);

    const auto& beats = rateBeats();
    double best = -1.0;

    for (int i = 0; i < kRateNames.size(); ++i)
    {
        const double b   = beats[(size_t) i];
        const double bpm = 60000.0 * b / t;
        if (bpm < 20.0 || bpm > 950.0) continue;      /* well inside the engine's guard */

        const double d = std::abs (std::log (bpm / 120.0));
        if (best < 0.0 || d < best)
        {
            best = d;
            tb.rate = kRateNames[i];
            tb.beats = b;
            tb.bpm = bpm;
        }
    }

    /* The ladder spans 85x in 13 rungs of ratio <= 2 and the window is 47x
     * wide, so a rung always lands inside it for t in [2, 8000]. If that ever
     * stops being true, SAY SO -- reverting quietly to a 120 BPM fallback is
     * the bug this function exists to remove. */
    if (best < 0.0) tb.clamped = true;
    return tb;
}

/* Creates and destroys a preview engine. A reused instance carries envelope
 * state between calls -- env, env_t, the stage, the playhead -- so a curve
 * would start wherever the previous render happened to stop: plausible every
 * time, and a different picture depending on what you touched last. */
struct Scratch
{
    explicit Scratch (double sr) : core (tg_core_create (sr)) {}
    ~Scratch() { if (core != nullptr) tg_core_destroy (core); }
    Scratch (const Scratch&) = delete;
    Scratch& operator= (const Scratch&) = delete;
    tg_core_t* core;
};

/* DC 1.0 through the engine, which with amount and depth at 1 is the gain
 * itself. The transport position is COMPUTED, never accumulated: the engine
 * derives its own increment from bpm and sample rate, so handing it a
 * position that agrees to the sample leaves its PLL nothing to correct. */
void runDc (tg_core_t* core, const TimeBase& tb, double samplesPerStep,
            int frames, std::vector<float>& out)
{
    out.assign ((size_t) juce::jmax (0, frames), 0.0f);
    if (core == nullptr || frames <= 0 || samplesPerStep <= 0.0) return;

    constexpr int kBlock = 64;
    float bl[kBlock], br[kBlock];

    for (int i = 0; i < frames; i += kBlock)
    {
        const int n = juce::jmin (kBlock, frames - i);
        for (int k = 0; k < n; ++k) { bl[k] = 1.0f; br[k] = 1.0f; }

        tg_transport_t t {};
        t.running = 1;
        t.beats   = (double) i * tb.beats / samplesPerStep;
        t.bpm     = (float) tb.bpm;
        tg_core_process_f32_split (core, bl, br, n, &t);

        for (int k = 0; k < n; ++k) out[(size_t) (i + k)] = bl[k];
    }
}

}  // namespace

/* ================================================= the envelope shape == */

float TgEnvelopeShape::levelAt (double frac) const
{
    if (env.empty()) return 0.0f;
    const int n = (int) env.size();
    const int i = juce::jlimit (0, n - 1, (int) std::lround (frac * (double) (n - 1)));
    return env[(size_t) i];
}

/*
 * THE WHOLE ENVELOPE, NOT THE PART THAT FITS.
 *
 * The x-axis used to be one step, which meant a 442 ms decay on a 29.5 ms
 * step showed its first 6.7% -- a picture of a knob you could not read by
 * turning it. The axis is now the envelope's own length, with the step edge
 * marked on it, so a decay that overruns reads as exactly that.
 *
 *     gateMs      = hold * msStep              where the gate really closes
 *     releaseAtMs = max(gateMs, A + D)         where the DRAWN release begins
 *     spanMs      = releaseAtMs + R            where the drawn envelope ends
 *
 * The max is the whole point, and it is why the drawn release is not simply
 * the gate. A long decay under a short gate is the case this picture exists
 * for -- 442 ms of decay on a 29.5 ms step -- and letting the gate cut the
 * render there would kill the curve at 3% of the axis and leave the rest
 * blank: the same unreadable picture as before, merely on a wider canvas. So
 * the curve completes the decay it was DIALLED with, and the gate is drawn as
 * a rule across it, saying "this is where it really stops". Shape on the
 * curve, truth on the marks.
 *
 * When the gate closes after the decay has finished -- the ordinary case --
 * the two coincide and the curve is literally what the engine does.
 *
 * The 4% of air at the end also guarantees releaseAtMs/spanMs < 1, which is
 * what makes the engine's own gate rule fire at all.
 */
TgEnvelopeShape TgEnvelopeShape::render (const TranceGateProcessor& src, double msStep)
{
    TgEnvelopeShape out;
    out.msStep    = msStep;
    out.holdFrac  = juce::jlimit (0.0, 1.0, src.engineGet ("hold").getDoubleValue());
    /*
     * THE STAGES ARE PERCENTAGES OF THE GATE'S WIDTH, so the drawing has to
     * convert before it can lay them on a millisecond axis. Reading them as
     * milliseconds made a 200%% decay draw as 200 ms: at a 15 ms gate the
     * axis came out twenty-five times too long and the curve a flat line
     * against the left edge.
     *
     * Width is taken from THIS patch's hold and the step being drawn, not
     * from the engine's width_ms, because the plot is asked to draw a step
     * duration that may not be the one currently playing.
     */
    const double holdFrac = juce::jlimit (0.0, 1.0,
                                          src.engineGet ("hold").getDoubleValue());
    const double widthMs  = holdFrac * msStep;
    const double pctToMs  = widthMs * 0.01;
    out.attackMs  = src.engineGet ("attack").getDoubleValue()  * pctToMs;
    out.decayMs   = src.engineGet ("decay").getDoubleValue()   * pctToMs;
    out.releaseMs = src.engineGet ("release").getDoubleValue() * pctToMs;
    out.sustainLevel = juce::jlimit (0.0, 1.0, src.engineGet ("sustain").getDoubleValue());
    if (msStep <= 0.0) return out;

    out.gateMs      = out.holdFrac * msStep;
    out.releaseAtMs = juce::jmax (out.gateMs, out.attackMs + out.decayMs);

    double span = juce::jmax (out.releaseAtMs + out.releaseMs, msStep, 2.0) * 1.04;
    out.spanMs = span;

    /* The render is no longer at the audio rate, so a three-second span costs
     * a few thousand samples rather than a hundred thousand. 1024 keeps at
     * least four samples per column of the narrowest plot; 32768 caps a
     * render at a fraction of a millisecond of message-thread work. */
    const int    frames   = (int) juce::jlimit (1024.0, 32768.0, std::round (span * 8.0));
    const double renderSr = (double) frames * 1000.0 / span;

    const auto tb = chooseTimeBase (span);
    out.clamped = tb.clamped;

    /*
     * ONE RENDER PER TRACE, identical but for the scaled `hold`.
     *
     * `releaseAt` is where the DIALLED shape lets go -- after the decay has
     * run its course -- and `gateMs` is where the gate really lets go. When
     * the gate closes later than the decay ends the two are the same number
     * and the second render is skipped.
     */
    auto renderAt = [&] (double releaseMsIn, std::vector<float>& dst)
    {
        Scratch sc (renderSr);
        if (sc.core == nullptr) return;

        tg_core_set_param (sc.core, "state", src.engineGet ("state").toRawUTF8());

        /* One always-on, untied step at full depth and full amount, so the
         * engine's output gain reduces to `env` exactly.
         *
         * THE `ties` AND `legato` LINES ARE LOAD-BEARING, not tidiness. Both
         * suppress the gate's early release, and this is a ONE-STEP pattern
         * whose "next step" is itself -- so either one left set would stop the
         * curve ever coming down. */
        tg_core_set_param (sc.core, "length", "0");        /* option index: 1 step */
        tg_core_set_param (sc.core, "cursor", "0");
        tg_core_set_param (sc.core, "ties",   "0");
        tg_core_set_param (sc.core, "step",   "On");
        tg_core_set_param (sc.core, "step_amount", "1.0");
        tg_core_set_param (sc.core, "amount", "1.0");
        tg_core_set_param (sc.core, "legato", "0");

        /*
         * THE RENDER'S STEP IS THE WHOLE SPAN, and the release is placed
         * inside it by rescaling one dimensionless fraction. `hold` is the
         * engine's only in-step release trigger, so this borrows the engine's
         * gate rule instead of writing a second one; A, D, S and R go in as
         * the patch's real milliseconds and never come near the 0..500 clamp.
         */
        tg_core_set_param (sc.core, "hold",
                           juce::String (releaseMsIn / span, 6).toRawUTF8());
        tg_core_set_param (sc.core, "rate", tb.rate.toRawUTF8());

        runDc (sc.core, tb, (double) frames, frames, dst);
    };

    renderAt (out.gateMs, out.env);
    if (out.releaseAtMs > out.gateMs + 1.0e-6)
        renderAt (out.releaseAtMs, out.envDialled);

    /* Computed, not sniffed off the last sample: you asked the gate to close
     * inside the step and the release has nowhere to finish. At Gate 100% the
     * release starts AT the boundary, and running past it is not a fault. */
    out.truncated = out.holdFrac < 1.0 && out.gateMs + out.releaseMs > msStep;
    return out;
}

/* ================================================== the pattern shape == */

TgPatternShape TgPatternShape::render (const TranceGateProcessor& src, double msStep, int columns)
{
    TgPatternShape out;
    out.msStep = msStep;
    if (msStep <= 0.0 || columns <= 0) return out;

    /* get_param("length") answers with the OPTION INDEX, as set_param takes
     * it -- so the step count is one more. */
    const int length = juce::jlimit (1, kMaxSteps, src.engineGet ("length").getIntValue() + 1);

    /* Enough samples per step that every column has something to reduce, few
     * enough that a whole 128-step pattern stays a fraction of a millisecond.
     * perStep is an INTEGER, so a step boundary falls on an exact sample and
     * the gridlines and the playhead cannot drift off it. */
    const int perStepWant = (int) std::lround (msStep * 8.0);
    const int perStepMin  = (int) std::ceil (4.0 * (double) columns / (double) length);
    const int perStepCap  = juce::jmax (perStepMin, juce::jmax (8, 32768 / length));

    out.length  = length;
    out.perStep = juce::jlimit (perStepMin, perStepCap, perStepWant);

    const int    frames   = out.perStep * length;
    const double renderSr = (double) out.perStep * 1000.0 / msStep;

    Scratch sc (renderSr);
    if (sc.core == nullptr) { out.length = 0; out.perStep = 0; return out; }

    /* The REAL patch: steps, ties, depths, length, rate, legato, hold, ADSR. */
    tg_core_set_param (sc.core, "state", src.engineGet ("state").toRawUTF8());

    /*
     * AMOUNT IS THE ONE OVERRIDE, AND LEAVING IT OUT IS WHAT MAKES THIS CHEAP.
     *
     *     m = 1 - amount*(1 - env*level) = floor + (1 - floor)*g,  floor = 1 - amount
     *
     * is affine in g, so Amount belongs in the paint transform rather than in
     * the render. The picture still matches the audio sample for sample, and
     * dragging Amount never invalidates the cached curve. It also steps
     * around the amount <= 0 short circuit, which leaves the buffer untouched
     * and would therefore store the input DC -- a solid open gate -- for a
     * setting that is in fact a true bypass.
     */
    tg_core_set_param (sc.core, "amount", "1.0");

    /* An equivalent (rate, bpm) pair for the step duration the patch really
     * has. The engine's behaviour depends only on samples_per_step and the
     * sample rate, and both are matched exactly. */
    const auto tb = chooseTimeBase (msStep);
    out.clamped = tb.clamped;

    runDc (sc.core, tb, (double) out.perStep, frames, out.gain);
    return out;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TranceGateProcessor();
}
