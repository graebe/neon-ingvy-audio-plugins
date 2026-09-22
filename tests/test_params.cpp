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
         * THE ENVELOPE CURVE. Two facts are worth pinning, and both are about
         * the axis being ONE STEP rather than abstract time -- which is the
         * only reason to draw it at all.
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
        setParam (p, "release", 250.0f);

        /* A 1/4 step at 120 BPM is 500 ms. Gate 50% gives the release the
         * second half -- 250 ms, which a 250 ms release fills exactly. */
        auto slow = TgEnvelopeShape::render (p, 500.0);
        check (! slow.env.empty(), "  a slow step renders",
               juce::String ((int) slow.env.size()) + " samples");
        check (slow.env.back() < 0.05f, "  a release that fits reaches zero",
               juce::String (slow.env.back(), 3));
        check (! slow.truncated, "  ...and is not reported as cut off");

        /* A 1/32 step at 174 BPM is ~43 ms. The same release cannot even
         * start to finish, and the curve must say so rather than quietly
         * rescaling -- which is the entire argument for a real time axis. */
        setParam (p, "release", 500.0f);
        auto fast = TgEnvelopeShape::render (p, 43.0);
        check (fast.truncated, "  a 500 ms release on a 43 ms step is cut off",
               juce::String (fast.env.back(), 3));
        check (fast.env.back() > 0.8f, "  ...with most of the release still to go",
               juce::String (fast.env.back(), 3));

        /* THE SAME PATCH AT TWO RATES IS TWO PICTURES. On a normalised axis
         * these would be identical, which is the lie this avoids. */
        auto sameSlow = TgEnvelopeShape::render (p, 500.0);
        check (sameSlow.env.back() < fast.env.back() - 0.3f,
               "  one patch, two rates, two shapes",
               juce::String (sameSlow.env.back(), 3) + " vs " + juce::String (fast.env.back(), 3));

        /* Gate positions where release BEGINS. */
        auto kneeAt = [] (const TgEnvelopeShape& sh)
        {
            for (size_t i = 0; i < sh.env.size(); ++i)
                if (sh.env[i] < 0.95f) return (double) i / (double) sh.env.size();
            return 1.0;
        };
        setParam (p, "release", 200.0f);
        setParam (p, "hold", 0.25f);
        const double kneeEarly = kneeAt (TgEnvelopeShape::render (p, 500.0));
        setParam (p, "hold", 0.75f);
        const double kneeLate  = kneeAt (TgEnvelopeShape::render (p, 500.0));

        check (std::abs (kneeEarly - 0.25) < 0.04, "  gate 25% puts the knee a quarter in",
               juce::String (kneeEarly, 3));
        check (std::abs (kneeLate  - 0.75) < 0.04, "  gate 75% puts it three quarters in",
               juce::String (kneeLate, 3));
        check (kneeLate > kneeEarly, "  ...so moving Gate moves the knee");

        /* The curve IS the engine, not a second formula -- so a sustain of
         * 0.5 must actually plateau at 0.5. */
        setParam (p, "decay", 10.0f);
        setParam (p, "sustain", 0.5f);
        setParam (p, "hold", 1.0f);
        auto half = TgEnvelopeShape::render (p, 500.0);
        const float mid = half.env[half.env.size() / 2];
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

    std::printf ("\n%s (%d checks, %d failures)\n",
                 failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
