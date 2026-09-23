/*
 * Renders the editor to PNG, so the design can be LOOKED AT rather than
 * reasoned about. It is how the panels-over-knobs z-order bug, the unstyled
 * readout and the 200px of dead space under a short pattern were all found --
 * none of which fails a build or an assertion.
 *
 * The states are chosen to exercise what the design system actually specifies
 * rather than only the pattern length: a control at each end of its range, a
 * playhead lit, and the envelope's truncated release. Compare each against
 * the matching preview under the system's own components/.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../plugins/trance-gate/Source/PluginProcessor.h"
#include "../plugins/trance-gate/Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out = juce::File (argc > 1 ? argv[1] : "/tmp");

    /* `hold` and `release` are set only for tg_cut: a long release on a short
     * step is the case the envelope panel exists to make visible, and a
     * marker that overlaps the curve is not something an assertion catches.
     *
     * tg_ends puts every knob at one end of its range or the other, which is
     * where a value arc drawn from the wrong angle shows up -- at the middle
     * of a range a 270-degree arc and a 360-degree one look much the same.
     * tg_play lights the playhead. */
    /*
     * KEYBOARD FOCUS IS NOT IN HERE, and that is a limitation rather than an
     * oversight: a component only reports focus once its window is active,
     * which a headless render never is. A focus shot would come out looking
     * exactly like an unfocused one -- a green tick for a thing that was
     * never drawn, which is worse than no shot. glow-focus is checked by
     * tabbing through the plugin in a host.
     */
    struct Shot { const char* name; int length; int rate;
                  float hold, release, decay, sustain, attack; bool ends, playing;
                  bool legato, dense; int curve; bool signal; };
    const Shot shots[] = {
        { "tg_16",   16,  7,  1.0f,  20.0f,  20.0f, 1.0f, 2.0f, false, false, false, false, 0, false },
        { "tg_64",   64,  9,  1.0f,  20.0f,  20.0f, 1.0f, 2.0f, false, false, false, false, 0, false },
        { "tg_128", 128, 12,  1.0f,  20.0f,  20.0f, 1.0f, 2.0f, false, false, false, false, 0, false },
        { "tg_cut",  32,  9,  0.5f, 500.0f,  20.0f, 1.0f, 2.0f, false, false, false, false, 0, false },
        { "tg_ends", 16,  0,  0.05f, 0.0f,   20.0f, 1.0f, 2.0f, true,  false, false, false, 0, false },
        { "tg_play", 32,  7,  1.0f,  20.0f,  20.0f, 1.0f, 2.0f, false, true, false, false, 0, false },
        /*
         * THE CASE THE ENVELOPE AXIS WAS CHANGED FOR: a 442 ms decay on a
         * 1/64 step, which is 15.6 ms at 120 BPM. On the old one-step axis
         * this drew the first 3% of the decay as a near-flat line -- and,
         * because the preview asked the engine for a tempo it refuses above
         * 1000 BPM, drew it on a time base that was silently 120 BPM, so the
         * gate never closed in the picture at all.
         */
        { "tg_slow", 32, 11,  1.0f,  20.0f, 442.0f, 0.0f, 2.0f, false, false, false, false, 0, false },
        /*
         * LEGATO AGAINST A RUN OF ON STEPS, which is the picture of what it
         * does: with Gate at half the pattern plot is a row of separate
         * pulses, and legato joins them into one long gate. Nothing else in
         * this set has adjacent ON steps, so nothing else could show it.
         */
        /* ATTACK 0 opens the gate on the very first sample, which is the
         * case that exposed a fill closing to the wrong corner. */
        { "tg_legato", 16, 7, 0.5f, 20.0f, 20.0f, 1.0f, 0.0f, false, false, true, true, 0, false },
        /*
         * THE SMALL END OF THE AXIS LADDER. A 1/128 step is 15.6 ms at
         * 120 BPM, and with short stages the whole span is about 20 -- where
         * a fixed interval collapses to one tick and the ladder has to reach
         * for 2 or 5 ms. The other shots all exercise its top end.
         */
        { "tg_fast", 32, 12,  0.6f,  4.0f,   3.0f, 0.5f, 1.0f, false, false, false, false, 0, false },
        /*
         * THE THREE CURVES, FROM IDENTICAL NUMBERS. Same ADSR, same rate,
         * same width -- only the shape differs, so anything that moves
         * between these three shots moved because of the curve and nothing
         * else. The sustain is deliberately below full: at 100% the decay has
         * nowhere to travel and all three shapes draw the same flat top.
         */
        { "tg_lin", 16, 7,  0.7f, 120.0f, 120.0f, 0.4f, 60.0f, false, false, false, false, 0, false },
        { "tg_exp", 16, 7,  0.7f, 120.0f, 120.0f, 0.4f, 60.0f, false, false, false, false, 1, false },
        { "tg_scv", 16, 7,  0.7f, 120.0f, 120.0f, 0.4f, 60.0f, false, false, false, false, 2, false },
        /* The scope, with two seconds of a 220 Hz sine actually pushed
         * through the processor first -- dry behind, gated in front. */
        { "tg_scope", 16, 7, 0.5f, 40.0f, 40.0f, 0.6f, 10.0f, false, false, false, false, 0, true },
    };

    for (auto& s : shots)
    {
        TranceGateProcessor proc;
        auto* lenP = proc.state().getParameter ("length");
        lenP->setValueNotifyingHost (lenP->convertTo0to1 ((float) s.length));
        auto* rateP = proc.state().getParameter ("rate");
        rateP->setValueNotifyingHost (rateP->convertTo0to1 ((float) s.rate));
        for (auto pair : { std::make_pair ("hold", s.hold),
                           std::make_pair ("release", s.release),
                           std::make_pair ("decay", s.decay),
                           std::make_pair ("sustain", s.sustain),
                           std::make_pair ("legato", s.legato ? 1.0f : 0.0f),
                           std::make_pair ("attack", s.attack) })
        {
            auto* q = proc.state().getParameter (pair.first);
            q->setValueNotifyingHost (q->convertTo0to1 (pair.second));
        }

        if (s.ends)
        {
            /* Minimum on one, maximum on the next: an arc that starts or ends
             * at the wrong angle is obvious here and nowhere else. */
            const char* ids[] = { "amount", "attack", "decay", "sustain" };
            for (int k = 0; k < 4; ++k)
            {
                auto* q = proc.state().getParameter (ids[k]);
                q->setValueNotifyingHost (k % 2 == 0 ? 0.0f : 1.0f);
            }
        }

        /* A pattern with some shape in it: every third step, a few ties, and
         * a sweep of per-step amounts, so the drawing has something to get
         * wrong. */
        for (int i = 0; i < s.length; ++i)
        {
            proc.engineSet ("cursor", juce::String (i));
            proc.engineSet ("step", s.dense ? "On"
                                            : (i % 3 == 0) ? ((i % 12 == 0) ? "Tie" : "On")
                                                           : "Off");
            proc.engineSet ("step_amount",
                            juce::String (0.35f + 0.6f * (float) i / (float) s.length, 3));
        }
        proc.engineSet ("cursor", "5");

        /* A running transport, so the playhead is lit and the ring shows its
         * current segment in ink. Without a play head the plugin correctly
         * reports "stopped" and there is nothing to photograph. */
        struct Moving : juce::AudioPlayHead
        {
            /* Advanced by the scope's capture loop; the playhead shot leaves
             * it at zero and relies on the fixed offset below. */
            double samples = -1.0;
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setIsPlaying (true);
                p.setBpm (120.0);
                /* A quarter into the second beat when parked, or wherever the
                 * capture loop has got to. */
                p.setPpqPosition (samples < 0.0 ? 1.25
                                                : samples / 44100.0 * 2.0);
                return p;
            }
        } playHead;

        if (s.playing)
        {
            proc.setPlayHead (&playHead);
            proc.prepareToPlay (44100.0, 64);
            juce::AudioBuffer<float> buf (2, 64);
            juce::MidiBuffer midi;
            for (int k = 0; k < 8; ++k) { buf.clear(); proc.processBlock (buf, midi); }
        }

        {
            auto* q = proc.state().getParameter ("curve");
            q->setValueNotifyingHost (q->convertTo0to1 ((float) s.curve));
        }

        /*
         * THE SCOPE NEEDS AUDIO TO HAVE HAPPENED. Its capture is filled from
         * processBlock, so a shot of it taken from a processor that has never
         * run shows an empty band -- which would photograph as "the scope is
         * broken" rather than "nothing was played".
         */
        if (s.signal)
        {
            proc.setPlayHead (&playHead);
            proc.prepareToPlay (44100.0, 128);
            juce::AudioBuffer<float> buf (2, 128);
            juce::MidiBuffer midi;
            double phase = 0.0;
            const int blocks = (int) (44100.0 * 2.0 / 128.0);
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < 128; ++i)
                {
                    const float v = (float) std::sin (phase) * 0.8f;
                    phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / 44100.0;
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }
                playHead.samples = (double) (b * 128);
                proc.processBlock (buf, midi);
            }
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        if (auto* tg = dynamic_cast<TranceGateEditor*> (ed.get()))
            tg->showSignalPlot (s.signal);
        ed->setSize (ed->getWidth(), ed->getHeight());
        /* The editor fills its picture from the `ui` readout on a timer; one
         * dispatch pass is enough for the first frame. */
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true);

        /* A magnified crop of the control block, so the component details --
         * the readout's box, the unit's colour, the knob's rail and pointer --
         * can be compared against the design system's own previews rather
         * than squinted at in a full-window shot. */
        /* The ring's playhead is one segment in `ink` among segments in
         * phosphor -- a real difference that a full-window shot cannot
         * resolve, so the playing state is photographed close up. */
        const bool wantDetail = juce::String (s.name) == "tg_16"
                             || juce::String (s.name) == "tg_ends"
                             || juce::String (s.name) == "tg_fast"
                             || juce::String (s.name) == "tg_cut"
                             || s.curve != 0
                             || juce::String (s.name) == "tg_lin"
                             || s.playing;
        if (wantDetail)
        {
            /* The playhead is a ring fact; the millisecond axis is an
             * envelope-plot fact, and at 10px its labels are unreadable in a
             * full-window shot. */
            const auto crop = (juce::String (s.name).startsWith ("tg_lin")
                            || juce::String (s.name).startsWith ("tg_exp")
                            || juce::String (s.name).startsWith ("tg_scv")
                            || juce::String (s.name) == "tg_fast"
                            || juce::String (s.name) == "tg_cut")
                                ? juce::Rectangle<int> { 24, 280, 260, 116 }  /* the envelope plot */
                            : s.playing
                                ? juce::Rectangle<int> { 24, 24, 260, 260 }   /* the ring */
                                : juce::Rectangle<int> { 296, 24, 400, 180 }; /* the panel */
            const auto detail = ed->createComponentSnapshot (crop, true, 3.0f);
            const auto df = out.getChildFile (juce::String (s.name) + "_detail.png");
            df.deleteFile();
            std::unique_ptr<juce::FileOutputStream> dos (df.createOutputStream());
            if (dos != nullptr) juce::PNGImageFormat().writeImageToStream (detail, *dos);
        }

        /*
         * THE TWO PLOTS, CLOSE UP. The envelope's marks are a hairline and a
         * 2px rule a few pixels apart at a fast rate, and the pattern's step
         * gridlines are 6px apart at 128 -- neither survives a full-window
         * shot. The bounds come from the editor rather than from a literal,
         * so moving the layout moves the crop with it.
         */
        const juce::String nm (s.name);
        if (nm == "tg_16" || nm == "tg_128" || nm == "tg_slow" || nm == "tg_legato")
        {
            auto box = ed->getLocalBounds()
                         .withTop (TranceGateEditor::plotStripTop())
                         .withHeight (TranceGateEditor::plotStripHeight());
            const auto strip = ed->createComponentSnapshot (box, true, 2.0f);
            const auto sf = out.getChildFile (nm + "_plot.png");
            sf.deleteFile();
            std::unique_ptr<juce::FileOutputStream> sos (sf.createOutputStream());
            if (sos != nullptr) juce::PNGImageFormat().writeImageToStream (strip, *sos);
        }
        juce::PNGImageFormat png;
        const auto file = out.getChildFile (juce::String (s.name) + ".png");
        file.deleteFile();
        std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
        if (os != nullptr && png.writeImageToStream (img, *os))
            std::printf ("  wrote %s (%d x %d)\n", file.getFullPathName().toRawUTF8(),
                         img.getWidth(), img.getHeight());
        else
            std::printf ("  FAILED to write %s\n", file.getFullPathName().toRawUTF8());
    }
    return 0;
}
