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
                  float hold, release; bool ends, playing; };
    const Shot shots[] = {
        { "tg_16",   16,  7,  1.0f,  20.0f, false, false },
        { "tg_64",   64,  9,  1.0f,  20.0f, false, false },
        { "tg_128", 128, 12,  1.0f,  20.0f, false, false },
        { "tg_cut",  32,  9,  0.5f, 500.0f, false, false },
        { "tg_ends", 16,  0,  0.05f, 0.0f,  true,  false },
        { "tg_play", 32,  7,  1.0f,  20.0f, false, true  },
    };

    for (auto& s : shots)
    {
        TranceGateProcessor proc;
        auto* lenP = proc.state().getParameter ("length");
        lenP->setValueNotifyingHost (lenP->convertTo0to1 ((float) s.length));
        auto* rateP = proc.state().getParameter ("rate");
        rateP->setValueNotifyingHost (rateP->convertTo0to1 ((float) s.rate));
        for (auto pair : { std::make_pair ("hold", s.hold), std::make_pair ("release", s.release) })
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
            proc.engineSet ("step", (i % 3 == 0) ? ((i % 12 == 0) ? "Tie" : "On") : "Off");
            proc.engineSet ("step_amount",
                            juce::String (0.35f + 0.6f * (float) i / (float) s.length, 3));
        }
        proc.engineSet ("cursor", "5");

        /* A running transport, so the playhead is lit and the ring shows its
         * current segment in ink. Without a play head the plugin correctly
         * reports "stopped" and there is nothing to photograph. */
        struct Moving : juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo p;
                p.setIsPlaying (true);
                p.setBpm (120.0);
                p.setPpqPosition (1.25);       /* a quarter into the second beat */
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

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
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
                             || s.playing;
        if (wantDetail)
        {
            /* The playhead is a ring fact; glow-focus is a knob fact. */
            const auto crop = s.playing
                                ? juce::Rectangle<int> { 24, 24, 260, 260 }   /* the ring */
                                : juce::Rectangle<int> { 296, 24, 400, 180 }; /* the panel */
            const auto detail = ed->createComponentSnapshot (crop, true, 3.0f);
            const auto df = out.getChildFile (juce::String (s.name) + "_detail.png");
            df.deleteFile();
            std::unique_ptr<juce::FileOutputStream> dos (df.createOutputStream());
            if (dos != nullptr) juce::PNGImageFormat().writeImageToStream (detail, *dos);
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
