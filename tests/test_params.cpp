/*
 * The parameter layer, tested against the ENGINE rather than against itself.
 *
 * Every assertion here ends at `engineGet` -- what the DSP actually holds --
 * because a parameter test that only reads parameters back proves the host's
 * storage works and nothing about whether the sound changed. The two ways
 * this layer can be wrong are both invisible from the parameter side: an
 * off-by-one on a value wired as an option INDEX, and a patch load that
 * leaves the automation lane showing the value it had before.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include "../plugins/trance-gate/Source/PluginProcessor.h"
#include "../plugins/trance-gate/Source/PluginEditor.h"

static int failures = 0, checks = 0;

static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    ++checks;
    std::printf ("  %-58s %s%s\n", what.toRawUTF8(), ok ? "ok" : "FAIL",
                 detail.isEmpty() ? "" : (" (" + detail + ")").toRawUTF8());
    if (! ok) ++failures;
}

static void eq (const juce::String& got, const juce::String& want, const juce::String& what)
{
    check (got == want, what, "got " + got + " want " + want);
}

/* Move a parameter the way a host does: through the normalised range, so the
 * test exercises the same conversion automation does rather than a shortcut
 * past it. */
static void setParam (TranceGateProcessor& p, const juce::String& id, float displayValue)
{
    auto* param = p.state().getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (displayValue));
}

static float getParam (TranceGateProcessor& p, const juce::String& id)
{
    return p.state().getRawParameterValue (id)->load();
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    {
        /*
         * THE DEFAULTS MUST AGREE, and nothing else checks it.
         *
         * The constructor pushes every parameter's default DOWN into the
         * engine, so a layout default that drifted from the engine's would
         * not produce an error -- it would quietly make a fresh plugin
         * instance sound different from a fresh Move instance, which is the
         * one promise this port makes.
         */
        std::puts ("defaults agree with the engine:");
        tg_core_t* bare = tg_core_create (44100.0);
        TranceGateProcessor p;

        auto bareGet = [bare] (const char* k)
        {
            char buf[64] = {};
            tg_core_get_param (bare, k, buf, sizeof (buf));
            return juce::String (buf);
        };

        for (auto* key : { "rate", "attack", "decay", "sustain", "release",
                           "hold", "amount", "legato", "slot", "length",
                           "time_mode", "curve" })
            eq (p.engineGet (key), bareGet (key),
                juce::String ("  ") + key + " default survives construction");

        tg_core_destroy (bare);
    }

    {
        std::puts ("\nevery parameter reaches the engine:");
        TranceGateProcessor p;

        setParam (p, "attack",  123.0f);  eq (p.engineGet ("attack"),  "123.0", "  attack");
        /* A stage runs 0..200 -- a percentage of the gate's width, not
         * milliseconds. 200 is the top of the range and means "twice the
         * gate", a stage that cannot finish inside it. */
        setParam (p, "decay",   120.0f);  eq (p.engineGet ("decay"),   "120.0", "  decay");
        setParam (p, "release", 200.0f);  eq (p.engineGet ("release"), "200.0", "  release");
        setParam (p, "sustain",   0.25f); eq (p.engineGet ("sustain"),  "0.25", "  sustain");
        setParam (p, "hold",      0.50f); eq (p.engineGet ("hold"),     "0.50", "  gate");
        setParam (p, "amount",    0.75f); eq (p.engineGet ("amount"),   "0.75", "  amount");
        setParam (p, "legato",    1.0f);  eq (p.engineGet ("legato"),      "1", "  legato");

        /*
         * `curve` had NO value assertion anywhere in the suite. It was proved
         * only transitively -- the plots table shows the cache key moves --
         * and a cache key moves just as happily for the wrong value, so an
         * off-by-one into S-Curve would have shipped. Both ends, so an
         * inverted or shifted table cannot pass.
         */
        setParam (p, "curve", 0.0f);      eq (p.engineGet ("curve"),       "0", "  curve linear");
        setParam (p, "curve", 2.0f);      eq (p.engineGet ("curve"),       "2", "  ...and s-curve");
        setParam (p, "time_mode", 1.0f);  eq (p.engineGet ("time_mode"),   "1", "  env time");
        setParam (p, "time_mode", 0.0f);  eq (p.engineGet ("time_mode"),   "0", "  ...and back");

        /*
         * RATE IS WIRED BY LABEL, LENGTH BY INDEX, and the two are the whole
         * reason this file exists. Length 128 must arrive as the index 127;
         * sending the displayed number instead would set 129 -- clamped to
         * 128, so it would look right at the top of the range and be wrong
         * everywhere else.
         */
        setParam (p, "rate", 12.0f);      eq (p.engineGet ("rate"), "1/128", "  rate reaches the new 1/128");
        setParam (p, "rate", 0.0f);       eq (p.engineGet ("rate"),  "1/1T", "  ...and the other end of the table");
        setParam (p, "length", 128.0f);   eq (p.engineGet ("length"),  "127", "  length 128 arrives as index 127");
        setParam (p, "length", 1.0f);     eq (p.engineGet ("length"),    "0", "  length 1 arrives as index 0");
        setParam (p, "slot", 7.0f);       eq (p.engineGet ("slot"),      "7", "  slot choice 7 is the eighth pattern");
        /* A host would have run the slot refresh by now, and the sweep below
         * depends on it: without the pump the Length parameter still reads 1
         * from the line above, so setting 1 again is a no-op APVTS correctly
         * skips -- and the sweep would be testing nothing at its first step. */
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        /* Every displayed length round-trips, not just the two ends: an
         * off-by-one that only bites in the middle is the kind this layer
         * produces. */
        bool allLengths = true;
        for (int n = 1; n <= 128; ++n)
        {
            setParam (p, "length", (float) n);
            if (p.engineGet ("length").getIntValue() != n - 1)
            {
                if (allLengths)
                    std::printf ("      first miss: asked %d, raw %.6f, engine %s\n",
                                 n, getParam (p, "length"), p.engineGet ("length").toRawUTF8());
                allLengths = false;
            }
        }
        check (allLengths, "  all 128 lengths round-trip");

        /* The assumption toWire rounds against: an int parameter's raw
         * value comes back exactly integral. JUCE gives that via a snapping
         * NormalisableRange rather than via its interface, so it is pinned
         * here rather than trusted. */
        bool integral = true;
        for (int n = 1; n <= 128; ++n)
        {
            setParam (p, "length", (float) n);
            const float raw = getParam (p, "length");
            if (! juce::approximatelyEqual (raw, std::floor (raw))) integral = false;
        }
        check (integral, "  an int parameter's raw value is exactly integral");

        bool allRates = true;
        const juce::StringArray rates { "1/1T","1/2","1/2T","1/4","1/4T","1/8","1/8T",
                                        "1/16","1/16T","1/32","1/32T","1/64","1/128" };
        for (int i = 0; i < rates.size(); ++i)
        {
            setParam (p, "rate", (float) i);
            if (p.engineGet ("rate") != rates[i]) allRates = false;
        }
        check (allRates, "  every rate label the engine accepts");
    }

    {
        /*
         * THE HALF THAT IS EASY TO FORGET. A patch loaded underneath the
         * parameters must push its macros back OUT, or Live's automation lane
         * shows the old value while the engine has the new one -- and the
         * next automation touch snaps the sound back to what the lane says.
         */
        std::puts ("\na loaded patch updates the parameters:");
        juce::MemoryBlock saved;
        {
            TranceGateProcessor a;
            setParam (a, "rate", 9.0f);        /* 1/32 */
            setParam (a, "length", 64.0f);
            setParam (a, "sustain", 0.30f);
            setParam (a, "attack", 40.0f);
            setParam (a, "legato", 1.0f);
            a.getStateInformation (saved);
        }

        TranceGateProcessor b;
        check (saved.getSize() > 100, "  the patch is a real blob",
               juce::String ((int) saved.getSize()) + " bytes");
        b.setStateInformation (saved.getData(), (int) saved.getSize());

        eq (b.engineGet ("rate"), "1/32", "  engine took the patch");
        check (juce::approximatelyEqual (getParam (b, "rate"),    9.0f),  "  rate parameter followed");
        check (juce::approximatelyEqual (getParam (b, "length"), 64.0f),  "  length parameter followed",
               juce::String (getParam (b, "length")));
        check (std::abs (getParam (b, "sustain") - 0.30f) < 0.005f, "  sustain parameter followed");
        check (std::abs (getParam (b, "attack")  - 40.0f) < 0.05f,  "  attack parameter followed");
        check (getParam (b, "legato") > 0.5f, "  legato parameter followed");

        /* And the load must not have bounced back through the write path:
         * the engine still holds exactly what the patch said. */
        eq (b.engineGet ("length"), "63", "  the engine was not rewritten by the sync");
    }

    {
        /*
         * SLOT IS NOT A VALUE, IT IS A SELECTION. Switching brings a pattern
         * with its own Length, so the Length parameter has to catch up --
         * otherwise the first touch of that knob snaps the new slot to the
         * old slot's length.
         */
        std::puts ("\nswitching slot refreshes the rest:");
        TranceGateProcessor p;
        setParam (p, "slot", 1.0f);
        setParam (p, "length", 7.0f);            /* slot 1 is now 7 steps */
        setParam (p, "slot", 4.0f);              /* another slot, still the default 16 */

        /* The refresh is asynchronous on purpose -- it touches the host's
         * lanes and so belongs on the message thread. */
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        eq (p.engineGet ("length"), "15", "  the engine is on the new slot's length");
        check (juce::approximatelyEqual (getParam (p, "length"), 16.0f),
               "  the length parameter caught up", juce::String (getParam (p, "length")));

        setParam (p, "slot", 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (juce::approximatelyEqual (getParam (p, "length"), 7.0f),
               "  ...and back again shows 7", juce::String (getParam (p, "length")));
    }

    {
        /*
         * A FULLY ACCENTED 128-STEP PATCH MUST SURVIVE A SAVE. This is the
         * buffer that was sized 2048 by hand and is now sized TG_STATE_MAX:
         * too short does not throw, it truncates, and the truncation only
         * shows up as a project that reopens with the wrong pattern.
         */
        std::puts ("\nthe biggest patch survives a save:");
        TranceGateProcessor a;
        for (int s = 0; s < 8; ++s)
        {
            setParam (a, "slot", (float) (s + 1));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            setParam (a, "length", 128.0f);
            for (int i = 0; i < 128; ++i)
            {
                a.engineSet ("cursor", juce::String (i));
                a.engineSet ("step", "1");
                a.engineSet ("step_amount", juce::String (0.1f + 0.003f * (float) i, 2));
            }
        }
        juce::MemoryBlock saved;
        a.getStateInformation (saved);
        check ((int) saved.getSize() > 2000, "  the blob is the big one",
               juce::String ((int) saved.getSize()) + " bytes");
        check (juce::String::fromUTF8 ((const char*) saved.getData(),
                                       (int) saved.getSize()).trim().endsWith ("}"),
               "  it was not truncated mid-blob");

        TranceGateProcessor b;
        b.setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MemoryBlock again;
        b.getStateInformation (again);
        check (saved == again, "  and it round-trips byte for byte");
    }

    {
        /*
         * THE ENVELOPE CURVE. The axis is the ENVELOPE's own length -- the
         * whole attack, decay and release as dialled -- with the step edge
         * and the gate marked on it. Assertions are therefore in AXIS
         * FRACTIONS rather than array indices, which is what lets them
         * survive the axis changing again.
         */
        std::puts ("\nthe envelope curve:");
        TranceGateProcessor p;

        /* A flat plateau, so the only knee in the curve is the release.
         * env_enter walks zero-length stages, so attack 0 / decay 0 is a
         * gate that is simply open. */
        setParam (p, "attack",  0.0f);
        setParam (p, "decay",   0.0f);
        setParam (p, "sustain", 1.0f);
        setParam (p, "hold",    0.5f);
        setParam (p, "release", 80.0f);

        /*
         * A 1/4 step at 120 BPM is 500 ms. Width 50% makes the gate 250 ms,
         * so a release of 80% of that is 200 ms -- and it starts at 250 ms
         * with 250 ms of step left, so it finishes with room to spare.
         *
         * The arithmetic is spelled out because a stage is a percentage of
         * the WIDTH now: whether a release fits depends on Width twice over,
         * once for the length of the release and once for how much step is
         * left when it starts.
         */
        auto slow = TgEnvelopeShape::render (p, 500.0);
        check (! slow.env.empty(), "  a slow step renders",
               juce::String ((int) slow.env.size()) + " samples");
        check (slow.env.back() < 0.05f, "  a release that fits reaches zero",
               juce::String (slow.env.back(), 3));
        check (! slow.truncated, "  ...and is not reported as cut off");

        /* A 1/32 step at 174 BPM is ~43 ms. The same release cannot even
         * begin to finish inside the step. The curve now shows the whole
         * release, so the fact being pinned is no longer "where the curve
         * ends" but "how far it had got when the step ran out". */
        /*
         * CUT OFF NEEDS THE RELEASE TO OUTLAST WHAT IS LEFT OF THE STEP, and
         * in these units that is a condition on Width: a 200% release lasts
         * 2*hold*step while the step has (1-hold)*step remaining, so it only
         * overruns when hold > 1/3. At Width 80% the release is 1.6 steps
         * against 0.2 of a step remaining, so it is barely begun at the edge.
         */
        setParam (p, "hold",    0.8f);
        setParam (p, "release", 200.0f);
        auto fast = TgEnvelopeShape::render (p, 43.0);
        check (fast.truncated, "  a release twice the gate is cut off by the step");
        check (fast.levelAt (fast.stepFrac()) > 0.8f,
               "  ...with most of the release still to go at the step edge",
               juce::String (fast.levelAt (fast.stepFrac()), 3));

        /*
         * THE SAME PATCH AT TWO RATES IS NOW THE SAME PICTURE, and this test
         * used to assert the opposite -- correctly, when a stage was absolute
         * milliseconds and a change of rate moved the step edge across a
         * fixed envelope.
         *
         * Measured against Width, every duration in the drawing scales with
         * the step together, so the shape is rate-invariant. That is the
         * entire claim of the change, so it is asserted rather than quietly
         * deleted.
         */
        auto sameSlow = TgEnvelopeShape::render (p, 500.0);
        check (std::abs (fast.stepFrac() - sameSlow.stepFrac()) < 0.001,
               "  one patch, two rates, ONE shape",
               juce::String (fast.stepFrac(), 3) + " vs " + juce::String (sameSlow.stepFrac(), 3));

        /* Where the release BEGINS. With sustain 1 and no attack or decay the
         * plateau is exactly 1.0f, so the first sample under 0.999 is the
         * first sample of the release. */
        auto kneeAt = [] (const TgEnvelopeShape& sh)
        {
            for (size_t i = 0; i < sh.env.size(); ++i)
                if (sh.env[i] < 0.999f) return (double) i / (double) sh.env.size();
            return 1.0;
        };
        setParam (p, "release", 50.0f);
        setParam (p, "hold", 0.25f);
        const auto early = TgEnvelopeShape::render (p, 500.0);
        setParam (p, "hold", 0.75f);
        const auto late  = TgEnvelopeShape::render (p, 500.0);

        /* Axis-independent, and stronger than the old "a quarter of the way
         * in": the knee must land where the shape SAYS the release starts,
         * and the gate must be where the patch says it is. With no attack or
         * decay the drawn release and the real gate coincide. */
        check (std::abs (kneeAt (early) - early.releaseFrac()) < 0.01,
               "  the knee is where releaseFrac says it is (gate 25%)",
               juce::String (kneeAt (early), 3) + " vs " + juce::String (early.releaseFrac(), 3));
        check (std::abs (early.gateMs - 0.25 * 500.0) < 0.01,
               "  ...and gate 25% of a 500 ms step is 125 ms",
               juce::String (early.gateMs, 2));
        check (std::abs (kneeAt (late) - late.releaseFrac()) < 0.01,
               "  the knee is where releaseFrac says it is (gate 75%)",
               juce::String (kneeAt (late), 3) + " vs " + juce::String (late.releaseFrac(), 3));
        check (std::abs (late.gateMs - 0.75 * 500.0) < 0.01,
               "  ...and gate 75% of a 500 ms step is 375 ms",
               juce::String (late.gateMs, 2));
        check (kneeAt (late) > kneeAt (early), "  ...so moving Gate moves the knee");

        /*
         * A DIALLED DECAY THE GATE CUTS SHORT, which is what the second,
         * dimmer trace exists to show.
         *
         * This block used to pin a decay FIFTEEN TIMES the step. That case no
         * longer exists: a stage tops out at 200% -- twice the gate -- so the
         * extreme it guarded against is unreachable by construction, which is
         * a better guarantee than a test. What remains true, and is worth
         * keeping, is that a decay outlasting the gate is drawn twice: solid
         * for what the gate allows, dim for what was asked for.
         */
        setParam (p, "attack",  10.0f);
        setParam (p, "decay",   200.0f);
        setParam (p, "sustain", 0.0f);
        setParam (p, "release", 20.0f);
        setParam (p, "hold",    0.80f);
        const auto wide = TgEnvelopeShape::render (p, 125.0);
        /* Width 80% of a 125 ms step is a 100 ms gate, so the stages come to
         * 5 + 200 + 20 = 225 ms -- comfortably longer than the step, which is
         * what makes the axis the STAGES' and not the step's. */
        const double gate = 0.80 * 125.0;
        check (std::abs (wide.spanMs - (0.10 + 2.00 + 0.20) * gate * 1.04) < 1.0,
               "  the axis spans the stages, not the step",
               juce::String (wide.spanMs, 1));
        check (wide.gateFrac() < wide.releaseFrac(),
               "  the gate closes before the dialled decay ends",
               juce::String (wide.gateFrac(), 4) + " vs " + juce::String (wide.releaseFrac(), 4));
        check (! wide.envDialled.empty(),
               "  ...so the dialled shape is drawn as a second trace");
        /* The two traces must actually differ, which is the only reason to
         * draw both -- and the solid one must be the LOWER, being the one the
         * gate cut short. */
        const float ghostMid = wide.envDialled.empty() ? 0.0f
                             : wide.envDialled[wide.envDialled.size() / 2];
        check (ghostMid > wide.levelAt (0.5) + 0.05f,
               "  ...and the dialled decay is still above the gated one at mid-axis",
               juce::String (ghostMid, 3) + " vs " + juce::String (wide.levelAt (0.5), 3));
        check (wide.env.back() < 0.01f, "  ...reaching zero on screen",
               juce::String (wide.env.back(), 4));

        /*
         * THE TWO TRACES ARE ONE CURVE UNTIL THE GATE.
         *
         * This is the invariant the whole two-trace drawing rests on and
         * nothing asserted it. They are rendered separately -- same patch,
         * same span, same time base, differing only in where the release is
         * triggered -- so up to the gate they must be the SAME attack and the
         * SAME decay, and only after it may they part.
         *
         * They did not. A stage is a percentage of Width, and the release
         * point was being placed by handing each render a different `hold`;
         * since width_ms = hold * ms_per_step, that scaled every stage too,
         * and the ghost came out stretched by releaseAt/gate -- always, since
         * it is only drawn when those differ.
         */
        {
            double worst = 0.0, at = 0.0;
            for (double f = 0.02; f < wide.gateFrac(); f += 0.02)
            {
                const double i = f * (double) (wide.envDialled.size() - 1);
                const double gh = wide.envDialled[(size_t) juce::jlimit (0.0, (double) wide.envDialled.size() - 1.0, i)];
                const double d  = std::abs (gh - wide.levelAt (f));
                if (d > worst) { worst = d; at = f; }
            }
            check (worst < 0.02,
                   "  the two traces are one curve until the gate",
                   "worst " + juce::String (worst, 3) + " at " + juce::String (at, 3));
        }

        /*
         * GATE MUST MOVE THE PICTURE.
         *
         * When attack + decay outlast the gate point the axis cannot move
         * with Gate -- so if the curve is the dialled shape too, the knob
         * looks dead. It WAS dead: every Gate value from 5% to 100% rendered
         * a byte-identical curve. The solid trace must differ; the ghost,
         * which is the dialled shape, must not.
         */
        setParam (p, "hold", 0.25f);
        const auto g25 = TgEnvelopeShape::render (p, 29.53);
        setParam (p, "hold", 0.75f);
        const auto g75 = TgEnvelopeShape::render (p, 29.53);
        check (g25.env != g75.env, "  turning Width moves the gated trace");
        /*
         * AND THE DIALLED ONE TOO, which is new and is the consequence worth
         * pinning. This asserted the opposite while a stage was absolute
         * milliseconds: Width decided only where the gate shut, so the shape
         * you dialled was none of its business.
         *
         * A stage is a percentage OF Width now, so Width is the unit the
         * whole envelope is written in and turning it rescales every stage.
         * That is the surprising half of "one width is 100%" and belongs in a
         * test rather than only in a commit message.
         */
        check (g25.envDialled != g75.envDialled,
               "  ...and the dialled one, Width being the unit it is drawn in");
        check (g25.gateFrac() < g75.gateFrac(), "  ...in the direction it was turned",
               juce::String (g25.gateFrac(), 4) + " -> " + juce::String (g75.gateFrac(), 4));

        /* The curve IS the engine, not a second formula -- so a sustain of
         * 0.5 must actually plateau at 0.5. Sampled mid-STEP deliberately:
         * mid-axis is now somewhere in the release. */
        setParam (p, "attack",  0.0f);
        setParam (p, "decay",   10.0f);
        setParam (p, "sustain", 0.5f);
        setParam (p, "release", 200.0f);
        setParam (p, "hold",    1.0f);
        auto half = TgEnvelopeShape::render (p, 500.0);
        const float mid = half.levelAt (0.5 * half.stepFrac());
        check (std::abs (mid - 0.5f) < 0.02f, "  sustain 50% plateaus at 0.5",
               juce::String (mid, 3));

        /* Rendering twice must give the same picture: the render owns its
         * engine precisely so no state survives to shift the second one. */
        auto again = TgEnvelopeShape::render (p, 500.0);
        check (again.env == half.env, "  two renders of one patch are identical");
    }

    {
        /*
         * THE 128-STEP MASK PARSE. The engine writes the highest non-zero
         * word FIRST and unpadded, so a naive left-to-right parse puts word 0
         * in the wrong place -- and is perfectly correct for every pattern of
         * 32 steps or fewer, which is every pattern that existed until now.
         */
        std::puts ("\nthe editor's 128-step mask:");
        using Mask = TranceGateEditor::Mask;

        auto m32 = Mask::fromHex ("0000FFFF");
        check (m32.get (0) && m32.get (15) && ! m32.get (16),
               "  a 32-step pattern parses as it always did");

        /* Step 127 only: word 3 bit 31 -> "80000000" then three zero words. */
        auto top = Mask::fromHex ("80000000000000000000000000000000");
        check (top.get (127) && ! top.get (0) && ! top.get (31) && ! top.get (95),
               "  step 127 lands in the top word");

        /* Step 32 only: word 1 bit 0 -> "1" then one zero word. */
        auto w1 = Mask::fromHex ("100000000");
        check (w1.get (32) && ! w1.get (0) && ! w1.get (31) && ! w1.get (33),
               "  an unpadded leading word still aligns from the right");

        check (! Mask::fromHex ("").get (0), "  an empty read is empty, not full");
    }

    {
        /*
         * THE TIME BASE, ACROSS THE WHOLE RATE LADDER.
         *
         * tg_block_setup ignores a transport tempo outside (1, 1000) and
         * falls back to 120 BPM. The preview used to express every step
         * duration as a 1/4 at 60000/msStep BPM, so every step shorter than
         * 60 ms asked for a tempo the engine refused: the scratch core then
         * believed a step lasted 500 ms while one step's worth of samples was
         * rendered, `frac` never reached `hold`, and THE GATE NEVER CLOSED.
         * The drawing was not cramped, it was wrong.
         *
         * Every entry below 60 ms in this list fails on that code.
         */
        std::puts ("\nthe preview's time base:");
        TranceGateProcessor p;
        setParam (p, "attack",  0.0f);
        setParam (p, "decay",   0.0f);
        setParam (p, "sustain", 1.0f);
        setParam (p, "hold",    0.5f);

        auto kneeAt = [] (const TgEnvelopeShape& sh)
        {
            for (size_t i = 0; i < sh.env.size(); ++i)
                if (sh.env[i] < 0.999f) return (double) i / (double) sh.env.size();
            return 1.0;
        };

        for (double ms : { 1.9, 15.6, 29.5, 62.5, 125.0, 500.0, 2000.0 })
        {
            setParam (p, "release", (float) juce::jmin (500.0, ms));
            const auto sh = TgEnvelopeShape::render (p, ms);

            check (std::abs (sh.gateMs - 0.5 * ms) < 0.01,
                   "  gate 50% of a " + juce::String (ms, 1) + " ms step is half of it",
                   juce::String (sh.gateMs, 3));
            check (std::abs (kneeAt (sh) - sh.releaseFrac()) < 0.01,
                   "  ...and the gate actually closes there",
                   juce::String (kneeAt (sh), 3) + " vs " + juce::String (sh.releaseFrac(), 3));
            check (! sh.clamped, "  ...on a rate the engine accepts");
        }
    }

    {
        /*
         * THE PATTERN SHAPE -- the plot above the pads. Everything here is
         * about the engine's behaviour ACROSS steps, which is exactly what a
         * one-step preview cannot show.
         */
        std::puts ("\nthe pattern shape:");
        TranceGateProcessor p;
        setParam (p, "attack",  0.0f);
        setParam (p, "decay",   0.0f);
        setParam (p, "sustain", 1.0f);
        setParam (p, "release", 0.0f);
        setParam (p, "hold",    1.0f);
        setParam (p, "length",  4.0f);
        p.engineSet ("pattern", "5");        /* steps 0 and 2 on */
        p.engineSet ("ties", "0");

        auto sh = TgPatternShape::render (p, 125.0, 747);
        check (sh.length == 4 && sh.perStep > 0, "  a 4-step pattern renders",
               juce::String (sh.length) + " x " + juce::String (sh.perStep));
        check ((int) sh.gain.size() == sh.length * sh.perStep,
               "  gain is exactly length x perStep");
        check ((int) sh.gain.size() >= 4 * 747,
               "  ...and at least four samples per column",
               juce::String ((int) sh.gain.size()));

        auto midOf = [&sh] (int step) { return sh.gain[(size_t) (step * sh.perStep + sh.perStep / 2)]; };
        check (midOf (0) > 0.99f && midOf (2) > 0.99f, "  the lit steps are open",
               juce::String (midOf (0), 3));
        check (midOf (1) < 0.01f && midOf (3) < 0.01f, "  the gaps are shut",
               juce::String (midOf (1), 3));

        /* A TIE HOLDS THROUGH -- the headline reason this plot exists. Gate
         * 50% would close step 0 half way, and the tie must override that. */
        setParam (p, "hold", 0.5f);
        p.engineSet ("pattern", "3");        /* steps 0 and 1 on */
        p.engineSet ("ties", "1");           /* step 0 ties into step 1 */
        /* Stated, not assumed: legato would hold these two together as well,
         * and then the untied case below would prove nothing. */
        setParam (p, "legato", 0.0f);
        auto tied = TgPatternShape::render (p, 125.0, 747);
        const float atThreeQuarters = tied.gain[(size_t) (tied.perStep * 3 / 4)];
        check (atThreeQuarters > 0.99f, "  a tied step is not cut short by Gate",
               juce::String (atThreeQuarters, 3));

        p.engineSet ("ties", "0");
        auto untied = TgPatternShape::render (p, 125.0, 747);
        check (untied.gain[(size_t) (untied.perStep * 3 / 4)] < 0.01f,
               "  ...and without the tie the same sample is shut",
               juce::String (untied.gain[(size_t) (untied.perStep * 3 / 4)], 3));

        /* Per-step accents survive the state round trip and scale the curve. */
        setParam (p, "hold", 1.0f);
        p.engineSet ("cursor", "0");
        p.engineSet ("step_amount", "0.5");
        auto accent = TgPatternShape::render (p, 125.0, 747);
        check (std::abs (accent.gain[(size_t) (accent.perStep / 2)] - 0.502f) < 0.01f,
               "  a step at amount 0.5 opens half way",
               juce::String (accent.gain[(size_t) (accent.perStep / 2)], 3));

        /* AMOUNT IS NOT BAKED IN -- it is an affine map applied at paint, so
         * moving it must not change a single rendered sample. */
        setParam (p, "amount", 1.0f);
        auto full = TgPatternShape::render (p, 125.0, 747);
        setParam (p, "amount", 0.4f);
        auto quiet = TgPatternShape::render (p, 125.0, 747);
        check (full.gain == quiet.gain, "  Amount does not change the render");

        auto twice = TgPatternShape::render (p, 125.0, 747);
        check (twice.gain == quiet.gain, "  two renders of one pattern are identical");
    }

    {
        /*
         * LEGATO THROUGH THE PLOT'S OWN PATH.
         *
         * The engine tests pin the behaviour; this pins that the picture
         * follows it, because TgPatternShape deliberately does NOT override
         * legato -- it renders the patch as it stands, which is the only
         * reason the plot can be trusted to match what you hear.
         */
        std::puts ("\nlegato in the pattern plot:");
        TranceGateProcessor p;
        setParam (p, "attack",  0.0f);
        setParam (p, "decay",   0.0f);
        setParam (p, "sustain", 1.0f);
        setParam (p, "release", 0.0f);
        setParam (p, "hold",    0.5f);
        setParam (p, "length",  4.0f);
        p.engineSet ("pattern", "f");        /* all four steps on */
        p.engineSet ("ties", "0");

        setParam (p, "legato", 0.0f);
        const auto apart = TgPatternShape::render (p, 125.0, 747);
        setParam (p, "legato", 1.0f);
        const auto joined = TgPatternShape::render (p, 125.0, 747);

        /* Three quarters through step 1: past the gate point, so it is shut
         * without legato and held open with it. */
        const auto at = [] (const TgPatternShape& sh)
        { return sh.gain[(size_t) (sh.perStep + sh.perStep * 3 / 4)]; };

        check (at (apart) < 0.01f, "  legato off, the gate has closed mid-step",
               juce::String (at (apart), 3));
        check (at (joined) > 0.99f, "  legato on, adjacent ON steps hold through",
               juce::String (at (joined), 3));
        check (apart.gain != joined.gain, "  ...so the plot actually changes");
    }

    {
        /*
         * A SLOT IS A WHOLE SOUND NOW, and the parameters have to follow it
         * out of the engine -- otherwise Live's automation lane keeps showing
         * the previous slot's envelope while the audio runs on the new one.
         */
        /*
         * A SLOT IS A PATTERN, NOT A PRESET -- and that is a decision, so it
         * is pinned rather than assumed.
         *
         * Per-slot parameters were built and then deliberately undone; the
         * engine stores rate, the envelope, Width, Amount and Join Neighbors
         * once, on the instance, and only the steps, the ties, the per-step
         * amounts and the Length belong to a slot. The block that used to sit
         * here asserted the opposite. Two of its five checks kept passing
         * after the revert -- reading back the last value written to a SHARED
         * field looks exactly like reading back a slot's own -- which is the
         * worse failure: a test that cannot fail is not coverage, it is
         * decoration. This asserts the behaviour that exists.
         */
        std::puts ("\na slot carries a pattern, not a sound:");
        TranceGateProcessor p;

        setParam (p, "slot", 0.0f);
        setParam (p, "attack", 11.0f);
        setParam (p, "length", 8.0f);
        setParam (p, "slot", 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        check (std::abs (getParam (p, "attack") - 11.0f) < 1.0f,
               "  the envelope does not change with the slot",
               juce::String (getParam (p, "attack"), 1));

        setParam (p, "attack", 150.0f);
        setParam (p, "length", 32.0f);
        setParam (p, "slot", 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        check (std::abs (getParam (p, "attack") - 150.0f) < 1.0f,
               "  ...in either direction -- it is one value, shared",
               juce::String (getParam (p, "attack"), 1));
        check (juce::approximatelyEqual (getParam (p, "length"), 8.0f),
               "  but Length is the slot's, and comes back with it",
               juce::String (getParam (p, "length"), 1));

        setParam (p, "slot", 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (juce::approximatelyEqual (getParam (p, "length"), 32.0f),
               "  ...and the other slot keeps its own",
               juce::String (getParam (p, "length"), 1));

        /* The pattern is the slot's too, and it survives a host save -- the
         * half of the old block that was testing something real. */
        p.engineSet ("pattern", "aa");
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        TranceGateProcessor q;
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        setParam (q, "slot", 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        eq (q.engineGet ("pattern"), "AA", "  each slot's pattern survives a save");
        check (juce::approximatelyEqual (getParam (q, "length"), 32.0f),
               "  ...with its length", juce::String (getParam (q, "length"), 1));
    }

    {
        /* The window has to stay on a screen. heightFor is a pure static, so
         * this costs nothing and catches a layout constant that grew. */
        std::puts ("\nthe editor's size:");
        /*
         * 1060 IS 1080 LESS A HOST'S WINDOW CHROME, and it is a real ceiling
         * rather than a round number -- the limit was 1000 until the config
         * actions needed a second settings row, and 128 steps now lands at
         * 1035.
         *
         * THE NEXT THING THAT NEEDS HEIGHT CANNOT HAVE IT. The top block is
         * already at its floor (two panels need 166 each and have 172), the
         * grid is 376 of the total and cannot shrink without scaling a step,
         * and the plots are as short as their captions and axis allow. The
         * answer after this is structural -- paging the grid, or folding a
         * section away -- not another bump.
         */
        check (TranceGateEditor::heightFor (128) <= 1060,
               "  128 steps still fits a 1080p screen",
               juce::String (TranceGateEditor::heightFor (128)) + " px");
    }

    {
        /*
         * % MODE IS ONE NUMBER READ TWO WAYS.
         *
         * The value must not move when the mode flips -- that is the whole
         * reason the two scales share a top -- while the TEXT must, because
         * the text is what a host puts in its automation lane and what the
         * user reads off the knob.
         */
        std::puts ("\nenvelope times in ms or % of the step:");
        TranceGateProcessor p;
        auto* attack = p.state().getParameter ("attack");
        auto* mode   = p.state().getParameter ("time_mode");
        check (mode != nullptr, "  the mode is a parameter");
        if (mode == nullptr) { std::puts ("\nFAIL"); return 1; }

        /* A stage is a percentage of the gate's width. At the default rate
         * and a full Width the gate is 125 ms, so 40% of it is 50 ms. */
        setParam (p, "attack", 40.0f);
        eq (attack->getCurrentValueAsText(), "50.0 ms", "  ms mode prints milliseconds");
        eq (p.engineGet ("attack"), "40.0", "  ...and the engine holds the percentage");

        mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));
        eq (p.engineGet ("time_mode"), "1", "  the mode reaches the engine");
        eq (attack->getCurrentValueAsText(), "40.00 %", "  % mode prints the percentage");
        eq (p.engineGet ("attack"), "40.0",
            "  ...and the stored value did not move");

        /* Back again, with nothing left behind. */
        mode->setValueNotifyingHost (mode->convertTo0to1 (0.0f));
        eq (attack->getCurrentValueAsText(), "50.0 ms", "  and back to milliseconds");

        /* Typing into a % readout is read as a share of the step, not as ms. */
        mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));
        attack->setValueNotifyingHost (attack->getValueForText ("20 %"));
        eq (p.engineGet ("attack"), "20.0", "  typing \"20 %\" stores twenty percent");
    }

    {
        /*
         * EVERY SOUND PARAMETER MUST CHANGE WHAT IS DRAWN.
         *
         * Both plots cache their render against a stamp of the engine keys
         * that shape the sound, and a key left out of that stamp does not
         * fail loudly -- the picture simply stops following the control and
         * comes right again when you touch an unrelated knob. That is what
         * "I cannot switch back to linear" turned out to be: `curve` was in
         * neither stamp and `time_mode` in only one.
         *
         * Rendering the shape either side of each parameter is the check that
         * cannot drift: it does not ask what the stamp contains, it asks
         * whether the drawing moved.
         */
        std::puts ("\nevery sound parameter reaches the plots:");

        struct Move { const char* id; float from, to; bool env, pat; };
        /*
         * `legato` is pattern-only ON PURPOSE: the envelope plot draws ONE
         * envelope in isolation, and legato is about whether the NEXT step
         * retriggers. Expecting it there would be expecting the wrong plot to
         * know about it.
         */
        const Move moves[] = {
            { "attack",    10.0f,  60.0f, true,  true  },
            { "decay",     30.0f,  90.0f, true,  true  },
            { "sustain",    0.5f,   0.2f, true,  true  },
            { "release",   30.0f,  90.0f, true,  true  },
            { "hold",       0.8f,   0.4f, true,  true  },
            { "legato",     0.0f,   1.0f, false, true  },
            /*
             * `time_mode` is NOT here any more. It used to change the sound --
             * it chose whether a stage was milliseconds or a share of the
             * step -- and now it chooses only how that stage is PRINTED, so a
             * plot that redrew for it would be redrawing for nothing. Its
             * effect on the text is tested where the text is.
             */
            { "curve",      0.0f,   1.0f, true,  true  },
        };

        for (const auto& m : moves)
        {
            /*
             * A FRESH PATCH PER PARAMETER, and one where every stage is
             * actually reached: attack 10, decay 30, sustain 0.5 held to 80%
             * of a 125 ms step, then release. Chaining the moves instead put
             * attack past the gate's close, so decay and sustain never ran
             * and "changing them does nothing" was the truth about the setup
             * rather than about the plots.
             */
            TranceGateProcessor p;
            setParam (p, "length", 4.0f);
            p.engineSet ("pattern", "f");        /* four steps, all on */
            p.engineSet ("ties", "0");
            setParam (p, "attack",  10.0f);
            setParam (p, "decay",   30.0f);
            setParam (p, "sustain",  0.5f);
            setParam (p, "release", 30.0f);
            setParam (p, "hold",     0.8f);
            setParam (p, m.id, m.from);

            const auto stampBefore    = TranceGateEditor::soundStamp   (p.engineGet ("params"));
            const auto patStampBefore = TranceGateEditor::patternStamp (p.engineGet ("params"));
            const auto beforeEnv = TgEnvelopeShape::render (p, 125.0).env;
            const auto beforePat = TgPatternShape::render (p, 125.0, 200).gain;
            setParam (p, m.id, m.to);
            const auto afterEnv = TgEnvelopeShape::render (p, 125.0).env;
            const auto afterPat = TgPatternShape::render (p, 125.0, 200).gain;

            /* THE STAMP IS THE ACTUAL INVARIANT. Rendering either side of a
             * parameter proves the RENDERER responds -- which it always did,
             * even while the plot sat stale -- because it bypasses the cache
             * the stamp guards. Both halves are needed: the renderer must
             * move, and the cached picture must be thrown away. */
            /* patternStamp is a superset of soundStamp, so it must move for
             * everything; soundStamp only for what the envelope plot draws.
             * Checking both ways round is what keeps a key from being quietly
             * demoted to the pattern plot when the envelope needs it. */
            check (TranceGateEditor::patternStamp (p.engineGet ("params")) != patStampBefore,
                   juce::String ("  ") + m.id + " moves the pattern plot's cache key");
            if (m.env)
                check (TranceGateEditor::soundStamp (p.engineGet ("params")) != stampBefore,
                       juce::String ("  ...and the envelope plot's"));

            if (m.env)
                check (beforeEnv != afterEnv,
                       juce::String ("  ") + m.id + " changes the envelope plot");
            if (m.pat)
                check (beforePat != afterPat,
                       juce::String ("  ") + m.id + " changes the pattern plot");
        }
    }

    {
        /*
         * WHAT THE HOST SEES US DO.
         *
         * This is the defect the whole change exists for, and it was
         * invisible to every test in the suite: syncParamsFromEngine wrote
         * all twelve parameters unconditionally and without gestures on every
         * slot change -- including Slot back at itself. A host cannot tell
         * that from a user grabbing the knob, so in Live the first point of a
         * Slot lane overrode the lane, and eleven others with it.
         *
         * An AudioProcessorListener is exactly what the VST3 and AU wrappers
         * attach, so this records what a host would record. Gestures are the
         * discriminator: the test's own writes carry none, so every gesture
         * seen here was emitted by the PLUGIN.
         */
        std::puts ("\nwhat the host sees the plugin write:");

        struct Recorder : juce::AudioProcessorListener
        {
            void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override
            { ++changes; }
            void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
            void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int i) override
            { ++begins; touched.addIfNotAlreadyThere (i); }
            void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override
            { ++ends; }
            int begins = 0, ends = 0, changes = 0;
            juce::Array<int> touched;
            void reset() { begins = ends = changes = 0; touched.clearQuick(); }
        } rec;

        TranceGateProcessor p;
        p.addListener (&rec);

        /* Two slots with the SAME length: a switch between them changes
         * nothing, so the plugin must say nothing. */
        setParam (p, "slot", 0.0f);
        setParam (p, "length", 16.0f);
        setParam (p, "slot", 1.0f);
        setParam (p, "length", 16.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        rec.reset();
        setParam (p, "slot", 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (rec.begins == 0,
               "  a slot switch that changes nothing writes nothing",
               juce::String (rec.begins) + " gestures");
        /* THE COUNT IS WHAT WOULD HAVE CAUGHT THE OLD CODE. Gestures alone
         * would not: there were none to count. One notification is the test's
         * own write of Slot; the unconditional recall added twelve more. */
        check (rec.changes <= 1,
               "  ...and the host hears one notification, not thirteen",
               juce::String (rec.changes));

        /* Now one that does differ: exactly Length moves, and Slot -- the
         * parameter the host is driving -- must not be written back. */
        setParam (p, "slot", 1.0f);
        setParam (p, "length", 7.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        rec.reset();
        setParam (p, "slot", 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);

        const int slotIdx = p.getParameters().indexOf (p.state().getParameter ("slot"));
        check (! rec.touched.contains (slotIdx),
               "  ...and never writes Slot back at the host that moved it");
        check (rec.begins == rec.ends,
               "  every write the plugin makes is a balanced gesture",
               juce::String (rec.begins) + " begins, " + juce::String (rec.ends) + " ends");
        check (rec.begins <= 1,
               "  ...and only the value that actually differs is written",
               juce::String (rec.begins) + " gestures");

        p.removeListener (&rec);
    }

    {
        /*
         * NOTHING IS DISCARDED.
         *
         * The old listener returned early whenever a refresh was in flight,
         * so an automation point arriving in that window was dropped -- not
         * deferred, dropped -- and for VST3 during playback the audio thread
         * is the only route a lane has. A change from a non-message thread
         * must now be STAGED and applied by the next block, never lost.
         */
        std::puts ("\na change off the message thread is staged, not dropped:");

        TranceGateProcessor p;
        p.prepareToPlay (48000.0, 64);          /* rolling: a block will come */
        setParam (p, "attack", 10.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        struct Writer : juce::Thread
        {
            Writer (TranceGateProcessor& proc) : juce::Thread ("w"), p (proc) {}
            void run() override
            {
                auto* q = p.state().getParameter ("attack");
                q->setValueNotifyingHost (q->convertTo0to1 (77.0f));
            }
            TranceGateProcessor& p;
        } w (p);

        w.startThread();
        w.waitForThreadToExit (2000);

        check (p.engineGet ("attack").getFloatValue() < 20.0f,
               "  the engine has not been touched from the other thread",
               p.engineGet ("attack"));

        juce::AudioBuffer<float> buf (2, 64);
        juce::MidiBuffer midi;
        buf.clear();
        p.processBlock (buf, midi);

        check (std::abs (p.engineGet ("attack").getFloatValue() - 77.0f) < 0.5f,
               "  ...and the block applied it", p.engineGet ("attack"));

        p.releaseResources();
    }

    {
        /*
         * THE SCOPE DRAWS THE GAIN THAT WAS ACTUALLY APPLIED.
         *
         * The capture is compared against the engine's own per-sample gain,
         * driven with DC so that a column's min/max IS the gain over that
         * column and any difference is the capture's doing rather than the
         * signal's.
         *
         * It is here because the scope was visibly WRONG and looked merely
         * odd: it read the phase once after the engine ran and filed the
         * whole block under that one column, so every value arrived about a
         * block late. On a rising edge late reads low, and the gated band
         * climbed slower than the curve drawn over it -- the peak error was
         * 0.114 of full scale at attack 25%%, which is a tenth of the picture.
         * A lag is exactly the kind of fault that looks like a design choice,
         * so it is pinned with a number.
         */
        std::puts ("\nthe signal scope tracks the gain:");

        constexpr double SR = 48000.0;
        constexpr int    BL = 128;
        constexpr int    COLS = TranceGateProcessor::Capture::columns;

        /* 16 steps of 1/16 at 120 BPM = 2 s. */
        const int cycle = (int) (2.0 * SR + 0.5);

        struct Rolling : juce::AudioPlayHead
        {
            double ppq = 0.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setIsPlaying (true);
                p.setBpm (120.0);
                p.setPpqPosition (ppq);
                return p;
            }
        } head;

        TranceGateProcessor p;
        p.setPlayHead (&head);
        p.prepareToPlay (SR, BL);
        setParam (p, "rate",    7.0f);      /* index -> 1/16 */
        setParam (p, "length",  16.0f);     /* the DISPLAY value: 16 steps */
        setParam (p, "attack",  25.0f);
        setParam (p, "decay",   30.0f);
        setParam (p, "sustain", 0.7f);
        setParam (p, "release", 20.0f);
        setParam (p, "hold",    0.8f);
        setParam (p, "amount",  1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        p.engineSet ("pattern", "ffff");

        juce::AudioBuffer<float> buf (2, BL);
        juce::MidiBuffer midi;
        for (int n = 0; n < cycle * 2; n += BL)
        {
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (buf.getWritePointer (ch), 1.0f, BL);
            head.ppq = (double) n / SR * 2.0;
            p.processBlock (buf, midi);
        }

        /*
         * The engine's own gain, one sample at a time, binned the same way.
         *
         * THE PATCH IS COPIED FROM THE PROCESSOR rather than typed out again:
         * the plugin's parameters carry display units (Length's knob says 16
         * where the engine's key says index 15) and a mismatch there would
         * show up as a difference in the picture with nothing to say it was
         * the patch and not the capture. A state blob cannot drift.
         */
        std::vector<float> truth ((size_t) COLS, -2.0f);
        {
            tg_core_t* c = tg_core_create (SR);
            tg_core_set_param (c, "state", p.engineGet ("state").toRawUTF8());
            char a[512];
            tg_core_get_param (c, "params", a, sizeof (a));
            /* Field by field, and the floats to a tolerance: a state blob is
             * a PATCH and carries patch precision, so a parameter that landed
             * on 30.0000019 through the host's 0..1 round trip comes back as
             * 30. That is the blob's job, not a fault, and it is six decimal
             * orders below anything this test is looking at. */
            {
                const auto got  = juce::StringArray::fromTokens (p.engineGet ("params"), ":", "");
                const auto want = juce::StringArray::fromTokens (juce::String (a), ":", "");
                bool same = got.size() == want.size() && got.size() == 13;
                for (int f = 0; same && f < got.size(); ++f)
                    same = (f < 6) ? got[f] == want[f]
                                   : std::abs (got[f].getFloatValue() - want[f].getFloatValue()) < 1.0e-3f;
                check (same, "both engines hold the same patch",
                       "processor " + p.engineGet ("params") + " / reference " + juce::String (a));
            }
            for (int n = 0; n < cycle * 2; ++n)
            {
                float l = 1.0f, r = 1.0f;
                tg_transport_t t { 1, (double) n / SR * 2.0, 120.0f };
                tg_core_process_f32_split (c, &l, &r, 1, &t);
                if (n < cycle) continue;
                const int col = juce::jlimit (0, COLS - 1,
                                              (int) (tg_core_phase01 (c) * COLS));
                truth[(size_t) col] = juce::jmax (truth[(size_t) col], l);
            }
            tg_core_destroy (c);
        }

        const auto& cap = p.capture();
        float worst = 0.0f; int worstCol = -1; int compared = 0;
        for (int i = 0; i < COLS; ++i)
        {
            if (truth[(size_t) i] < -1.0f) continue;
            const float d = cap.wetHi[i].load() - truth[(size_t) i];
            ++compared;
            if (std::abs (d) > std::abs (worst)) { worst = d; worstCol = i; }
        }

        check (compared > COLS / 2, "every column of the sweep was captured",
               juce::String (compared) + "/" + juce::String (COLS));
        /* One column is 3.9 ms here and the steepest attack covers about a
         * seventh of full scale in one -- so a fault worth seeing is bigger
         * than a column, and 0.02 is comfortably under that and far under the
         * 0.114 the lag produced. */
        check (std::abs (worst) < 0.02f,
               "  and it is the gain the engine applied, not a lagged copy",
               "worst " + juce::String (worst, 4) + " at col " + juce::String (worstCol));

        p.setPlayHead (nullptr);
        p.releaseResources();
    }

    {
        /*
         * LENGTH'S MAGNETS.
         *
         * The behaviour that matters is not "it snaps" -- it is that it
         * snaps WITHOUT taking anything away. A dead zone would pass a
         * "sticks to 16" test and still be wrong, because 15 and 17 would
         * have stopped existing. So the assertions are: it pulls, it pulls
         * harder at a bar than a beat, and it is still monotonic, which is
         * what keeps every value reachable.
         */
        std::puts ("\nthe Length knob's magnets:");

        UvKnob k;
        k.setRange (1.0, 128.0, 1.0);
        k.setMagnets (4.0, 16.0);            /* 1/16: beat 4, bar 16 */
        const double vpp = 127.0 / 250.0;    /* the coarse drag, in value per pixel */

        /* Near a bar, the value is dragged towards it. */
        const double near16 = k.magnetise (17.0, vpp);
        check (near16 < 17.0 && near16 > 16.0,
               "  a value just past a bar is pulled back toward it",
               juce::String (near16, 3));

        /* ...and harder than the same distance from a beat. */
        const double near8 = k.magnetise (9.0, vpp);
        check ((17.0 - near16) > (9.0 - near8),
               "  ...harder at a bar than at a beat",
               juce::String (17.0 - near16, 3) + " vs " + juce::String (9.0 - near8, 3));

        /* THE ONE THAT RULES OUT A DEAD ZONE. Monotonic across the whole
         * window means the mapping is a bijection: nothing in it has been
         * made unreachable, 15 and 17 included. */
        bool mono = true;
        double prev = -1.0e9;
        for (double v = 12.0; v <= 20.0; v += 0.05)
        {
            const double m = k.magnetise (v, vpp);
            if (m < prev - 1.0e-9) { mono = false; break; }
            prev = m;
        }
        check (mono, "  the warp is monotonic, so no value is unreachable");

        /* Exactly on a magnet, nothing moves. */
        check (std::abs (k.magnetise (32.0, vpp) - 32.0) < 1.0e-9,
               "  a value already on a bar is left alone");

        /* Far from any magnet, nothing moves either. */
        check (std::abs (k.magnetise (22.0, vpp) - 22.0) < 1.0e-9,
               "  and a value between magnets is untouched",
               juce::String (k.magnetise (22.0, vpp), 3));

        /* A rate whose bar lands outside the range must not invent a magnet
         * at the range's end, which a naive round() does. */
        UvKnob wide;
        wide.setRange (1.0, 128.0, 1.0);
        wide.setMagnets (0.0, 128.0);
        check (std::abs (wide.magnetise (3.0, vpp) - 3.0) < 1.0e-9,
               "  a multiple below the range is not treated as a magnet",
               juce::String (wide.magnetise (3.0, vpp), 3));

        /* No magnets set: a plain knob is untouched. */
        UvKnob plain;
        plain.setRange (0.0, 1.0, 0.0);
        check (std::abs (plain.magnetise (0.37, 0.004) - 0.37) < 1.0e-9,
               "  a knob with no magnets behaves as it always did");
    }

    {
        /*
         * THE INDEX CHAIN, WHICH NOTHING CHECKS IN A RELEASE BUILD.
         *
         * Host automation arrives as an INT INDEX, and the processor turns it
         * into an engine key through three tables that must agree with
         * makeLayout's declaration order: the Pid enum, the id table beside
         * it, and drainLocked's wire[]. The constructor does check -- with
         * jassert, which this build compiles out, and a static_assert that
         * only counts entries rather than reading them.
         *
         * A reorder of makeLayout would therefore send every lane to the
         * wrong engine key, silently, and the UI would go on working, because
         * an attachment carries its own string id and never uses the index.
         * That is the worst shape a bug can have: invisible from the surface
         * anyone tests by hand.
         */
        std::puts ("\nthe parameter index chain:");
        TranceGateProcessor p;

        static const char* const order[] = { "rate", "length", "slot", "legato",
                                             "time_mode", "curve", "amount", "hold",
                                             "attack", "decay", "sustain", "release" };

        const auto& all = p.getParameters();
        check (all.size() >= (int) std::size (order),
               "  the processor publishes at least the twelve",
               juce::String (all.size()));

        bool ordered = true;
        for (int i = 0; i < (int) std::size (order) && i < all.size(); ++i)
        {
            auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (all[i]);
            if (w == nullptr || w->paramID != order[i])
            {
                ordered = false;
                std::printf ("      index %d is %s, expected %s\n", i,
                             w ? w->paramID.toRawUTF8() : "(not a WithID)", order[i]);
            }
        }
        check (ordered, "  every index maps to the id the engine table expects");

        /*
         * And the end-to-end version of the same claim: drive each parameter
         * BY INDEX, the way a host does, and read the engine back. This is
         * what actually proves the enum, the id table and wire[] agree --
         * the loop above only proves the first two do.
         */
        struct Probe { int index; const char* key; float display; const char* want; };
        const Probe probes[] = {
            { 0,  "rate",      9.0f,   "1/32" },
            { 1,  "length",   64.0f,     "63" },
            { 2,  "slot",      3.0f,      "3" },
            { 3,  "legato",    1.0f,      "1" },
            { 4,  "time_mode", 1.0f,      "1" },
            { 5,  "curve",     2.0f,      "2" },
            { 6,  "amount",    0.5f,   "0.50" },
            { 7,  "hold",      0.6f,   "0.60" },
            { 8,  "attack",   40.0f,   "40.0" },
            { 9,  "decay",    50.0f,   "50.0" },
            { 10, "sustain",   0.3f,   "0.30" },
            { 11, "release",  60.0f,   "60.0" },
        };

        for (auto& pr : probes)
        {
            if (pr.index >= all.size()) continue;
            if (auto* w = dynamic_cast<juce::RangedAudioParameter*> (all[pr.index]))
                w->setValueNotifyingHost (w->convertTo0to1 (pr.display));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            eq (p.engineGet (pr.key), pr.want,
                juce::String ("  index ") + juce::String (pr.index) + " -> " + pr.key);
        }
    }

    {
        /*
         * A SUSPENDED PLUGIN MUST NOT STRAND A STAGED VALUE.
         *
         * `rolling` says "a block is coming, let processBlock apply this".
         * It is set in prepareToPlay and cleared in releaseResources -- but a
         * VST3 host that calls setProcessing(false) without deactivating
         * reaches NEITHER, so the flag stays true while no block will ever
         * come. A change staged in that window is not posted to the message
         * thread and not drained: it sits with its dirty bit set.
         *
         * It is not lost -- any later drain applies it -- but until then the
         * engine holds the old value, so getStateInformation() in that window
         * saves a patch MISSING the change. Draining on the way out is what
         * closes it.
         */
        std::puts ("\nsuspending does not strand a staged change:");

        TranceGateProcessor p;
        p.prepareToPlay (48000.0, 64);
        setParam (p, "attack", 10.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

        struct Writer : juce::Thread
        {
            Writer (TranceGateProcessor& proc) : juce::Thread ("w"), p (proc) {}
            void run() override
            {
                auto* q = p.state().getParameter ("attack");
                q->setValueNotifyingHost (q->convertTo0to1 (55.0f));
            }
            TranceGateProcessor& p;
        } w (p);

        w.startThread();
        w.waitForThreadToExit (2000);
        check (p.engineGet ("attack").getFloatValue() < 20.0f,
               "  staged off-thread, not yet in the engine", p.engineGet ("attack"));

        /* No processBlock ever runs. The host just stops. */
        p.releaseResources();
        check (std::abs (p.engineGet ("attack").getFloatValue() - 55.0f) < 0.5f,
               "  ...and releaseResources applies it rather than losing it",
               p.engineGet ("attack"));

        /* Which is what makes a save in that window honest. */
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        /* The FIELD, not the digits: "55" turns up elsewhere in a blob of
         * masks and depths, so a substring search would pass on its own. */
        const auto blob = juce::String::fromUTF8 ((const char*) saved.getData(),
                                                  (int) saved.getSize());
        check (blob.contains ("\"attack\":55"),
               "  ...so a patch saved while suspended carries it",
               blob.fromFirstOccurrenceOf ("\"attack\"", true, false).upToFirstOccurrenceOf (",", false, false));
    }

    {
        /*
         * JOIN NEIGHBORS CANNOT ACT AT THE FACTORY DEFAULTS, AND THAT IS THE
         * BEHAVIOUR WORTH PINNING.
         *
         * It was reported as "the audio does not change". It does not, and
         * the parameter path is fine: the engine reads legato in two places
         * and both are switched off by the defaults.
         *
         *   - the gate rule is guarded by `hold < 1.0`, and Width defaults to
         *     100%, so it never runs;
         *   - the boundary retrigger is arithmetic that cancels at Sustain
         *     100%: decay gives env = 1 - (1-sustain)*w = 1, so attack ramps
         *     from 1.0 to 1.0 and every sample is unchanged.
         *
         * Both halves are asserted, because a future change that makes the
         * default case audible is as much a regression as one that makes the
         * working case silent.
         */
        std::puts ("\nJoin Neighbors, where it can and cannot act:");
        TranceGateProcessor p;
        setParam (p, "length", 4.0f);
        p.engineSet ("pattern", "f");        /* four adjacent ON steps */
        p.engineSet ("ties", "0");
        setParam (p, "attack",  0.0f);
        setParam (p, "decay",   0.0f);
        setParam (p, "release", 0.0f);

        auto renderWith = [&] (float widthPct, float sustain, bool join)
        {
            setParam (p, "hold",    widthPct);
            setParam (p, "sustain", sustain);
            setParam (p, "legato",  join ? 1.0f : 0.0f);
            return TgPatternShape::render (p, 125.0, 400).gain;
        };

        check (renderWith (1.0f, 1.0f, false) == renderWith (1.0f, 1.0f, true),
               "  at Width 100% and Sustain 100% it is bit-identical");

        check (renderWith (0.5f, 1.0f, false) != renderWith (0.5f, 1.0f, true),
               "  below Width 100% it holds the gate through");

        /* A retrigger is only audible if the attack takes TIME: with a
         * zero-length attack env_enter walks straight through to sustain and
         * the two are identical again, which is a property of the envelope
         * rather than of legato. */
        setParam (p, "attack", 40.0f);
        check (renderWith (1.0f, 0.4f, false) != renderWith (1.0f, 0.4f, true),
               "  below Sustain 100% it stops the re-articulation");
        setParam (p, "attack", 0.0f);

        /*
         * AND THE EFFECT NOTHING DOCUMENTED: with adjacent ON steps of
         * DIFFERENT per-step depths, legato skips the step_level latch, so
         * the gain stays at the previous step's depth through the new one.
         * Audible at any Width and Sustain, and the one legato behaviour that
         * does not depend on either.
         */
        setParam (p, "hold",    1.0f);
        setParam (p, "sustain", 1.0f);
        p.engineSet ("cursor", "1");
        p.engineSet ("step_amount", "0.3");   /* step 1 quieter than step 0 */

        setParam (p, "legato", 0.0f);
        const auto apart  = TgPatternShape::render (p, 125.0, 400).gain;
        setParam (p, "legato", 1.0f);
        const auto joined = TgPatternShape::render (p, 125.0, 400).gain;
        check (apart != joined,
               "  ...and it carries the previous step's depth across a join");
    }

    std::printf ("\n%s (%d checks, %d failures)\n",
                 failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
