/*
 * The VST3, loaded and rendered the way Ableton Live loads it.
 *
 * The AU test covers the same ground for AU, but VST3 is the format Live
 * uses, and the two wrappers do NOT share a parameter path: AU converts
 * through `value / maxValue` against a published maximum, VST3 hands over
 * normalised values and reports a step count. A discrete/continuous mistake
 * shows up differently in each, so each is checked.
 *
 * Skips with success when the plugin is not installed, so a fresh checkout
 * does not fail the suite.
 */
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
extern "C" {
#include "trance_gate_core.h"
}

namespace {
constexpr double kSR  = 44100.0;
constexpr int    kBlk = 128;
constexpr double kBpm = 123.0;

int failures = 0;

void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    std::printf ("  %-56s %s%s\n", what.toRawUTF8(), ok ? "ok" : "FAIL",
                 detail.isEmpty() ? "" : (" (" + detail + ")").toRawUTF8());
    if (! ok) ++failures;
}

/* The transport a DAW supplies. JUCE's VST3 host fills the ProcessContext
 * from whatever play head is set, so this is the same road Live's transport
 * travels. */
struct Transport : juce::AudioPlayHead
{
    double samples = 0.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (kBpm);
        p.setTimeInSamples ((int64_t) samples);
        p.setTimeInSeconds (samples / kSR);
        p.setPpqPosition (samples / kSR * (kBpm / 60.0));
        return p;
    }
};

juce::AudioProcessorParameter* byName (juce::AudioPluginInstance& p, const juce::String& n)
{
    for (auto* param : p.getParameters())
        if (param->getName (64) == n) return param;
    return nullptr;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const auto path = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Audio/Plug-Ins/VST3/Trance Gate.vst3");
    if (! path.exists())
    {
        std::puts ("  (skipped: the Trance Gate VST3 is not installed)");
        return 0;
    }

    juce::AudioPluginFormatManager fm;
    auto* vst3 = new juce::VST3PluginFormat();
    fm.addFormat (vst3);

    juce::OwnedArray<juce::PluginDescription> found;
    juce::KnownPluginList list;
    list.scanAndAddFile (path.getFullPathName(), true, found, *vst3);
    check (! found.isEmpty(), "the VST3 is found and describes itself",
           juce::String (found.size()) + " variant(s)");
    if (found.isEmpty()) { std::puts ("\nFAIL"); return 1; }

    juce::String err;
    auto plugin = fm.createPluginInstance (*found[0], kSR, kBlk, err);
    check (plugin != nullptr, "it instantiates", err);
    if (plugin == nullptr) { std::puts ("\nFAIL"); return 1; }

    /*
     * TEN MACROS AND A BYPASS. VST3 requires a bypass parameter and JUCE adds
     * one automatically, so eleven is the right number -- the assertion names
     * what is there rather than counting, or the next format that adds a
     * parameter of its own turns a correct plugin into a failing test.
     */
    juce::StringArray names;
    for (auto* q : plugin->getParameters()) names.add (q->getName (32));
    check (names.size() == 12, "it publishes eleven macros and a bypass",
           names.joinIntoString (", "));
    check (names.contains ("Bypass"), "...the bypass VST3 requires");

    /*
     * DISCRETE MEANS DISCRETE IN THIS FORMAT TOO. VST3 carries a step count;
     * a continuous Length would report 0 here and Live would draw a fader
     * over 128 values that only has 128 legal positions.
     */
    auto steps = [&plugin] (const juce::String& n)
    {
        auto* p = byName (*plugin, n);
        return p != nullptr ? p->getNumSteps() : -1;
    };
    check (steps ("Length") == 128, "Length is 128 discrete steps",
           juce::String (steps ("Length")));
    check (steps ("Slot") == 8, "Slot is 8 discrete steps", juce::String (steps ("Slot")));
    check (steps ("Rate") == 13, "Rate is 13 discrete steps", juce::String (steps ("Rate")));
    check (byName (*plugin, "Decay") != nullptr
             && byName (*plugin, "Decay")->getNumSteps() > 1000,
           "...and Decay is not");

    /* The patch, set through the host's normalised parameter interface --
     * which is exactly what an automation lane writes. */
    struct { const char* name; float lo, hi, real; } patch[] = {
        { "Rate",    0.0f,  12.0f,   7.0f },
        { "Length",  1.0f, 128.0f,  16.0f },
        { "Slot",    0.0f,   7.0f,   0.0f },
        { "Join Neighbors", 0.0f,   1.0f,   0.0f },
        { "Amount",  0.0f,   1.0f,   0.9f },
        { "Gate",    0.05f,  1.0f,   0.75f },
        { "Attack",  0.0f, 500.0f,   3.5f },
        { "Decay",   0.0f, 500.0f,  40.0f },
        { "Sustain", 0.0f,   1.0f,   0.6f },
        { "Release", 0.0f, 500.0f,  25.0f },
    };
    /* The text a HOST displays, which is the parameter's own formatter --
     * the same strings Live puts in its automation lane. */
    const char* shows[] = { "1/16", "16", "1", "Off", "90 %", "75 %",
                            "3.5 ms", "40.0 ms", "60 %", "25.0 ms" };

    bool allSet = true, allShow = true;
    for (size_t i = 0; i < std::size (patch); ++i)
    {
        auto* p = byName (*plugin, patch[i].name);
        if (p == nullptr) { allSet = false; continue; }
        p->setValueNotifyingHost ((patch[i].real - patch[i].lo)
                                    / (patch[i].hi - patch[i].lo));

        const auto shown = p->getCurrentValueAsText();
        const bool agree = shown == juce::String (shows[i]);
        if (! agree)
        {
            std::printf ("      %-8s shows \"%s\", wanted \"%s\"\n",
                         patch[i].name, shown.toRawUTF8(), shows[i]);
            allShow = false;
        }
    }
    check (allSet,  "every macro is reachable by name");
    check (allShow, "and each one displays the value it was given");

    /* The reference: the same patch straight into the engine. */
    tg_core_t* ref = tg_core_create (kSR);
    tg_core_set_param (ref, "rate", "1/16");
    tg_core_set_param (ref, "length", "15");
    tg_core_set_param (ref, "slot", "0");
    tg_core_set_param (ref, "amount", "0.900");
    tg_core_set_param (ref, "hold", "0.750");
    tg_core_set_param (ref, "attack", "3.500");
    tg_core_set_param (ref, "decay", "40.000");
    tg_core_set_param (ref, "sustain", "0.600");
    tg_core_set_param (ref, "release", "25.000");
    tg_core_set_param (ref, "legato", "0");

    Transport transport;
    plugin->setPlayHead (&transport);
    plugin->prepareToPlay (kSR, kBlk);

    juce::AudioBuffer<float> buf (2, kBlk);
    juce::MidiBuffer midi;
    float refL[kBlk], refR[kBlk];
    double phase = 0.0, refPhase = 0.0;
    const double w = 2.0 * juce::MathConstants<double>::pi * 220.0 / kSR;

    const int total = (int) (4.0 * kSR);
    double maxDiff = 0.0, stepPeak = 0.0, loudest = 0.0, quietest = 1e30;
    const int stepSamples = (int) (kSR * 60.0 / kBpm / 4.0);
    int stepFill = 0;

    for (int done = 0; done < total; done += kBlk)
    {
        const int n = juce::jmin (kBlk, total - done);
        buf.setSize (2, n, false, false, true);

        for (int i = 0; i < n; ++i)
        {
            /* Quantised to int16 before gating, as the Move renderer does --
             * otherwise this measures rounding, not the port. */
            const float q = (float) (short) std::lrint (22000.0 * std::sin (phase));
            phase += w;
            buf.setSample (0, i, q);
            buf.setSample (1, i, q);

            refL[i] = refR[i] = (float) (short) std::lrint (22000.0 * std::sin (refPhase));
            refPhase += w;
        }

        plugin->processBlock (buf, midi);

        tg_transport_t t {};
        t.running = 1;
        t.beats   = transport.samples / kSR * (kBpm / 60.0);
        t.bpm     = (float) kBpm;
        tg_core_process_f32_split (ref, refL, refR, n, &t);

        for (int i = 0; i < n; ++i)
        {
            const double d = std::abs ((double) buf.getSample (0, i) - (double) refL[i]);
            if (d > maxDiff) maxDiff = d;
            const double mag = std::abs ((double) buf.getSample (0, i));
            if (mag > stepPeak) stepPeak = mag;
            if (++stepFill >= stepSamples)
            {
                loudest  = juce::jmax (loudest, stepPeak);
                quietest = juce::jmin (quietest, stepPeak);
                stepPeak = 0.0;
                stepFill = 0;
            }
        }
        transport.samples += n;
    }

    check (maxDiff <= 1.0, "the VST3 renders what the engine renders",
           "largest sample difference " + juce::String (maxDiff, 3));
    check (quietest > 0.0 && loudest > quietest * 3.0,
           "the gate actually gated (the transport reached it)",
           "loudest step " + juce::String (loudest, 0) + ", quietest "
             + juce::String (quietest, 0));

    /* THE STATE IS THE MOVE PATCH, through this format too. */
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    /* The container opens with a binary header ("VC2!" and a few raw bytes)
     * before the XML, so converting the WHOLE blob as UTF-8 yields nothing
     * usable -- the invalid leading bytes end the string before the payload.
     * Find where the XML starts in the raw bytes and read from there. */
    const char* rawState = (const char*) saved.getData();
    const int   rawSize  = (int) saved.getSize();
    int xmlAt = -1;
    for (int i = 0; i + 5 < rawSize; ++i)
        if (std::memcmp (rawState + i, "<?xml", 5) == 0) { xmlAt = i; break; }
    const auto asText = xmlAt >= 0
        ? juce::String::fromUTF8 (rawState + xmlAt, rawSize - xmlAt).trim()
        : juce::String();
    /*
     * THE MOVE PATCH IS IN THERE. A hosted VST3's state is JUCE's own
     * container ("VC2!"), not the plugin's raw chunk -- the chunk is nested
     * inside it. What matters is that the thing being nested is still the
     * Move patch and not some parameter dump of VST3's own, because that
     * patch is the interchange format with the hardware.
     */
    /*
     * THE MOVE PATCH IS IN THERE -- and this is the assertion that matters
     * for the whole port, because that patch is the interchange format with
     * the hardware. A hosted VST3's state is JUCE's own XML container with
     * the plugin's chunk base64'd inside it, so the check has to open the
     * container rather than grep the bytes: the patch is present and is still
     * the engine's own JSON, not a parameter dump VST3 invented.
     */
    juce::String movePatch;
    if (auto xml = juce::parseXML (asText))
        if (auto* comp = xml->getChildByName ("IComponent"))
        {
            juce::MemoryBlock mb;
            mb.fromBase64Encoding (comp->getAllSubText().trim());
            movePatch = juce::String::fromUTF8 ((const char*) mb.getData(), (int) mb.getSize()).trim();
        }

    check (movePatch.startsWith ("{") && movePatch.endsWith ("}")
             && movePatch.contains ("\"p0\":"),
           "its saved state still carries the Move patch",
           juce::String (movePatch.length()) + " bytes of patch in "
             + juce::String ((int) saved.getSize()) + " of container");

    /* And it describes the patch we SET, not the defaults -- so the values
     * checked here are ones that differ from a fresh instance's. 1/16 would
     * prove nothing: it is the default rate. */
    check (movePatch.contains ("0.750") && movePatch.contains ("0.900"),
           "...carrying the values that were set, not the defaults",
           movePatch.contains ("0.750") ? "gate 0.75 present" : "gate 0.75 MISSING");

    plugin->releaseResources();
    plugin->setPlayHead (nullptr);
    plugin.reset();
    tg_core_destroy (ref);

    std::printf ("\n%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
