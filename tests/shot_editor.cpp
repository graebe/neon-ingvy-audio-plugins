/* Renders the editor to PNG at a few pattern lengths, so the layout can be
 * LOOKED AT rather than reasoned about. The grid grows rows with the pattern
 * and the window does not, which is exactly the sort of change that collides
 * with something two panels away. */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../plugins/trance-gate/Source/PluginProcessor.h"
#include "../plugins/trance-gate/Source/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out = juce::File (argc > 1 ? argv[1] : "/tmp");

    /* `hold` and `release` are set only for the last shot: a long release on
     * a short step is the case the envelope panel exists to make visible, and
     * a marker that overlaps the curve is not something an assertion catches. */
    struct { const char* name; int length; int rate; float hold, release; } shots[] = {
        { "tg_16",  16,  7,  1.0f,  20.0f },
        { "tg_64",  64,  9,  1.0f,  20.0f },
        { "tg_128", 128, 12, 1.0f,  20.0f },
        { "tg_cut", 32,  9,  0.5f, 500.0f },
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

        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (ed->getWidth(), ed->getHeight());
        /* The editor fills its picture from the `ui` readout on a timer; one
         * dispatch pass is enough for the first frame. */
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);

        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true);
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
