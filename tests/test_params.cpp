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
                           "hold", "amount", "legato", "slot", "length" })
            eq (p.engineGet (key), bareGet (key),
                juce::String ("  ") + key + " default survives construction");

        tg_core_destroy (bare);
    }

    {
        std::puts ("\nevery parameter reaches the engine:");
        TranceGateProcessor p;

        setParam (p, "attack",  123.0f);  eq (p.engineGet ("attack"),  "123.0", "  attack");
        setParam (p, "decay",   250.0f);  eq (p.engineGet ("decay"),   "250.0", "  decay");
        setParam (p, "release", 500.0f);  eq (p.engineGet ("release"), "500.0", "  release");
        setParam (p, "sustain",   0.25f); eq (p.engineGet ("sustain"),  "0.25", "  sustain");
        setParam (p, "hold",      0.50f); eq (p.engineGet ("hold"),     "0.50", "  gate");
        setParam (p, "amount",    0.75f); eq (p.engineGet ("amount"),   "0.75", "  amount");
        setParam (p, "legato",    1.0f);  eq (p.engineGet ("legato"),      "1", "  legato");

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

        /* The assumption pullParam rounds against: an int parameter's raw
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
        setParam (p, "release", 200.0f);

        /* A 1/4 step at 120 BPM is 500 ms. Gate 50% opens the release at
         * 250 ms, and 200 ms of release finishes well inside the step. */
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
        setParam (p, "release", 500.0f);
        auto fast = TgEnvelopeShape::render (p, 43.0);
        check (fast.truncated, "  a 500 ms release on a 43 ms step is cut off");
        check (fast.levelAt (fast.stepFrac()) > 0.8f,
               "  ...with most of the release still to go at the step edge",
               juce::String (fast.levelAt (fast.stepFrac()), 3));

        /* THE SAME PATCH AT TWO RATES IS TWO PICTURES -- now because the step
         * edge falls somewhere different on the axis, which is the fact the
         * drawing exists to show. */
        auto sameSlow = TgEnvelopeShape::render (p, 500.0);
        check (fast.stepFrac() < 0.2 && sameSlow.stepFrac() > 0.6,
               "  one patch, two rates, two shapes",
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
        setParam (p, "release", 200.0f);
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
         * THE CASE THE WHOLE CHANGE EXISTS FOR: a decay fifteen times longer
         * than the step. The axis must be the decay's, the step must be a
         * mark near the left-hand edge, and the curve must actually come down
         * on screen rather than being cut off by a gate at 3%.
         */
        setParam (p, "attack",  10.0f);
        setParam (p, "decay",   442.0f);
        setParam (p, "sustain", 0.0f);
        setParam (p, "release", 20.0f);
        setParam (p, "hold",    0.53f);
        const auto wide = TgEnvelopeShape::render (p, 29.53);
        check (std::abs (wide.spanMs - (10.0 + 442.0 + 20.0) * 1.04) < 1.0,
               "  a 442 ms decay on a 29.5 ms step spans the decay, not the step",
               juce::String (wide.spanMs, 1));
        check (wide.stepFrac() < 0.07,
               "  ...with the step edge a mark near the left",
               juce::String (wide.stepFrac(), 4));
        check (wide.gateFrac() < wide.releaseFrac(),
               "  ...the gate closes before the dialled decay ends",
               juce::String (wide.gateFrac(), 4) + " vs " + juce::String (wide.releaseFrac(), 4));
        check (! wide.envDialled.empty(),
               "  ...so the dialled shape is drawn as a second trace");
        const float ghostMid = wide.envDialled.empty() ? 0.0f
                             : wide.envDialled[wide.envDialled.size() / 2];
        check (ghostMid > 0.4f && ghostMid < 0.6f,
               "  ...whose decay is HALF WAY DOWN at mid-axis, not long gone",
               juce::String (ghostMid, 3));
        /* The SOLID trace is the gated one, so with the gate at 53% of a
         * 29.5 ms step it is long over by mid-axis. That is the difference
         * between the two traces, and the reason there are two. */
        check (wide.levelAt (0.5) < 0.01f,
               "  ...while the gated trace is already shut there",
               juce::String (wide.levelAt (0.5), 3));
        check (wide.env.back() < 0.01f, "  ...reaching zero on screen",
               juce::String (wide.env.back(), 4));

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
        check (g25.env != g75.env, "  turning Gate moves the gated trace");
        check (g25.envDialled == g75.envDialled,
               "  ...and leaves the dialled one where it was");
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
        std::puts ("\na slot carries its own sound:");
        TranceGateProcessor p;
        setParam (p, "slot", 0.0f);
        setParam (p, "rate", 5.0f);          /* 1/8 */
        setParam (p, "attack", 11.0f);
        setParam (p, "slot", 1.0f);
        setParam (p, "rate", 9.0f);          /* 1/32 */
        setParam (p, "attack", 222.0f);

        setParam (p, "slot", 0.0f);
        p.syncParamsFromEngine();
        check (std::abs (getParam (p, "attack") - 11.0f) < 1.0f,
               "  slot 0's attack comes back to the parameter",
               juce::String (getParam (p, "attack"), 1));
        eq (p.engineGet ("rate"), "1/8", "  ...and its rate");

        setParam (p, "slot", 1.0f);
        p.syncParamsFromEngine();
        check (std::abs (getParam (p, "attack") - 222.0f) < 1.0f,
               "  slot 1's differ, and follow the switch",
               juce::String (getParam (p, "attack"), 1));
        eq (p.engineGet ("rate"), "1/32", "  ...with its own rate");

        /* And across a host save, which is the engine's blob verbatim. */
        juce::MemoryBlock saved;
        p.getStateInformation (saved);
        TranceGateProcessor q;
        q.setStateInformation (saved.getData(), (int) saved.getSize());
        setParam (q, "slot", 0.0f);
        q.syncParamsFromEngine();
        check (std::abs (getParam (q, "attack") - 11.0f) < 1.0f,
               "  both slots survive a host save",
               juce::String (getParam (q, "attack"), 1));
    }

    {
        /* The window has to stay on a screen. heightFor is a pure static, so
         * this costs nothing and catches a layout constant that grew. */
        std::puts ("\nthe editor's size:");
        check (TranceGateEditor::heightFor (128) <= 1000,
               "  128 steps still fits a 1080p screen",
               juce::String (TranceGateEditor::heightFor (128)) + " px");
    }

    std::printf ("\n%s (%d checks, %d failures)\n",
                 failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
